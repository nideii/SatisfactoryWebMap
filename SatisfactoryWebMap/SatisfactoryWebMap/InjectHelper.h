#include <windows.h>

#include "xorstr.h"

static DWORD FindProcessByName(const std::wstring &processName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snap, &pe32)) {
        return 0;
        CloseHandle(snap);
    }

    DWORD pid = 0;
    do {
        if (processName == pe32.szExeFile) {
            pid = pe32.th32ProcessID;
            break;
        }
    } while (Process32Next(snap, &pe32));

    CloseHandle(snap);

    return pid;
}

static HWND FindWindowByPid(DWORD pid)
{
    std::pair<HWND, DWORD> params = { 0, pid };

    BOOL res = EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto &[res, pid] = *(std::pair<HWND, DWORD> *)(lParam);

        DWORD processId;
        if (GetWindowThreadProcessId(hwnd, &processId) && processId == pid) {
            // Stop enumerating
            SetLastError(-1);
            res = hwnd;
            return FALSE;
        }

        return TRUE;
    }, (LPARAM)&params);

    if (!res && GetLastError() == -1 && IsWindow(params.first)) {
        return params.first;
    }

    return 0;
}

static bool LoadDebugPrivilege()
{
    const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(), flags, &token)) {
        return false;
    }

    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        return false;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), nullptr, nullptr)) {
        return false;
    }

    CloseHandle(token);

    return ERROR_NOT_ALL_ASSIGNED != GetLastError();
}

constexpr char to_int(const char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 55;
    } else if (c >= 'a' && c <= 'z') {
        return c - 87;
    } else {
        return c - 48;
    }
}

template<typename T, size_t N>
auto parser_bytecode(const char (&code)[N])
{
    std::vector<T> bytes;

    for (int i = 0; code[i] != '\x00';) {
        const auto &ch = code[i++];
        if (ch == '\n' || ch == ' ') {
            continue;
        }

        bytes.push_back((to_int(ch) << 4) | to_int(code[i++]));
    }

    const int pad = 32 - (bytes.size() % 32);
    for (int i = 0; i < pad; ++i) {
        bytes.push_back('\xCC');
    }

    return bytes;
}

// shell code from CheatEngine
const auto bytecodes = parser_bytecode<char>(R"(
48 83 EC 40
48 B9 8967452301000000
FF15 02000000 EB08 8967452301000000
48 83 C4 40
48 85 C0
75 06
B8 02000000
C3
B8 01000000
C3
)");

__declspec(noinline) decltype(auto) test()
{
    OutputDebugStringA(&bytecodes[0]);
    return 0;
}

const auto test_res = test();

void InjectDll(DWORD pid, const std::wstring &dll)
{
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
    if (!hProcess) {
        throw std::runtime_error("OpenProcess(" + std::to_string(pid) + ") fails. code=" + std::to_string(GetLastError()));
    }

    HMODULE hMod = GetModuleHandle(xorstr(L"kernel32.dll"));

    typedef HANDLE(WINAPI *__VirtualAllocEx)(HANDLE, void *, SIZE_T, DWORD, DWORD);
    __VirtualAllocEx _VirtualAllocEx = (__VirtualAllocEx)GetProcAddress(hMod, xorstr("VirtualAllocEx"));
    LPVOID pRemoteBuf = _VirtualAllocEx(hProcess, nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!pRemoteBuf) {
        throw std::runtime_error("VirtualAllocEx fails. code=" + std::to_string(GetLastError()));
    }

    size_t bufsize = dll.size() * sizeof(wchar_t);

    typedef HANDLE(WINAPI *__WriteProcessMemory)(HANDLE, void *, SIZE_T, DWORD, DWORD);
    __WriteProcessMemory _WriteProcessMemory = (__WriteProcessMemory)GetProcAddress(hMod, xorstr("WriteProcessMemory"));
    if (!WriteProcessMemory(hProcess, pRemoteBuf, &dll[0], bufsize, nullptr)) {
        throw std::runtime_error("WriteProcessMemory 1 fails. code=" + std::to_string(GetLastError()));
    }

    auto pLoadLibraryW = GetProcAddress(hMod, xorstr("LoadLibraryW"));
    if (!pLoadLibraryW) {
        throw std::runtime_error("Get LoadLibraryW fails. code=" + std::to_string(GetLastError()));
    }

    *(size_t *)(&bytecodes[6]) = (size_t)pRemoteBuf;
    *(size_t *)(&bytecodes[22]) = (size_t)pLoadLibraryW;

    const size_t pad = 32 - (bufsize % 32);
    auto funcAddr = (uint8_t *)pRemoteBuf + bufsize + pad;

    if (!WriteProcessMemory(hProcess, funcAddr, &bytecodes[0], bytecodes.size(), nullptr)) {
        throw std::runtime_error("WriteProcessMemory 2 fails. code=" + std::to_string(GetLastError()));
    }

    typedef HANDLE(WINAPI *__CreateRemoteThread)(HANDLE, void *, SIZE_T, void *, void *, DWORD, void *);
    __CreateRemoteThread _CreateRemoteThread = (__CreateRemoteThread)GetProcAddress(hMod, xorstr("CreateRemoteThread"));
    HANDLE hThread = _CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)funcAddr, nullptr, 0, nullptr);

    Sleep(1000);

    auto waitRes = WaitForSingleObject(hThread, 6000);
    if (waitRes == WAIT_TIMEOUT) {
        throw std::runtime_error("server start timed out.");
    } else if (waitRes != WAIT_OBJECT_0) {
        throw std::runtime_error(std::string("server start failed: code=") + std::to_string(GetLastError()));
    }

    DWORD code = 0;
    GetExitCodeThread(hThread, &code);

    // Faild LoadBibrary
    if (code != 1) {
        throw std::runtime_error("failed to load server. code=" + std::to_string(code));
    }

    CloseHandle(hThread);

    typedef HANDLE(WINAPI *__VirtualFreeEx)(HANDLE, void *, SIZE_T, DWORD);
    __VirtualFreeEx _VirtualFreeEx = (__VirtualFreeEx)GetProcAddress(hMod, xorstr("VirtualFreeEx"));
    _VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);

    CloseHandle(hProcess);
}
