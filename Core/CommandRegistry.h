#pragma once

#include "CliTypes.h"

namespace vmex::cli
{
    class CommandRegistry final
    {
    public:
        void Add(Command command);

        [[nodiscard]] const Command* Find(std::wstring_view name) const;
        [[nodiscard]] const std::vector<Command>& Commands() const noexcept;
        [[nodiscard]] std::size_t Count() const noexcept;

        [[nodiscard]] std::vector<std::wstring> RenderGeneralHelp(const ITextResolver& text) const;
        [[nodiscard]] std::vector<std::wstring> RenderCommandHelp(const Command& command, const ITextResolver& text) const;
        [[nodiscard]] std::vector<std::wstring> RenderSkill(const ITextResolver& text) const;

    private:
        std::vector<Command> m_commands;
    };
}
