#pragma once

#include "RecordingMode.h"

namespace vmex::recording
{
    Status Request(std::wstring_view action, std::wstring_view device, Snapshot& state);
}
