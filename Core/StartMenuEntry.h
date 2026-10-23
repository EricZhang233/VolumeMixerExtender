#pragma once

#include <string>

namespace vmex::startmenu
{
    class StartMenuEntry final
    {
    public:
        static bool Register(std::wstring& error);
    };
}
