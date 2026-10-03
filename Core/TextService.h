#pragma once

#include "Foundation.h"

namespace vmex::text
{
    class ITextSource
    {
    public:
        virtual ~ITextSource() = default;

        [[nodiscard]] virtual std::wstring_view Name() const = 0;
        [[nodiscard]] virtual bool TryGet(std::wstring_view key, std::wstring& value) const = 0;
        [[nodiscard]] virtual std::vector<std::wstring> Keys() const = 0;
        [[nodiscard]] virtual std::size_t Count() const noexcept = 0;
    };

    class TextService final : public ITextResolver
    {
    public:
        Status Attach(const std::shared_ptr<ITextSource>& source);

        [[nodiscard]] std::size_t Count() const noexcept;
        [[nodiscard]] bool Contains(std::wstring_view key) const;
        [[nodiscard]] std::wstring GetOr(std::wstring_view key, std::wstring_view fallback) const;
        [[nodiscard]] std::vector<std::wstring> Keys() const;
        [[nodiscard]] std::vector<std::wstring> MissingKeys() const;
        void ClearMissingKeys();

        [[nodiscard]] std::wstring Resolve(std::wstring_view key) const override;
        [[nodiscard]] std::wstring ResolveFormat(std::wstring_view key, const std::vector<std::wstring>& arguments) const override;

    private:
        void NoteMissing(std::wstring_view key) const;

        std::shared_ptr<ITextSource> m_source;
        mutable std::vector<std::wstring> m_missing;
    };

    [[nodiscard]] std::shared_ptr<ITextSource> CreateEmbeddedTextSource();
    [[nodiscard]] std::shared_ptr<ITextSource> CreateYamlTextSource(std::wstring_view yaml, std::wstring_view name);
    [[nodiscard]] std::shared_ptr<ITextSource> CreateFileTextSource(const std::filesystem::path& file);

    [[nodiscard]] TextService& Embedded();
    void AttachEmbeddedToLogger();
}
