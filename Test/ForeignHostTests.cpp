#include "Platform.h"
#include "ManagementService.h"
#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 1;
    auto expected = std::filesystem::path(argv[1]).make_preferred();
    const auto application = vmex::platform::GetApplicationDirectory();
    const auto process = vmex::platform::GetExecutableDirectory();
    if (_wcsicmp(application.c_str(), expected.c_str()) != 0 || _wcsicmp(process.c_str(), application.c_str()) == 0 ||
        !vmex::management::IsHostImage(expected / L"vmex.exe", application) ||
        vmex::management::IsHostImage(expected / L"vmex.exe", process))
    {
        std::wcerr << L"Foreign-process package identity failed: process=" << process << L" application=" << application << L"\n";
        return 1;
    }
    std::cout << "Foreign executable correctly uses the shared Core package directory\n";
    return 0;
}
