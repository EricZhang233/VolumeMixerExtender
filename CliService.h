#pragma once

#include "CliTypes.h"
#include "CommandRegistry.h"

namespace vmex::cli
{
    void WriteToConsole(std::wstring_view text, bool toError);

    class ConsoleOutput final : public ICliOutput
    {
    public:
        explicit ConsoleOutput(bool colorEnabled);

        void Heading(std::wstring_view text) override;
        void Line(std::wstring_view text) override;
        void Field(std::wstring_view label, std::wstring_view value) override;
        void Blank() override;
        void Warn(std::wstring_view text) override;
        void Error(std::wstring_view text) override;
        void Success(std::wstring_view text) override;

    private:
        void Write(std::wstring_view text, std::uint16_t attributes) const;
        void WriteLine(std::wstring_view text, std::uint16_t attributes) const;

        bool m_color = false;
    };

    struct ParseOutcome final
    {
        bool accepted = false;
        Invocation invocation;
        std::wstring errorMessage;
    };

    class CliService final
    {
    public:
        CliService(CommandRegistry& commands, const ITextResolver& text);

        [[nodiscard]] int Run(const std::vector<std::wstring>& arguments) const;

    private:
        [[nodiscard]] ParseOutcome Parse(const std::vector<std::wstring>& arguments) const;

        CommandRegistry& m_commands;
        const ITextResolver& m_text;
    };
}
