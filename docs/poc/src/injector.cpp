// injector.cpp -- minimal DLL injector (CreateRemoteThread + LoadLibraryW).
//
// Usage:
//   injector.exe --dll <abs path to dll> [--process ShellHost.exe] [--pid N]
//
// Why this works here: ShellHost.exe runs at the same integrity level as us (Medium),
// is not a protected process, and OpenProcess(PROCESS_ALL_ACCESS) is granted.
// (measured -- see ../logs/injection-feasibility.txt)

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string>

static DWORD FindPidByName(const wchar_t* name)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring dllPath;
    std::wstring procName = L"ShellHost.exe";
    DWORD pid = 0;

    for (int i = 1; i < argc; ++i) {
        if (!_wcsicmp(argv[i], L"--dll") && i + 1 < argc)          dllPath  = argv[++i];
        else if (!_wcsicmp(argv[i], L"--process") && i + 1 < argc) procName = argv[++i];
        else if (!_wcsicmp(argv[i], L"--pid") && i + 1 < argc)     pid      = wcstoul(argv[++i], nullptr, 10);
    }

    if (dllPath.empty()) {
        wprintf(L"usage: injector.exe --dll <path> [--process <name>] [--pid <n>]\n");
        return 2;
    }

    wchar_t full[MAX_PATH] = {};
    if (!GetFullPathNameW(dllPath.c_str(), MAX_PATH, full, nullptr)) {
        wprintf(L"[!] bad dll path\n");
        return 2;
    }
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"[!] dll not found: %s\n", full);
        return 2;
    }

    if (!pid) pid = FindPidByName(procName.c_str());
    if (!pid) {
        wprintf(L"[!] process not found: %s\n", procName.c_str());
        return 3;
    }
    wprintf(L"[*] target : %s (pid %lu)\n", procName.c_str(), pid);
    wprintf(L"[*] dll    : %s\n", full);

    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) {
        wprintf(L"[!] OpenProcess failed, err=%lu\n", GetLastError());
        return 4;
    }

    const SIZE_T bytes = (wcslen(full) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hp, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        wprintf(L"[!] VirtualAllocEx failed, err=%lu\n", GetLastError());
        CloseHandle(hp);
        return 5;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hp, remote, full, bytes, &written)) {
        wprintf(L"[!] WriteProcessMemory failed, err=%lu\n", GetLastError());
        CloseHandle(hp);
        return 6;
    }

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLib) {
        wprintf(L"[!] LoadLibraryW not found\n");
        CloseHandle(hp);
        return 7;
    }

    HANDLE th = CreateRemoteThread(hp, nullptr, 0, (LPTHREAD_START_ROUTINE)loadLib, remote, 0, nullptr);
    if (!th) {
        wprintf(L"[!] CreateRemoteThread failed, err=%lu\n", GetLastError());
        CloseHandle(hp);
        return 8;
    }

    DWORD wr = WaitForSingleObject(th, 20000);
    if (wr != WAIT_OBJECT_0) {
        wprintf(L"[!] remote thread did not finish (wait=0x%lX)\n", wr);
        CloseHandle(th);
        CloseHandle(hp);
        return 9;
    }

    DWORD ec = 0;
    GetExitCodeThread(th, &ec);
    if (ec) {
        wprintf(L"[+] injected OK, remote HMODULE = 0x%p\n", (void*)(ULONG_PTR)ec);
    } else {
        wprintf(L"[!] remote LoadLibraryW FAILED (returned NULL)\n");
    }

    VirtualFreeEx(hp, remote, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(hp);
    return ec ? 0 : 10;
}
