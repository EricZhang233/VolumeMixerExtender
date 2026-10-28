#include "AudioMirror.h"

#include "AudioFrameQueue.h"
#include "AudioInterop.h"
#include "Logger.h"
#include <future>
#include <stdexcept>

namespace vmex::recording
{
    namespace
    {
        void Check(HRESULT result)
        {
            if (FAILED(result)) throw result;
        }

        struct Apartment final
        {
            HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~Apartment() { if (SUCCEEDED(result)) ::CoUninitialize(); }
        };

        struct Format final
        {
            WAVEFORMATEX* value = nullptr;
            ~Format() { ::CoTaskMemFree(value); }
        };

        struct Stream final
        {
            audio::detail::Ptr<IAudioClient> client;
            bool started = false;
            ~Stream() { if (started) client->Stop(); }
            void Start() { Check(client->Start()); started = true; }
        };
    }

    AudioMirror::~AudioMirror() { Stop(); }

    Status AudioMirror::Start(std::wstring_view sourceDevice, std::wstring_view targetDevice)
    {
        if (sourceDevice.empty() || targetDevice.empty() || sourceDevice == targetDevice)
            return Status::InvalidArguments(L"mirror-endpoints");
        Stop();
        m_stop = false;
        std::promise<Status> ready;
        auto started = ready.get_future();
        m_thread = std::thread([this, source = std::wstring(sourceDevice), target = std::wstring(targetDevice), ready = std::move(ready)]() mutable
        {
            bool announced = false;
            Status result;
            try
            {
                Apartment apartment;
                Check(apartment.result);
                auto enumerator = audio::detail::MakeEnumerator();
                if (!enumerator) throw E_NOINTERFACE;
                auto input = audio::detail::FindDevice(enumerator.get(), source);
                auto output = audio::detail::FindDevice(enumerator.get(), target);
                if (!input || !output) throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                Stream capture, render;
                Check(input->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, capture.client.putv()));
                Check(output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, render.client.putv()));
                Format format;
                Check(capture.client->GetMixFormat(&format.value));
                if (!format.value || !format.value->nBlockAlign || !format.value->nSamplesPerSec) throw E_INVALIDARG;
                Check(capture.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 300000, 0, format.value, nullptr));
                Check(render.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_RATEADJUST,
                    300000, 0, format.value, nullptr));
                audio::detail::Ptr<IAudioCaptureClient> reader;
                audio::detail::Ptr<IAudioRenderClient> writer;
                audio::detail::Ptr<IAudioClockAdjustment> clock;
                Check(capture.client->GetService(__uuidof(IAudioCaptureClient), reader.putv()));
                Check(render.client->GetService(__uuidof(IAudioRenderClient), writer.putv()));
                Check(render.client->GetService(__uuidof(IAudioClockAdjustment), clock.putv()));
                UINT32 capacity = 0;
                Check(render.client->GetBufferSize(&capacity));
                const auto rate = format.value->nSamplesPerSec;
                AudioFrameQueue queue(rate / 4, format.value->nBlockAlign);
                queue.Push(nullptr, rate * 30 / 1000);
                capture.Start();
                render.Start();
                {
                    std::lock_guard lock(m_mutex);
                    m_result = Status::Ok();
                }
                ready.set_value(Status::Ok());
                announced = true;
                unsigned ticks = 0;
                std::size_t dropped = 0;
                while (!m_stop.load())
                {
                    UINT32 packet = 0;
                    Check(reader->GetNextPacketSize(&packet));
                    while (packet)
                    {
                        BYTE* bytes = nullptr;
                        UINT32 frames = 0;
                        DWORD flags = 0;
                        Check(reader->GetBuffer(&bytes, &frames, &flags, nullptr, nullptr));
                        dropped += queue.Push((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : bytes, frames);
                        Check(reader->ReleaseBuffer(frames));
                        Check(reader->GetNextPacketSize(&packet));
                    }
                    UINT32 padding = 0;
                    Check(render.client->GetCurrentPadding(&padding));
                    const auto available = capacity > padding ? capacity - padding : 0;
                    if (available)
                    {
                        BYTE* bytes = nullptr;
                        Check(writer->GetBuffer(available, &bytes));
                        queue.Pop(bytes, available);
                        Check(writer->ReleaseBuffer(available, 0));
                    }
                    if (++ticks % 100 == 0)
                    {
                        const double targetFrames = rate * 30.0 / 1000;
                        const double error = (static_cast<double>(queue.Size()) - targetFrames) / targetFrames;
                        const double correction = std::clamp(error * 0.001, -0.005, 0.005);
                        Check(clock->SetSampleRate(static_cast<float>(rate * (1.0 + correction))));
                    }
                    ::Sleep(3);
                }
                if (dropped)
                    log::Logger::Instance().WriteKeyFormat(log::Level::Warn, L"recording", L"log.recording.dropped", { std::to_wstring(dropped) });
            }
            catch (HRESULT error) { result = Status::FromHResult(L"audio-mirror", error); }
            catch (...) { result = Status::Failed(L"audio-mirror-exception"); }
            {
                std::lock_guard lock(m_mutex);
                m_result = result;
            }
            if (!announced) ready.set_value(result);
            if (!result)
                log::Logger::Instance().WriteKeyFormat(log::Level::Error, L"recording", L"log.recording.result",
                    { L"mirror", std::to_wstring(result.code), result.detail });
        });
        return started.get();
    }

    void AudioMirror::Stop() noexcept
    {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
    }

    Status AudioMirror::State() const
    {
        std::lock_guard lock(m_mutex);
        return m_result;
    }
}
