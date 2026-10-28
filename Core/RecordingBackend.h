#pragma once

#include "RecordingMode.h"
#include <memory>

namespace vmex::recording
{
    [[nodiscard]] std::unique_ptr<IBackend> CreateWindowsBackend();
}
