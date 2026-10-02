#include "CliService.h"

#include "Strings.h"

#include <windows.h>

namespace vmex::cli
{
    namespace
    {
        constexpr std::uint16_t kColorDefault = 0x07;
        constexpr std::uint16_t kColorHeading = 0x0B;
        constexpr std::uint16_t kColorWarn = 0x0E;
        constexpr std::uint16_t kColorError = 0x0C;
        constexpr std::uint16_t kColorSuccess = 0x0A;

        bool ConsoleSupportsColor()
        {
            const auto handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
            if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
            {
                return false;
            }

            DWORD mode = 0;
            return ::GetConsoleMode(handle, &mode) != FALSE;
        }

        std::wstring ToKey(std::wstring_view name)
        {
            std::wstring key(L"cli.error.");
            key.append(name);
            return key;
        }
    }

    void WriteToConsole(std::wstring_view text, bool toError)
    {
        const auto handle = ::GetStdHandle(toError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD mode = 0;
        if (::GetConsoleMode(handle, &mode) != FALSE)
        {
            DWORD written = 0;
            ::WriteConsoleW(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            return;
        }

        const auto utf8 = strings::ToNarrow(text);
        if (utf8.empty())
        {
            return;
        }

        DWORD written = 0;
        ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

    ConsoleOutput::ConsoleOutput(bool colorEnabled)
        : m_color(colorEnabled)
    {
    }

    void ConsoleOutput::Write(std::wstring_view text, std::uint16_t attributes) const
    {
        const auto handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
        {
            return;
        }

        DWORD mode = 0;
        const bool isConsole = ::GetConsoleMode(handle, &mode) != FALSE;

        if (!isConsole || !m_color)
        {
            WriteToConsole(text, false);
            return;
        }

        CONSOLE_SCREEN_BUFFER_INFO info{};
        std::uint16_t previous = kColorDefault;
        if (::GetConsoleScreenBufferInfo(handle, &info) != FALSE)
        {
            previous = info.wAttributes;
        }

        ::SetConsoleTextAttribute(handle, attributes);
        WriteToConsole(text, false);
        ::SetConsoleTextAttribute(handle, previous);
    }

    void ConsoleOutput::WriteLine(std::wstring_view text, std::uint16_t attributes) const
    {
        Write(text, attributes);
        Write(L"\r\n", kColorDefault);
    }

    void ConsoleOutput::Heading(std::wstring_view text)
    {
        WriteLine(text, kColorHeading);
    }

    void ConsoleOutput::Line(std::wstring_view text)
    {
        WriteLine(text, kColorDefault);
    }

    void ConsoleOutput::Field(std::wstring_view label, std::wstring_view value)
    {
        std::wstring line(L"  ");
        line.append(label);
        line.append(L": ");
        line.append(value);
        WriteLine(line, kColorDefault);
    }

    void ConsoleOutput::Blank()
    {
        WriteLine(L"", kColorDefault);
    }

    void ConsoleOutput::Warn(std::wstring_view text)
    {
        WriteLine(text, kColorWarn);
    }

    void ConsoleOutput::Error(std::wstring_view text)
    {
        WriteLine(text, kColorError);
    }

    void ConsoleOutput::Success(std::wstring_view text)
    {
        WriteLine(text, kColorSuccess);
    }

    CliService::CliService(CommandRegistry& commands, const ITextResolver& text)
        : m_commands(commands)
        , m_text(text)
    {
    }

    ParseOutcome CliService::Parse(const std::vector<std::wstring>& arguments) const
    {
        ParseOutcome outcome;

        if (arguments.empty())
        {
            outcome.errorMessage = m_text.Resolve(L"cli.usage");
            return outcome;
        }

        outcome.invocation.command = arguments.front();
        const auto* command = m_commands.Find(outcome.invocation.command);
        if (command == nullptr)
        {
            outcome.errorMessage = m_text.ResolveFormat(L"cli.error.unknown_command", { outcome.invocation.command });
            return outcome;
        }

        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            const auto& token = arguments[index];
            if (token.size() >= 2 && token[0] == L'-' && token[1] == L'-')
            {
                const auto body = std::wstring_view(token).substr(2);
                const auto separator = body.find(L'=');

                const auto name = separator == std::wstring_view::npos ? body : body.substr(0, separator);
                if (name.empty())
                {
                    outcome.errorMessage = m_text.ResolveFormat(L"cli.error.unknown_option", { token });
                    return outcome;
                }

                const auto spec = std::find_if(command->options.begin(), command->options.end(), [name](const OptionSpec& option) {
                    return strings::EqualsIgnoreCase(option.name, name);
                });

                if (spec == command->options.end())
                {
                    outcome.errorMessage = m_text.ResolveFormat(L"cli.error.unknown_option", { std::wstring(name) });
                    return outcome;
                }

                if (!spec->requiresValue)
                {
                    outcome.invocation.flags.push_back(std::wstring(name));
                    continue;
                }

                if (separator != std::wstring_view::npos)
                {
                    outcome.invocation.options[std::wstring(name)] = std::wstring(body.substr(separator + 1));
                    continue;
                }

                if (index + 1 >= arguments.size())
                {
                    outcome.errorMessage = m_text.ResolveFormat(L"cli.error.missing_value", { std::wstring(name) });
                    return outcome;
                }

                outcome.invocation.options[std::wstring(name)] = arguments[++index];
                continue;
            }

            outcome.invocation.positionals.push_back(token);
        }

        for (const auto& option : command->options)
        {
            if (option.required && !outcome.invocation.HasOption(option.name))
            {
                outcome.errorMessage = m_text.ResolveFormat(L"cli.error.missing_required", { option.name });
                return outcome;
            }
        }

        for (std::size_t index = 0; index < command->arguments.size(); ++index)
        {
            if (command->arguments[index].required && outcome.invocation.Positional(index) == nullptr)
            {
                outcome.errorMessage = m_text.ResolveFormat(L"cli.error.missing_required", { command->arguments[index].name });
                return outcome;
            }
        }

        outcome.accepted = true;
        return outcome;
    }

    int CliService::Run(const std::vector<std::wstring>& arguments) const
    {
        const auto parsed = Parse(arguments);
        ConsoleOutput output(ConsoleSupportsColor());

        if (!parsed.accepted)
        {
            if (!parsed.errorMessage.empty())
            {
                output.Error(parsed.errorMessage);
            }
            return ToExitCode(Outcome::InvalidArguments);
        }

        const auto* command = m_commands.Find(parsed.invocation.command);
        if (command == nullptr || !command->handler)
        {
            output.Error(m_text.ResolveFormat(L"cli.error.unknown_command", { parsed.invocation.command }));
            return ToExitCode(Outcome::InvalidArguments);
        }

        const auto result = command->handler(parsed.invocation, output);
        switch (result.outcome)
        {
        case Outcome::Success:
            output.Success(m_text.Resolve(L"cli.outcome.success"));
            break;
        case Outcome::NotSupported:
            output.Warn(m_text.Resolve(L"cli.outcome.not_supported"));
            break;
        case Outcome::Failed:
            output.Error(result.message.empty() ? m_text.Resolve(L"cli.outcome.failed") : result.message);
            break;
        default:
            if (!result.message.empty())
            {
                output.Error(result.message);
            }
            break;
        }

        return ToExitCode(result.outcome);
    }
}
