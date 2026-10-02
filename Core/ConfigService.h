#pragma once

#include "Foundation.h"
#include "TextEncoding.h"

namespace vmex::config
{
    struct Entry final
    {
        std::wstring key;
        std::wstring value;
    };

    struct Section final
    {
        std::wstring name;
        std::vector<Entry> entries;
    };

    class ConfigService final
    {
    public:
        Status Load(const std::filesystem::path& file);
        Status Save();
        Status SaveAs(const std::filesystem::path& file);

        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] const std::filesystem::path& FilePath() const noexcept;
        [[nodiscard]] encoding::Encoding SourceEncoding() const noexcept;
        [[nodiscard]] bool SourceEncodingIsCanonical() const noexcept;

        [[nodiscard]] bool Has(std::wstring_view section, std::wstring_view key) const;
        [[nodiscard]] std::wstring GetString(std::wstring_view section, std::wstring_view key, std::wstring_view fallback) const;
        [[nodiscard]] std::int64_t GetInt(std::wstring_view section, std::wstring_view key, std::int64_t fallback) const;
        [[nodiscard]] double GetDouble(std::wstring_view section, std::wstring_view key, double fallback) const;
        [[nodiscard]] bool GetBool(std::wstring_view section, std::wstring_view key, bool fallback) const;

        void SetString(std::wstring_view section, std::wstring_view key, std::wstring_view value);
        void SetInt(std::wstring_view section, std::wstring_view key, std::int64_t value);
        void SetBool(std::wstring_view section, std::wstring_view key, bool value);

        bool Remove(std::wstring_view section, std::wstring_view key);
        bool RemoveSection(std::wstring_view section);

        [[nodiscard]] std::vector<std::wstring> Sections() const;
        [[nodiscard]] std::vector<std::wstring> Keys(std::wstring_view section) const;

    private:
        [[nodiscard]] Section* FindSection(std::wstring_view name);
        [[nodiscard]] const Section* FindSection(std::wstring_view name) const;
        [[nodiscard]] Section& EnsureSection(std::wstring_view name);

        std::filesystem::path m_file;
        std::vector<Section> m_sections;
        encoding::Encoding m_sourceEncoding = encoding::Encoding::Unknown;
        bool m_loaded = false;
    };
}
