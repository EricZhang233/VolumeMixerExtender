#pragma once

#include "Foundation.h"

#include <map>

namespace vmex::cli
{
    enum class Outcome
    {
        Success = 0,
        InvalidArguments = 1,
        NotSupported = 2,
        Failed = 3
    };

    [[nodiscard]] Code ToExitCode(Outcome outcome);
    [[nodiscard]] std::wstring_view ToString(Outcome outcome);

    class ICliOutput
    {
    public:
        virtual ~ICliOutput() = default;

        virtual void Heading(std::wstring_view text) = 0;
        virtual void Line(std::wstring_view text) = 0;
        virtual void Field(std::wstring_view label, std::wstring_view value) = 0;
        virtual void Blank() = 0;
        virtual void Warn(std::wstring_view text) = 0;
        virtual void Error(std::wstring_view text) = 0;
        virtual void Success(std::wstring_view text) = 0;
    };

    struct ArgumentSpec final
    {
        std::wstring name;
        std::wstring summaryKey;
        bool required = false;
    };

    struct OptionSpec final
    {
        std::wstring name;
        std::wstring valueName;
        std::wstring summaryKey;
        bool requiresValue = false;
        bool required = false;
    };

    struct ExampleSpec final
    {
        std::wstring commandLine;
        std::wstring summaryKey;
    };

    struct Invocation final
    {
        std::wstring command;
        std::vector<std::wstring> positionals;
        std::map<std::wstring, std::wstring> options;
        std::vector<std::wstring> flags;

        [[nodiscard]] bool HasFlag(std::wstring_view name) const;
        [[nodiscard]] bool HasOption(std::wstring_view name) const;
        [[nodiscard]] std::wstring Option(std::wstring_view name, std::wstring_view fallback) const;
        [[nodiscard]] const std::wstring* Positional(std::size_t index) const;
    };

    struct Result final
    {
        Outcome outcome = Outcome::Success;
        std::wstring message;
    };

    using Handler = std::function<Result(const Invocation&, ICliOutput&)>;

    struct Command final
    {
        std::wstring name;
        std::wstring groupKey;
        std::wstring summaryKey;
        std::vector<ArgumentSpec> arguments;
        std::vector<OptionSpec> options;
        std::vector<ExampleSpec> examples;
        Handler handler;
        bool hidden = false;
    };
}
