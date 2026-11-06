#include "UpdateService.h"

#include "Logger.h"
#include "Platform.h"
#include "RecordingPanelState.h"
#include "Strings.h"
#include "TextService.h"
#include "UserSettings.h"
#include "Version.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <winhttp.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <limits>
#include <stdexcept>

namespace vmex::update
{
    namespace
    {
        constexpr std::wstring_view kLastCheck = L"UpdateLastCheckUtc";
        constexpr std::wstring_view kDownloadRoot = L"https://github.com/EricZhang233/VolumeMixerExtender/releases/download/";

        struct Handle final
        {
            HANDLE value = nullptr;
            ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
        };

        struct HttpHandle final
        {
            std::atomic<HINTERNET> value;
            explicit HttpHandle(HINTERNET handle) : value(handle) {}
            ~HttpHandle() { Close(); }
            void Close() { if (const auto handle = value.exchange(nullptr)) ::WinHttpCloseHandle(handle); }
            HINTERNET Get() const { return value.load(); }
        };

        std::uint64_t Now()
        {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        std::uint64_t LastCheck()
        {
            const auto stored = settings::ReadString(kLastCheck, L"0");
            std::int64_t value = 0;
            return strings::TryParseInt(stored, value) && value > 0 ? static_cast<std::uint64_t>(value) : 0;
        }

        std::wstring MutexName()
        {
            return L"Local\\VmExt.Update.S" + std::to_wstring(platform::GetCurrentSessionId());
        }

        bool UpdateInProgress()
        {
            Handle mutex{ ::OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MutexName().c_str()) };
            if (!mutex.value) return ::GetLastError() != ERROR_FILE_NOT_FOUND;
            const DWORD wait = ::WaitForSingleObject(mutex.value, 0);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return true;
            ::ReleaseMutex(mutex.value);
            return false;
        }

        void Report(const Status& result)
        {
            log::Logger::Instance().WriteFormat(result ? log::Level::Info : log::Level::Warn,
                L"update", L"Update check completed code={0} detail={1}", { std::to_wstring(result.code), result.detail });
        }

