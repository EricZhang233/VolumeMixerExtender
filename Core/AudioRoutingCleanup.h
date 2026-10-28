#pragma once

#include "Foundation.h"
#include <windows.h>

namespace vmex::recording
{
    Status ClearEndpointPreferences(HKEY root, std::wstring_view endpoint);
    Status ClearVirtualRedirects(std::wstring_view endpoint);
}
