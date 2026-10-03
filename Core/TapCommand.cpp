#include "TapCommand.h"

#include "Strings.h"

namespace vmex::inject
{
    namespace
    {
        std::optional<TapVerb> ParseVerb(std::string_view name)
        {
            if (name == "CLICK") return TapVerb::Click;
            if (name == "SETDEFAULT") return TapVerb::SetDefault;
            if (name == "SETREDIRECT") return TapVerb::SetRedirect;
            if (name == "CLEARREDIRECT") return TapVerb::ClearRedirect;
            if (name == "UNINSTALL") return TapVerb::Uninstall;
            return std::nullopt;
        }

        bool IsSeparator(char value)
        {
            return value == ' ' || value == '\t';
        }

        bool IsArgumentCountValid(TapVerb verb, std::size_t count)
        {
            switch (verb)
            {
            case TapVerb::SetDefault: return count == 2;
            case TapVerb::SetRedirect: return count == 2 || count == 3;
            case TapVerb::ClearRedirect: return count == 0;
            case TapVerb::Uninstall: return count == 0;
            default: return true;
            }
        }
    }

    std::wstring_view ToString(TapVerb verb)
    {
        switch (verb)
        {
        case TapVerb::Click: return L"CLICK";
        case TapVerb::SetDefault: return L"SETDEFAULT";
        case TapVerb::SetRedirect: return L"SETREDIRECT";
        case TapVerb::ClearRedirect: return L"CLEARREDIRECT";
        default: return L"UNINSTALL";
        }
    }

    std::optional<TapCommand> ParseTapCommand(std::string_view line)
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        {
            line.remove_suffix(1);
        }

        std::size_t cursor = 0;
        const auto takeToken = [&line, &cursor](std::string_view& token)
        {
            while (cursor < line.size() && IsSeparator(line[cursor]))
            {
                ++cursor;
            }
            if (cursor >= line.size())
            {
                return false;
            }

            const std::size_t start = cursor;
            while (cursor < line.size() && !IsSeparator(line[cursor]))
            {
                ++cursor;
            }

            token = line.substr(start, cursor - start);
            return true;
        };

        std::string_view verbToken;
        if (!takeToken(verbToken))
        {
            return std::nullopt;
        }

        const auto verb = ParseVerb(verbToken);
        if (!verb.has_value())
        {
            return std::nullopt;
        }

        TapCommand command;
        command.verb = *verb;
        command.raw = strings::ToWide(line);

        std::string_view token;
        while (takeToken(token))
        {
            command.arguments.push_back(strings::ToWide(token));
        }

        command.argumentsValid = IsArgumentCountValid(command.verb, command.arguments.size());
        return command;
    }
}