        std::wstring Fetch(std::stop_token stop)
        {
            HttpHandle session(::WinHttpOpen(L"VolumeMixerExtender-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session.Get()) throw std::runtime_error("update-http-session");
            ::WinHttpSetTimeouts(session.Get(), 5000, 5000, 5000, 5000);
            HttpHandle connection(::WinHttpConnect(session.Get(), L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!connection.Get()) throw std::runtime_error("update-http-connect");
            HttpHandle request(::WinHttpOpenRequest(connection.Get(), L"GET",
                L"/repos/EricZhang233/VolumeMixerExtender/releases?per_page=100", nullptr, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            if (!request.Get()) throw std::runtime_error("update-http-request");
            std::stop_callback cancel(stop, [&request] { request.Close(); });
            if (stop.stop_requested()) throw std::runtime_error("update-cancelled");
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            ::WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
            const auto started = std::chrono::steady_clock::now();
            if (!::WinHttpSendRequest(request.Get(), L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
                static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || stop.stop_requested() ||
                !::WinHttpReceiveResponse(request.Get(), nullptr)) throw std::runtime_error("update-http-response");
            DWORD status = 0, bytes = sizeof(status);
            if (!::WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) || status != 200)
                throw std::runtime_error("update-http-status");
            std::string body;
            char buffer[16384];
            for (;;)
            {
                if (stop.stop_requested() || std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
                    throw std::runtime_error("update-http-timeout");
                DWORD received = 0;
                if (!::WinHttpReadData(request.Get(), buffer, sizeof(buffer), &received))
                    throw std::runtime_error("update-http-read");
                if (!received) break;
                body.append(buffer, received);
                if (body.size() > 4 * 1024 * 1024) throw std::runtime_error("update-metadata-size");
            }
            return strings::ToWide(body);
        }
    }

    std::optional<std::array<unsigned, 4>> ParseVersion(std::wstring_view value)
    {
        if (!value.empty() && (value.front() == L'v' || value.front() == L'V')) value.remove_prefix(1);
        std::array<unsigned, 4> result{};
        std::size_t component = 0, digits = 0;
        for (const wchar_t ch : value)
        {
            if (ch == L'.')
            {
                if (!digits || ++component >= result.size()) return {};
                digits = 0;
            }
            else
            {
                if (ch < L'0' || ch > L'9' || ++digits > 5) return {};
                result[component] = result[component] * 10 + static_cast<unsigned>(ch - L'0');
                if (result[component] > 65535) return {};
            }
        }
        return digits && component >= 2 ? std::optional(result) : std::nullopt;
    }

    bool ValidDigest(std::wstring_view value)
    {
        return value.size() == 71 && value.substr(0, 7) == L"sha256:" &&
            std::all_of(value.begin() + 7, value.end(), [](wchar_t ch)
                { return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F'); });
    }

    bool CheckDue(std::uint64_t last, std::uint64_t now)
    {
        return !last || now < last || now - last >= kCheckIntervalSeconds;
    }

    std::optional<Release> SelectRelease(std::wstring_view json, std::wstring_view current)
    {
        using namespace winrt::Windows::Data::Json;
        const auto installed = ParseVersion(current);
        if (!installed) return {};
        auto best = *installed;
        std::optional<Release> selected;
        for (const auto& item : JsonArray::Parse(winrt::hstring(json)))
        {
            const auto release = item.GetObject();
            if (release.GetNamedBoolean(L"draft", false) || release.GetNamedBoolean(L"prerelease", false)) continue;
            const std::wstring tag(release.GetNamedString(L"tag_name", L""));
            const auto version = ParseVersion(tag);
            if (!version || *version <= best) continue;
            const std::wstring number = tag.starts_with(L"v") || tag.starts_with(L"V") ? tag.substr(1) : tag;
            const auto name = strings::ToWide(VMEX_PRODUCT_NAME) + L"_" + number + L".zip";
            for (const auto& entry : release.GetNamedArray(L"assets", JsonArray()))
            {
                const auto asset = entry.GetObject();
                if (std::wstring(asset.GetNamedString(L"name", L"")) != name) continue;
                const std::wstring url(asset.GetNamedString(L"browser_download_url", L""));
                const auto hash = asset.GetNamedValue(L"digest", JsonValue::CreateNullValue());
                if (hash.ValueType() != JsonValueType::String) continue;
                const std::wstring digest(hash.GetString());
                const double size = asset.GetNamedNumber(L"size", 0);
                if (url != std::wstring(kDownloadRoot) + tag + L"/" + name || !ValidDigest(digest) ||
                    size <= 0 || size > 1024.0 * 1024 * 1024 || std::floor(size) != size) continue;
                selected = Release{ number, tag, url, digest, static_cast<std::uint64_t>(size) };
                best = *version;
                break;
            }
        }
        return selected;
    }

    Status Check(std::optional<Release>& release, std::stop_token stop)
    {
        log::OperationTimer timing(L"update", L"CheckStableRelease");
        release.reset();
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            release = SelectRelease(Fetch(stop), strings::ToWide(VMEX_VERSION_STRING));
            const auto result = Status::Ok();
            log::Logger::Instance().WriteFormat(log::Level::Info, L"update", L"Stable release checked target={0}",
                { release ? release->version : strings::ToWide(VMEX_VERSION_STRING) });
            return result;
        }
        catch (const winrt::hresult_error& error) { return Status::FromHResult(L"update-check", error.code()); }
        catch (const std::exception& error) { return Status::Failed(strings::ToWide(error.what())); }
    }

    Status ExtractUpdater(const std::filesystem::path& executable)
    {
        HMODULE module = nullptr;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ExtractUpdater), &module)) return Status::FromLastError(L"update-module");
        const HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(200), RT_RCDATA);
        const auto loaded = resource ? ::LoadResource(module, resource) : nullptr;
        const auto data = loaded ? ::LockResource(loaded) : nullptr;
        const DWORD size = resource ? ::SizeofResource(module, resource) : 0;
        if (!data || !size) return Status::Failed(L"update-payload-missing");
        return platform::WriteAllBytes(executable,
            std::span(static_cast<const std::byte*>(data), size));
    }

    std::wstring ReleasePage(const Release& release)
    {
        if (!ParseVersion(release.version) || (release.tag != release.version &&
            release.tag != L"v" + release.version && release.tag != L"V" + release.version)) return {};
        return L"https://github.com/EricZhang233/VolumeMixerExtender/releases/tag/" + release.tag;
    }

    Status Launch(const Release& release, std::uint32_t managerProcessId, void** processHandle)
    {
        using namespace winrt::Windows::Data::Json;
        Handle mutex{ ::CreateMutexW(nullptr, FALSE, MutexName().c_str()) };
        if (!mutex.value) return Status::FromLastError(L"update-mutex");
        const DWORD wait = ::WaitForSingleObject(mutex.value, 0);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return Status::Failed(L"update-in-progress");
        struct Unlock { HANDLE value; ~Unlock() { ::ReleaseMutex(value); } } unlock{ mutex.value };
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            if (!ValidDigest(release.digest) || !ParseVersion(release.version) ||
                release.url != std::wstring(kDownloadRoot) + release.tag + L"/" +
                    strings::ToWide(VMEX_PRODUCT_NAME) + L"_" + release.version + L".zip")
                return Status::Failed(L"update-invalid-release");
            const auto cache = platform::GetCacheDirectory() / L"updater";
            std::filesystem::create_directories(cache);
            const auto executable = cache / L"vmex_updater.exe";
            const auto written = ExtractUpdater(executable);
            if (!written) return written;
            GUID id{};
            if (FAILED(::CoCreateGuid(&id))) return Status::Failed(L"update-request-id");
            wchar_t token[40]{};
            ::StringFromGUID2(id, token, 40);
            const auto request = cache / (std::wstring(token) + L".json");
            JsonObject manifest;
            manifest.Insert(L"directory", JsonValue::CreateStringValue(platform::GetApplicationDirectory().wstring()));
            manifest.Insert(L"version", JsonValue::CreateStringValue(release.version));
            manifest.Insert(L"tag", JsonValue::CreateStringValue(release.tag));
            manifest.Insert(L"url", JsonValue::CreateStringValue(release.url));
            manifest.Insert(L"digest", JsonValue::CreateStringValue(release.digest));
            manifest.Insert(L"size", JsonValue::CreateNumberValue(static_cast<double>(release.size)));
            manifest.Insert(L"managerProcessId", JsonValue::CreateNumberValue(managerProcessId));
            const auto encoded = strings::ToNarrow(std::wstring(manifest.Stringify()));
            const auto saved = platform::WriteAllBytes(request, std::vector<std::byte>(
                reinterpret_cast<const std::byte*>(encoded.data()), reinterpret_cast<const std::byte*>(encoded.data() + encoded.size())));
            if (!saved) return saved;
            std::wstring command = L"\"" + executable.wstring() + L"\" \"" + request.wstring() + L"\"";
            STARTUPINFOW startup{ sizeof(startup) };
            PROCESS_INFORMATION process{};
            if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, cache.c_str(), &startup, &process))
            {
                std::filesystem::remove(request);
                return Status::FromLastError(L"update-launch");
            }
            ::CloseHandle(process.hThread);
            if (processHandle) *processHandle = process.hProcess;
            else ::CloseHandle(process.hProcess);
            log::Logger::Instance().WriteFormat(log::Level::Info, L"update", L"Silent updater launched version={0} path={1}",
                { release.version, executable.wstring() });
            return Status::Ok();
        }
        catch (const winrt::hresult_error& error) { return Status::FromHResult(L"update-launch", error.code()); }
        catch (const std::exception& error) { return Status::Failed(strings::ToWide(error.what())); }
    }

