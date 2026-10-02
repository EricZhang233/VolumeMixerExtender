#include "ConfigService.h"

#include "Logger.h"
#include "Platform.h"
#include "Strings.h"
#include "TextEncoding.h"

#include <algorithm>

namespace vmex::config
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"config";

        std::wstring BuildDocument(const std::vector<Section>& sections)
        {
            std::wstring text;
            for (const auto& section : sections)
            {
                if (!text.empty())
                {
                    text.append(L"\r\n");
                }

                text.append(L"[");
                text.append(section.name);
                text.append(L"]\r\n");

                for (const auto& entry : section.entries)
                {
                    text.append(entry.key);
                    text.append(L"=");
                    text.append(entry.value);
                    text.append(L"\r\n");
                }
            }
            return text;
        }

        void ParseDocument(std::wstring_view text, std::vector<Section>& sections)
        {
            Section* current = nullptr;

            std::size_t start = 0;
            while (start <= text.size())
            {
                auto end = text.find(L'\n', start);
                if (end == std::wstring_view::npos)
                {
                    end = text.size();
                }

                auto line = text.substr(start, end - start);
                start = end + 1;

                if (!line.empty() && line.back() == L'\r')
                {
                    line.remove_suffix(1);
                }

                const auto trimmed = strings::TrimView(line);
                const bool last = end >= text.size();

                if (!trimmed.empty() && trimmed.front() != L';' && trimmed.front() != L'#')
                {
                    if (trimmed.front() == L'[')
                    {
                        const auto close = trimmed.find(L']');
                        if (close != std::wstring_view::npos)
                        {
                            const auto name = strings::TrimView(trimmed.substr(1, close - 1));
                            sections.push_back(Section{ std::wstring(name), {} });
                            current = &sections.back();
                        }
                    }
                    else
                    {
                        const auto separator = trimmed.find(L'=');
                        if (separator != std::wstring_view::npos)
                        {
                            if (current == nullptr)
                            {
                                sections.push_back(Section{ L"", {} });
                                current = &sections.back();
                            }

                            Entry entry;
                            entry.key = std::wstring(strings::TrimView(trimmed.substr(0, separator)));
                            entry.value = std::wstring(strings::TrimView(trimmed.substr(separator + 1)));
                            current->entries.push_back(std::move(entry));
                        }
                    }
                }

                if (last)
                {
                    break;
                }
            }
        }
    }

    Section* ConfigService::FindSection(std::wstring_view name)
    {
        const auto position = std::find_if(m_sections.begin(), m_sections.end(), [name](const Section& section) {
            return strings::EqualsIgnoreCase(section.name, name);
        });
        return position == m_sections.end() ? nullptr : &(*position);
    }

    const Section* ConfigService::FindSection(std::wstring_view name) const
    {
        const auto position = std::find_if(m_sections.begin(), m_sections.end(), [name](const Section& section) {
            return strings::EqualsIgnoreCase(section.name, name);
        });
        return position == m_sections.end() ? nullptr : &(*position);
    }

    Section& ConfigService::EnsureSection(std::wstring_view name)
    {
        if (auto* existing = FindSection(name))
        {
            return *existing;
        }

        m_sections.push_back(Section{ std::wstring(name), {} });
        return m_sections.back();
    }

    Status ConfigService::Load(const std::filesystem::path& file)
    {
        m_file = file;
        m_sections.clear();
        m_sourceEncoding = encoding::Encoding::Unknown;
        m_loaded = false;

        std::vector<std::byte> bytes;
        if (!platform::FileExists(file))
        {
            m_loaded = true;
            log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, L"log.config.created", { file.wstring() });
            return Status::Ok();
        }

        const auto status = platform::ReadAllBytes(file, bytes);
        if (!status.IsOk())
        {
            return status;
        }

        encoding::Decoded decoded;
        const auto decodeStatus = encoding::Decode(bytes, decoded);
        if (!decodeStatus.IsOk())
        {
            return decodeStatus;
        }

        m_sourceEncoding = decoded.encoding;
        ParseDocument(decoded.text, m_sections);
        m_loaded = true;

        log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, L"log.config.loaded", { file.wstring() });

        if (!SourceEncodingIsCanonical())
        {
            log::Logger::Instance().Write(log::Level::Warn, kChannel, std::wstring(L"config encoding is [" + std::wstring(encoding::ToString(m_sourceEncoding)) + L"], expected [utf-16le-bom]; non-ascii values may be unreliable until the next save"));
        }

        return Status::Ok();
    }

    Status ConfigService::Save()
    {
        return SaveAs(m_file);
    }

    Status ConfigService::SaveAs(const std::filesystem::path& file)
    {
        const auto document = BuildDocument(m_sections);
        const auto bytes = encoding::EncodeUtf16Le(document, true);

        const auto status = platform::WriteAllBytes(file, bytes);
        if (!status.IsOk())
        {
            return status;
        }

        m_file = file;
        m_sourceEncoding = encoding::Encoding::Utf16LeBom;
        log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, L"log.config.saved", { file.wstring() });
        return Status::Ok();
    }

    bool ConfigService::IsLoaded() const noexcept
    {
        return m_loaded;
    }

    const std::filesystem::path& ConfigService::FilePath() const noexcept
    {
        return m_file;
    }

    encoding::Encoding ConfigService::SourceEncoding() const noexcept
    {
        return m_sourceEncoding;
    }

    bool ConfigService::SourceEncodingIsCanonical() const noexcept
    {
        return m_sourceEncoding == encoding::Encoding::Utf16LeBom || m_sourceEncoding == encoding::Encoding::Unknown;
    }

    bool ConfigService::Has(std::wstring_view section, std::wstring_view key) const
    {
        const auto* found = FindSection(section);
        if (found == nullptr)
        {
            return false;
        }

        return std::any_of(found->entries.begin(), found->entries.end(), [key](const Entry& entry) {
            return strings::EqualsIgnoreCase(entry.key, key);
        });
    }

    std::wstring ConfigService::GetString(std::wstring_view section, std::wstring_view key, std::wstring_view fallback) const
    {
        const auto* found = FindSection(section);
        if (found == nullptr)
        {
            return std::wstring(fallback);
        }

        const auto position = std::find_if(found->entries.begin(), found->entries.end(), [key](const Entry& entry) {
            return strings::EqualsIgnoreCase(entry.key, key);
        });
        return position == found->entries.end() ? std::wstring(fallback) : position->value;
    }

    std::int64_t ConfigService::GetInt(std::wstring_view section, std::wstring_view key, std::int64_t fallback) const
    {
        std::int64_t value = 0;
        return strings::TryParseInt(GetString(section, key, L""), value) ? value : fallback;
    }

    double ConfigService::GetDouble(std::wstring_view section, std::wstring_view key, double fallback) const
    {
        double value = 0.0;
        return strings::TryParseDouble(GetString(section, key, L""), value) ? value : fallback;
    }

    bool ConfigService::GetBool(std::wstring_view section, std::wstring_view key, bool fallback) const
    {
        bool value = false;
        return strings::TryParseBool(GetString(section, key, L""), value) ? value : fallback;
    }

    void ConfigService::SetString(std::wstring_view section, std::wstring_view key, std::wstring_view value)
    {
        auto& target = EnsureSection(section);
        const auto position = std::find_if(target.entries.begin(), target.entries.end(), [key](const Entry& entry) {
            return strings::EqualsIgnoreCase(entry.key, key);
        });

        if (position == target.entries.end())
        {
            target.entries.push_back(Entry{ std::wstring(key), std::wstring(value) });
            return;
        }
        position->value.assign(value);
    }

    void ConfigService::SetInt(std::wstring_view section, std::wstring_view key, std::int64_t value)
    {
        SetString(section, key, std::to_wstring(value));
    }

    void ConfigService::SetBool(std::wstring_view section, std::wstring_view key, bool value)
    {
        SetString(section, key, value ? L"1" : L"0");
    }

    bool ConfigService::Remove(std::wstring_view section, std::wstring_view key)
    {
        auto* found = FindSection(section);
        if (found == nullptr)
        {
            return false;
        }

        const auto position = std::find_if(found->entries.begin(), found->entries.end(), [key](const Entry& entry) {
            return strings::EqualsIgnoreCase(entry.key, key);
        });
        if (position == found->entries.end())
        {
            return false;
        }

        found->entries.erase(position);
        return true;
    }

    bool ConfigService::RemoveSection(std::wstring_view section)
    {
        const auto position = std::find_if(m_sections.begin(), m_sections.end(), [section](const Section& candidate) {
            return strings::EqualsIgnoreCase(candidate.name, section);
        });
        if (position == m_sections.end())
        {
            return false;
        }

        m_sections.erase(position);
        return true;
    }

    std::vector<std::wstring> ConfigService::Sections() const
    {
        std::vector<std::wstring> names;
        names.reserve(m_sections.size());
        for (const auto& section : m_sections)
        {
            names.push_back(section.name);
        }
        return names;
    }

    std::vector<std::wstring> ConfigService::Keys(std::wstring_view section) const
    {
        std::vector<std::wstring> keys;
        const auto* found = FindSection(section);
        if (found == nullptr)
        {
            return keys;
        }

        keys.reserve(found->entries.size());
        for (const auto& entry : found->entries)
        {
            keys.push_back(entry.key);
        }
        return keys;
    }
}
