#include "pch.h"
#include "T6_TheaterFix.h"

// Redacted expects these exports to return a 64-bit value, including strings.
extern "C" int64_t __cdecl PreInit()
{
	return 1;
}

extern "C" int64_t __cdecl PostInit()
{
	if (T6_TheaterFix::IsGame())
		T6_TheaterFix::Init();

	return 1;
}

extern "C" int64_t __cdecl AuthorInfo()
{
	return reinterpret_cast<intptr_t>("Amyst 2026");
}

extern "C" int64_t __cdecl ExtendedInfo()
{
	return reinterpret_cast<intptr_t>("T6_TheaterFix 0.3.1");
}

extern "C" int64_t __cdecl OfficialMod()
{
	return 0;
}

extern "C" int64_t __cdecl DependencyCount()
{
	return 0;
}

extern "C" int64_t __cdecl GetDependency(int)
{
	return 0;
}

extern "C" int64_t __cdecl SendMessageB(const char*)
{
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		T6_TheaterFix::Module = hModule;
		DisableThreadLibraryCalls(hModule);
		break;
	}

	return TRUE;
}