    InteractiveSession::InteractiveSession(Checker checker, Opener opener, Launcher launcher)
        : m_checker(checker ? std::move(checker) : [](std::optional<Release>& release) { return update::Check(release); })
        , m_opener(opener ? std::move(opener) : [](std::wstring_view url)
        {
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", std::wstring(url).c_str(),
                nullptr, nullptr, SW_SHOWNORMAL));
            return result > 32 ? Status::Ok() : Status::Failed(L"update-release-open:" + std::to_wstring(result));
        })
        , m_launcher(launcher ? std::move(launcher) : [this](const Release& release)
            { return Launch(release, ::GetCurrentProcessId(), &m_process); })
    {
    }

    InteractiveSession::~InteractiveSession() { if (m_process) ::CloseHandle(m_process); }

    Status InteractiveSession::Check(bool& available)
    {
        std::lock_guard lock(m_mutex);
        m_release.reset();
        available = false;
        auto result = m_checker(m_release);
        if (!result) m_release.reset();
        available = m_release.has_value();
        Report(result);
        return result;
    }

    Status InteractiveSession::OpenRelease()
    {
        std::lock_guard lock(m_mutex);
        const auto page = m_release ? ReleasePage(*m_release) : std::wstring{};
        const auto result = page.empty() ? Status::Failed(L"update-not-checked") : m_opener(page);
        log::Logger::Instance().WriteFormat(result ? log::Level::Info : log::Level::Warn,
            L"update", L"Release page opened url={0} code={1}", { page, std::to_wstring(result.code) });
        return result;
    }

    Status InteractiveSession::Install()
    {
        std::lock_guard lock(m_mutex);
        if (m_process) return Status::Failed(L"update-in-progress");
        const auto result = m_release ? m_launcher(*m_release) : Status::Failed(L"update-not-checked");
        Report(result);
        return result;
    }

    Status InteractiveSession::Poll(bool& running)
    {
        std::lock_guard lock(m_mutex);
        running = false;
        if (!m_process) return Status::Ok();
        const DWORD wait = ::WaitForSingleObject(m_process, 0);
        if (wait == WAIT_TIMEOUT)
        {
            running = true;
            return Status::Ok();
        }
        DWORD exitCode = 0;
        const bool succeeded = wait == WAIT_OBJECT_0 && ::GetExitCodeProcess(m_process, &exitCode) && exitCode == 0;
        ::CloseHandle(std::exchange(m_process, nullptr));
        return succeeded ? Status::Ok() : Status::Failed(text::Embedded().Resolve(L"manager.update.installFailed"));
    }

    Service::~Service() { Stop(); }

    void Service::Start()
    {
        if (m_worker.joinable()) return;
        m_worker = std::jthread([this](std::stop_token stop)
        {
            auto retryAt = std::chrono::steady_clock::time_point::min();
            Handle installer;
            while (!stop.stop_requested())
            {
                if (installer.value && ::WaitForSingleObject(installer.value, 0) != WAIT_TIMEOUT)
                {
                    ::CloseHandle(std::exchange(installer.value, nullptr));
                    retryAt = std::chrono::steady_clock::now() + std::chrono::minutes(15);
                }
                recording::PanelState panel;
                const bool recordingActive = recording::PanelStateCache::Read(panel) &&
                    (panel.recording.enabled || panel.recording.cleanupPending);
                if (!installer.value && CheckDue(LastCheck(), Now()) &&
                    std::chrono::steady_clock::now() >= retryAt && !UpdateInProgress())
                {
                    retryAt = std::chrono::steady_clock::now() + std::chrono::minutes(15);
                    std::optional<Release> release;
                    auto result = Check(release, stop);
                    if (!stop.stop_requested())
                    {
                        if (result && release && !recordingActive) result = Launch(*release, 0, &installer.value);
                        else if (result && !release) settings::WriteString(kLastCheck, std::to_wstring(Now()));
                        else if (result) log::Logger::Instance().Write(log::Level::Info, L"update", L"Update installation deferred while recording is active");
                        Report(result);
                    }
                }
                std::unique_lock lock(m_mutex);
                m_wake.wait_for(lock, stop, std::chrono::seconds(30), [] { return false; });
            }
        });
    }

    void Service::Stop()
    {
        m_worker.request_stop();
        m_wake.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }
}
