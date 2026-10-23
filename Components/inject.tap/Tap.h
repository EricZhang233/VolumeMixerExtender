#pragma once

#include "Foundation.h"

#include <windows.h>

namespace vmex::tap
{
    void AttachLoggingOnce();

    void ProvideInitData(const wchar_t* config);
}
