#include "CommandRegistry.h"

#include "Strings.h"

#include <algorithm>

namespace vmex::cli
{
    namespace
    {
        void AppendSection(std::vector<std::wstring>& lines, const ITextResolver& text, std::wstring_view sectionKey)
        {
            lines.push_back(L"");
            lines.push_back(L"  " + text.Resolve(sectionKey));
        }
    }

    void CommandRegistry::Add(Command command)
    {
        m_commands.push_back(std::move(command));
    }

    const Command* CommandRegistry::Find(std::wstring_view name) const
    {
        const auto position = std::find_if(m_commands.begin(), m_commands.end(), [name](const Command& command) {
            return strings::EqualsIgnoreCase(command.name, name);
        });
        return position == m_commands.end() ? nullptr : &(*position);
    }

    const std::vector<Command>& CommandRegistry::Commands() const noexcept
    {
        return m_commands;
    }

    std::size_t CommandRegistry::Count() const noexcept
    {
        return m_commands.size();
    }

    std::vector<std::wstring> CommandRegistry::RenderGeneralHelp(const ITextResolver& text) const
    {
        std::vector<std::wstring> lines;
        lines.push_back(text.Resolve(L"cli.usage"));

        AppendSection(lines, text, L"cli.section.commands");

        std::vector<std::wstring> groupOrder;
        for (const auto& command : m_commands)
        {
            if (command.hidden)
            {
                continue;
            }
            if (std::find(groupOrder.begin(), groupOrder.end(), command.groupKey) == groupOrder.end())
            {
                groupOrder.push_back(command.groupKey);
            }
        }

        std::size_t width = 0;
        for (const auto& command : m_commands)
        {
            if (!command.hidden)
            {
                width = std::max(width, command.name.size());
            }
        }

        for (const auto& group : groupOrder)
        {
            lines.push_back(L"");
            lines.push_back(L"    [" + text.Resolve(group) + L"]");

            for (const auto& command : m_commands)
            {
                if (command.hidden || command.groupKey != group)
                {
                    continue;
                }

                std::wstring line = L"      ";
                line.append(command.name);
                line.append(width - command.name.size() + 2, L' ');
                line.append(text.Resolve(command.summaryKey));
                lines.push_back(line);
            }
        }

        lines.push_back(L"");
        lines.push_back(L"  " + text.Resolve(L"cli.hint.skill"));
        return lines;
    }

    std::vector<std::wstring> CommandRegistry::RenderCommandHelp(const Command& command, const ITextResolver& text) const
    {
        std::vector<std::wstring> lines;
        lines.push_back(text.Resolve(command.summaryKey));

        if (!command.arguments.empty())
        {
            AppendSection(lines, text, L"cli.section.arguments");
            for (const auto& argument : command.arguments)
            {
                std::wstring line = L"      ";
                line.append(argument.name);
                if (!argument.required)
                {
                    line.append(L"?");
                }
                line.append(L"  ");
                line.append(text.Resolve(argument.summaryKey));
                lines.push_back(line);
            }
        }

        if (!command.options.empty())
        {
            AppendSection(lines, text, L"cli.section.options");
            for (const auto& option : command.options)
            {
                std::wstring line = L"      --";
                line.append(option.name);
                if (option.requiresValue)
                {
                    line.append(L" <");
                    line.append(option.valueName.empty() ? L"value" : option.valueName);
                    line.append(L">");
                }
                line.append(L"  ");
                line.append(text.Resolve(option.summaryKey));
                lines.push_back(line);
            }
        }

        if (!command.examples.empty())
        {
            AppendSection(lines, text, L"cli.section.examples");
            for (const auto& example : command.examples)
            {
                lines.push_back(L"      " + example.commandLine);
                lines.push_back(L"        " + text.Resolve(example.summaryKey));
            }
        }

        return lines;
    }

    std::vector<std::wstring> CommandRegistry::RenderSkill(const ITextResolver& text) const
    {
        std::vector<std::wstring> lines;
        lines.push_back(L"# " + text.Resolve(L"skill.title"));
        lines.push_back(L"");

        lines.push_back(L"## " + text.Resolve(L"skill.section.purpose"));
        lines.push_back(text.Resolve(L"skill.purpose.text"));
        lines.push_back(L"");

        lines.push_back(L"## " + text.Resolve(L"skill.section.workflow"));
        lines.push_back(text.Resolve(L"skill.workflow.text"));
        lines.push_back(L"");

        lines.push_back(L"## " + text.Resolve(L"skill.section.commands"));
        lines.push_back(L"");

        std::vector<std::wstring> groupOrder;
        for (const auto& command : m_commands)
        {
            if (command.hidden)
            {
                continue;
            }
            if (std::find(groupOrder.begin(), groupOrder.end(), command.groupKey) == groupOrder.end())
            {
                groupOrder.push_back(command.groupKey);
            }
        }

        for (const auto& group : groupOrder)
        {
            lines.push_back(L"### " + text.Resolve(group));
            lines.push_back(L"");

            for (const auto& command : m_commands)
            {
                if (command.hidden || command.groupKey != group)
                {
                    continue;
                }

                std::wstring usage = L"vmex " + command.name;
                for (const auto& argument : command.arguments)
                {
                    usage.append(argument.required ? L" <" : L" [");
                    usage.append(argument.name);
                    usage.append(argument.required ? L">" : L"]");
                }

                lines.push_back(L"- `" + usage + L"`");
                lines.push_back(L"  - " + text.Resolve(command.summaryKey));

                for (const auto& option : command.options)
                {
                    std::wstring detail = L"  - `--" + option.name;
                    if (option.requiresValue)
                    {
                        detail.append(L" <");
                        detail.append(option.valueName.empty() ? L"value" : option.valueName);
                        detail.append(L">");
                    }
                    detail.append(L"` ");
                    detail.append(text.Resolve(option.summaryKey));
                    lines.push_back(detail);
                }
            }
            lines.push_back(L"");
        }

        lines.push_back(L"## " + text.Resolve(L"skill.section.exitcodes"));
        lines.push_back(text.Resolve(L"skill.exitcode.text"));
        return lines;
    }
}
