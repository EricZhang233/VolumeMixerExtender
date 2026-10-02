#include "CliTypes.h"

#include "Strings.h"

namespace vmex::cli
{
    Code ToExitCode(Outcome outcome)
    {
        switch (outcome)
        {
        case Outcome::Success: return kCodeOk;
        case Outcome::InvalidArguments: return kCodeInvalidArguments;
        case Outcome::NotSupported: return kCodeNotSupported;
        default: return kCodeFailed;
        }
    }

    std::wstring_view ToString(Outcome outcome)
    {
        switch (outcome)
        {
        case Outcome::Success: return L"success";
        case Outcome::InvalidArguments: return L"invalid-arguments";
        case Outcome::NotSupported: return L"not-supported";
        default: return L"failed";
        }
    }

    bool Invocation::HasFlag(std::wstring_view name) const
    {
        return std::any_of(flags.begin(), flags.end(), [name](const std::wstring& flag) {
            return strings::EqualsIgnoreCase(flag, name);
        });
    }

    bool Invocation::HasOption(std::wstring_view name) const
    {
        return std::any_of(options.begin(), options.end(), [name](const auto& entry) {
            return strings::EqualsIgnoreCase(entry.first, name);
        });
    }

    std::wstring Invocation::Option(std::wstring_view name, std::wstring_view fallback) const
    {
        const auto position = std::find_if(options.begin(), options.end(), [name](const auto& entry) {
            return strings::EqualsIgnoreCase(entry.first, name);
        });
        return position == options.end() ? std::wstring(fallback) : position->second;
    }

    const std::wstring* Invocation::Positional(std::size_t index) const
    {
        return index < positionals.size() ? &positionals[index] : nullptr;
    }
}
