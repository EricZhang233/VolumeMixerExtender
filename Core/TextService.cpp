#include "TextService.h"

#include "Logger.h"
#include "Platform.h"
#include "Strings.h"
#include "TextEncoding.h"
#include "TextResources.g.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace vmex::text
{
    namespace
    {
        class MapTextSource final : public ITextSource
        {
        public:
            MapTextSource(std::wstring name, std::map<std::wstring, std::wstring> entries)
                : m_name(std::move(name))
                , m_entries(std::move(entries))
            {
            }

            [[nodiscard]] std::wstring_view Name() const override { return m_name; }

            [[nodiscard]] bool TryGet(std::wstring_view key, std::wstring& value) const override
            {
                const auto position = m_entries.find(std::wstring(key));
                if (position == m_entries.end())
                {
                    return false;
                }
                value = position->second;
                return true;
            }

            [[nodiscard]] std::vector<std::wstring> Keys() const override
            {
                std::vector<std::wstring> keys;
                keys.reserve(m_entries.size());
                for (const auto& entry : m_entries)
                {
                    keys.push_back(entry.first);
                }
                return keys;
            }

            [[nodiscard]] std::size_t Count() const noexcept override { return m_entries.size(); }

        private:
            std::wstring m_name;
            std::map<std::wstring, std::wstring> m_entries;
        };

        void UnescapeValue(std::wstring& value)
        {
            value = strings::Replace(value, L"\\n", L"\n");
            value = strings::Replace(value, L"\\t", L"\t");
            value = strings::Replace(value, L"\\\\", L"\\");
        }

        std::map<std::wstring, std::wstring> ParseFlatYaml(std::wstring_view yaml)
        {
            std::map<std::wstring, std::wstring> entries;

            std::size_t start = 0;
            while (start <= yaml.size())
            {
                auto end = yaml.find(L'\n', start);
                if (end == std::wstring_view::npos)
                {
                    end = yaml.size();
                }

                auto line = yaml.substr(start, end - start);
                start = end + 1;

                if (!line.empty() && line.back() == L'\r')
                {
                    line.remove_suffix(1);
                }

                const auto trimmed = strings::TrimView(line);
                if (trimmed.empty() || trimmed.front() == L'#')
                {
                    if (end >= yaml.size())
                    {
                        break;
                    }
                    continue;
                }

                const auto separator = trimmed.find(L':');
                if (separator == std::wstring_view::npos)
                {
                    if (end >= yaml.size())
                    {
                        break;
                    }
                    continue;
                }

                const auto key = strings::TrimView(trimmed.substr(0, separator));
                auto value = strings::TrimView(trimmed.substr(separator + 1));
                if (key.empty())
                {
                    if (end >= yaml.size())
                    {
                        break;
                    }
                    continue;
                }

                std::wstring stored(value);
                if (stored.size() >= 2)
                {
                    const auto first = stored.front();
                    const auto last = stored.back();
                    if ((first == L'"' && last == L'"') || (first == L'\'' && last == L'\''))
                    {
                        stored = stored.substr(1, stored.size() - 2);
                    }
                }
                UnescapeValue(stored);
                entries[std::wstring(key)] = std::move(stored);

                if (end >= yaml.size())
                {
                    break;
                }
            }

            return entries;
        }
    }

    Status TextService::Attach(const std::shared_ptr<ITextSource>& source)
    {
        if (source == nullptr)
        {
            return Status::InvalidArguments(L"TextService::Attach");
        }
        m_source = source;
        return Status::Ok();
    }

    std::size_t TextService::Count() const noexcept
    {
        return m_source == nullptr ? 0 : m_source->Count();
    }

    bool TextService::Contains(std::wstring_view key) const
    {
        std::wstring ignored;
        return m_source != nullptr && m_source->TryGet(key, ignored);
    }

    std::wstring TextService::GetOr(std::wstring_view key, std::wstring_view fallback) const
    {
        std::wstring value;
        if (m_source != nullptr && m_source->TryGet(key, value))
        {
            return value;
        }
        return std::wstring(fallback);
    }

    std::vector<std::wstring> TextService::Keys() const
    {
        return m_source == nullptr ? std::vector<std::wstring>{} : m_source->Keys();
    }

    std::vector<std::wstring> TextService::MissingKeys() const
    {
        return m_missing;
    }

    void TextService::ClearMissingKeys()
    {
        m_missing.clear();
    }

    void TextService::NoteMissing(std::wstring_view key) const
    {
        const std::wstring owned(key);
        if (std::find(m_missing.begin(), m_missing.end(), owned) == m_missing.end())
        {
            m_missing.push_back(owned);
        }
    }

    std::wstring TextService::Resolve(std::wstring_view key) const
    {
        std::wstring value;
        if (m_source != nullptr && m_source->TryGet(key, value))
        {
            return value;
        }

        NoteMissing(key);
        std::wstring marker(L"[");
        marker.append(key);
        marker.append(L"]");
        return marker;
    }

    std::wstring TextService::ResolveFormat(std::wstring_view key, const std::vector<std::wstring>& arguments) const
    {
        const auto pattern = Resolve(key);
        return strings::Format(pattern, arguments);
    }

    std::shared_ptr<ITextSource> CreateEmbeddedTextSource()
    {
        return CreateYamlTextSource(generated::kTextYaml, L"embedded");
    }

    std::shared_ptr<ITextSource> CreateYamlTextSource(std::wstring_view yaml, std::wstring_view name)
    {
        return std::make_shared<MapTextSource>(std::wstring(name), ParseFlatYaml(yaml));
    }

    std::shared_ptr<ITextSource> CreateFileTextSource(const std::filesystem::path& file)
    {
        std::vector<std::byte> bytes;
        if (!platform::ReadAllBytes(file, bytes).IsOk() || bytes.empty())
        {
            return nullptr;
        }

        encoding::Decoded decoded;
        if (!encoding::Decode(bytes, decoded).IsOk())
        {
            return nullptr;
        }

        return CreateYamlTextSource(decoded.text, file.filename().wstring());
    }

    TextService& Embedded()
    {
        static TextService service = []()
        {
            TextService instance;
            instance.Attach(CreateEmbeddedTextSource());
            return instance;
        }();
        return service;
    }

    void AttachEmbeddedToLogger()
    {
        static std::once_flag once;
        std::call_once(once, []()
        {
            log::Logger::Instance().SetResolver(&Embedded());
        });
    }
}
