#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <httplib.h>

#include <filesystem>
#include <iostream>

#include "Config.h"

#define ModuleName "SatisfactoryWebMapServer"

bool setup();

using namespace httplib;

Server s;
Config config;

std::filesystem::path dllDir;

HANDLE readyEvent = NULL;

void MainThread(HMODULE hModule)
{
	OutputDebugStringA("Main Thread Started");

	readyEvent = CreateEventA(nullptr, true, false, "SatisfactoryWebMapServerReadyEvent");
	if (readyEvent == NULL) {
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			MessageBoxA(NULL, "Server already started", ModuleName, MB_ICONWARNING);
		} else {
			MessageBoxA(NULL, "Unable to create server ready event", ModuleName, MB_ICONERROR);
		}
		return;
	}

	char dll[MAX_PATH]{ 0 };
	if (!GetModuleFileNameA(hModule, &dll[0], sizeof(dll))) {
		dllDir = std::filesystem::path(".");
	} else {
		dllDir = std::filesystem::path(dll).parent_path();
	}

	config = Config::Load((dllDir / "config.json").string());

	auto res = setup();
	if (!res) {
		MessageBoxA(NULL, "Unable to init modules", ModuleName, MB_ICONERROR);

		FreeLibraryAndExitThread(hModule, 1);
		return;
	}

	if (!config.APIOnly) {
		auto root = config.Root;
		if (root.empty()) {
			root = (dllDir / "web").string();
		}

		OutputDebugStringA(("Using " + root + " as webroot").c_str());

		auto ret = s.set_mount_point("/", root.c_str());
		if (!ret) {
			std::string path;
			path.resize(MAX_PATH);
			auto len = GetFullPathNameA(root.c_str(), path.size(), &path[0], nullptr);
			path.resize(len);

			MessageBoxA(NULL, (path + " dose not exists").c_str(), ModuleName, MB_ICONERROR);
		}
	}

	s.Get("/api/stop", [&](const Request &req, Response &res) {
		s.stop();
	});

	SetEvent(readyEvent);

	s.listen(config.IP.c_str(), config.Port);

	ResetEvent(readyEvent);
	CloseHandle(readyEvent);

	OutputDebugStringA("Main Thread Exiting");

	HMODULE hModule2;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (wchar_t *)MainThread, &hModule2);

	FreeLibraryAndExitThread(hModule2, 0);
}

DWORD WINAPI _Proxy(HMODULE hModule)
{
	Sleep(3000);

	std::thread([=] {
		MainThread(hModule);
	}).detach();

	return 0;
}

BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
)
{
	if (fdwReason == DLL_PROCESS_ATTACH) {
		CloseHandle(CreateRemoteThread(GetCurrentProcess(), nullptr, 0, (LPTHREAD_START_ROUTINE)_Proxy, hinstDLL, 0, nullptr));
	} else if (fdwReason == DLL_PROCESS_DETACH) {
		s.stop();
	}

	return TRUE;
}
