#include "pch.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "T6_TheaterFix.h"
#include "LocalAddresses.h"
#include "DemoMetadata.h"
#include "DemoStatistics.h"
#include "RecordingDate.h"

namespace T6_TheaterFix
{
#pragma region State
	HMODULE Module = nullptr;
	char LogPath[MAX_PATH]{};
	SRWLOCK LogLock = SRWLOCK_INIT;
	LONG MetadataRecords = 0;
	bool Inited = false;
	bool StatsInited = false;
	SRWLOCK OwnersLock = SRWLOCK_INIT;
	struct Owner
	{
		uint64_t film, author;
	} Owners[256]{};
	unsigned NextOwner = 0;
	void RememberOwner(const void* metadata, const T6_TheaterFix::String& blob)
	{
		uint64_t film = 0, author = 0;
		std::memcpy(&film, static_cast<const unsigned char*>(metadata) + 8, 8);
		T6_TheaterFix::GetPooledAuthor(reinterpret_cast<const unsigned char*>(blob.Bytes()), blob.size, author);
		AcquireSRWLockExclusive(&OwnersLock);
		for (auto& entry : Owners)
			if (entry.film == film && film)
			{
				entry.author = author;
				ReleaseSRWLockExclusive(&OwnersLock);
				return;
			}
		Owners[NextOwner++ % 256] = {film, author};
		ReleaseSRWLockExclusive(&OwnersLock);
	}
	uint64_t GetRecordedOwner(uint64_t film)
	{
		uint64_t result = 0;
		AcquireSRWLockShared(&OwnersLock);
		for (const auto& entry : Owners)
			if (entry.film == film && film)
			{
				result = entry.author;
				break;
			}
		ReleaseSRWLockShared(&OwnersLock);
		return result;
	}
	void* FileInfoTrampoline = nullptr;
	using Push = void*(__thiscall*)(void*, void*);
	Push OriginalPush = nullptr;

#pragma endregion

#pragma region HostAndLogging
	bool IsGame()
	{
		char path[MAX_PATH]{};
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		const char* name = std::strrchr(path, '\\');
		name = name ? name + 1 : path;
		return _stricmp(name, "t6mpv43.exe") == 0;
	}
	void Log(const char* format, ...)
	{
		if (!LogPath[0])
			return;
		char line[768]{};
		va_list args;
		va_start(args, format);
		int length = _vsnprintf_s(line, sizeof(line) - 3, _TRUNCATE, format, args);
		va_end(args);
		if (length < 0)
			length = static_cast<int>(std::strlen(line));
		line[length++] = '\r';
		line[length++] = '\n';
		AcquireSRWLockExclusive(&LogLock);
		HANDLE f = CreateFileA(LogPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			WriteFile(f, line, static_cast<DWORD>(length), &written, nullptr);
			CloseHandle(f);
		}
		ReleaseSRWLockExclusive(&LogLock);
	}
#pragma endregion

#pragma region Hooks
	void __cdecl ResetInfo(void* info)
	{
		T6_TheaterFix::ResetFileInfo(info);
	}

	// The original function takes its output as the first stack argument, and a
	// std::string by value after it. Preserve its complete custom calling ABI.
	// clang-format off
	__declspec(naked) void FileInfoHook()
	{
		__asm
		{
			pushfd
			pushad
			push dword ptr [esp + 40]
			call ResetInfo
			add esp, 4
			popad
			popfd
			jmp dword ptr [FileInfoTrampoline]
		}
	}
	// clang-format on
	void* __fastcall MetadataPushHook(void* vector, void*, void* metadata)
	{
		auto& blob = T6_TheaterFix::GetPayload(metadata);
		auto* b = static_cast<unsigned char*>(metadata);
		const auto& filename = *reinterpret_cast<const T6_TheaterFix::String*>(b + 0x44);
		T6_TheaterFix::ApplyRecordingTime(metadata, reinterpret_cast<const unsigned char*>(blob.Bytes()), blob.size, filename.Bytes());
		RememberOwner(metadata, blob);
		if (InterlockedIncrement(&MetadataRecords) <= 500)
		{
			const auto& name = *reinterpret_cast<const T6_TheaterFix::String*>(b + 0x44);
			uint32_t id = 0;
			std::memcpy(&id, b + 8, 4);
			uint32_t creationTime = 0;
			std::memcpy(&creationTime, b + 0x10, 4);
			Log("metadata id=%08lX name=%.*s bytes=%lu duration_ms=%lu create_time=%lu", static_cast<unsigned long>(id),
				static_cast<int>(name.size < 200 ? name.size : 200), name.Bytes(), static_cast<unsigned long>(blob.size),
				static_cast<unsigned long>(T6_TheaterFix::GetDuration(blob)), static_cast<unsigned long>(creationTime));
		}
		// push_back performs a deep copy, so resetting the reusable source leaves
		// all result records and OpenNet's allocator ownership intact.
		void* result = OriginalPush(vector, metadata);
		T6_TheaterFix::ClearString(blob);
		return result;
	}
	uint64_t __cdecl SelectSummaryXuid(int controller, uint64_t film, unsigned valid)
	{
		using Xuid = uint64_t(__cdecl*)(int);
		using Name = const char*(__cdecl*)(int);
		const uint64_t current = reinterpret_cast<Xuid>(LocalAddresses::Game::GetXuid)(controller);
		if (!StatsInited || controller < 0 || controller >= 4 || !(valid & 0xff) ||
			!*reinterpret_cast<const int*>(LocalAddresses::Game::MatchRecordValid))
			return current;
		const auto* definition = *reinterpret_cast<const uint32_t* const*>(LocalAddresses::Game::MatchRecordState + 12);
		if (!definition || definition[0] != 38 || definition[1] != 770134)
			return current;
		const uint64_t owner = GetRecordedOwner(film);
		const char* name = reinterpret_cast<Name>(LocalAddresses::Game::GetGamertag)(controller);
		const auto selected = T6_TheaterFix::SelectStatsXuid(reinterpret_cast<const unsigned char*>(LocalAddresses::Game::MatchRecordData),
			T6_TheaterFix::SummaryBytes, current, owner, name, 64, true);
		if (selected.mapped)
			Log("stats film=%016llX current=%016llX recorded=%016llX slot=%d kills=%lu deaths=%lu team=%lu", film, current, selected.xuid,
				selected.slot, static_cast<unsigned long>(selected.kills), static_cast<unsigned long>(selected.deaths),
				static_cast<unsigned long>(selected.team));
		else
			Log("stats unchanged film=%016llX current=%016llX owner=%016llX exact_slot=%d", film, current, owner, selected.slot);
		return selected.xuid;
	}
#include "StatisticsAbi.h"
#pragma endregion

#pragma region PatchInstallation
	struct Patch
	{
		unsigned char* address;
		unsigned char before[5];
		unsigned char after[5];
	};
	void CreateBranch(unsigned char* branchBytes, unsigned char opcode, const void* from, const void* to)
	{
		branchBytes[0] = opcode;
		const auto relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(from) - 5);
		std::memcpy(branchBytes + 1, &relative, 4);
	}
	bool WritePatch(const Patch& patch, bool restore = false)
	{
		DWORD old = 0;
		if (!VirtualProtect(patch.address, 5, PAGE_EXECUTE_READWRITE, &old))
			return false;
		std::memcpy(patch.address, restore ? patch.before : patch.after, 5);
		FlushInstructionCache(GetCurrentProcess(), patch.address, 5);
		DWORD ignored = 0;
		VirtualProtect(patch.address, 5, old, &ignored);
		return true;
	}
	bool PrepareStatsPatch(Patch& patch)
	{
		const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
		if (base != LocalAddresses::Game::ImageBase)
			return false;
		const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
		if (pe->FileHeader.TimeDateStamp != LocalAddresses::Game::TimeDateStamp ||
			pe->OptionalHeader.SizeOfImage != LocalAddresses::Game::ImageSize)
			return false;
		const unsigned char xuid[] = {
			0x8b, 0x4c, 0x24, 4, 0x69, 0xc9, 0x70, 0x10, 0, 0, 0x8b, 0x81, 0xa8, 0x0c, 3, 3, 0x8b, 0x91, 0xac, 0x0c, 3, 3, 0xc3};
		const unsigned char name[] = {0xe8, 0x9b, 0xdd, 7, 0, 0x84, 0xc0, 0x74, 0x0e, 0xa1, 0x30, 0x78, 0x1c, 1};
		const unsigned char context[] = {0xe8, 0x22, 0x7e, 0xee, 0xff, 0x52, 0x50, 0x68, 0x10, 0x7a, 0xc0, 0};
		if (std::memcmp(reinterpret_cast<const void*>(LocalAddresses::Game::GetXuid), xuid, sizeof(xuid)) ||
			std::memcmp(reinterpret_cast<const void*>(LocalAddresses::Game::GetGamertag), name, sizeof(name)) ||
			std::memcmp(reinterpret_cast<const void*>(LocalAddresses::Game::SummaryXuidCall), context, sizeof(context)))
			return false;
		patch.address = reinterpret_cast<unsigned char*>(LocalAddresses::Game::SummaryXuidCall);
		std::memcpy(patch.before, patch.address, 5);
		CreateBranch(patch.after, 0xe8, patch.address, &SummaryXuidHook);
		return true;
	}
	bool Init()
	{
		if (Inited)
			return true;
		if (!IsGame())
			return false;
		GetModuleFileNameA(Module, LogPath, MAX_PATH);
		char* slash = std::strrchr(LogPath, '\\');
		if (!slash)
			return false;
		strcpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - LogPath), "T6_TheaterFix.log");
		HMODULE net = GetModuleHandleA("OpenNet.Red32n");
		if (!net)
		{
			Log("DISABLED: OpenNet.Red32n is not loaded.");
			return false;
		}
		auto* base = reinterpret_cast<unsigned char*>(net);
		// No pattern guessing or executable patches on other builds. The four
		// sites and their targets must match the investigated OpenNet exactly.
		const unsigned char prologue[] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
		const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			return false;
		const auto* pe = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		if (pe->OptionalHeader.SizeOfImage != LocalAddresses::OpenNet::ImageSize ||
			std::memcmp(base + LocalAddresses::OpenNet::FileInfo, prologue, sizeof(prologue)) != 0)
		{
			Log("DISABLED: unsupported OpenNet image/prologue.");
			return false;
		}
		const auto& sites = LocalAddresses::OpenNet::MetadataPushCalls;
		Patch patches[5]{};
		patches[0].address = base + LocalAddresses::OpenNet::FileInfo;
		std::memcpy(patches[0].before, patches[0].address, 5);
		for (int i = 0; i < 3; ++i)
		{
			patches[i + 1].address = base + sites[i];
			std::memcpy(patches[i + 1].before, patches[i + 1].address, 5);
			unsigned char expected[5];
			CreateBranch(expected, 0xe8, patches[i + 1].address, base + LocalAddresses::OpenNet::MetadataPush);
			if (std::memcmp(expected, patches[i + 1].before, 5) != 0)
			{
				Log("DISABLED: metadata callsite mismatch at RVA %lX.", static_cast<unsigned long>(sites[i]));
				return false;
			}
		}
		if (!PrepareStatsPatch(patches[4]))
		{
			Log("DISABLED: unsupported native summary callback/identity ABI.");
			return false;
		}
		auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (!trampoline)
			return false;
		std::memcpy(trampoline, patches[0].before, 5);
		CreateBranch(trampoline + 5, 0xe9, trampoline + 5, patches[0].address + 5);
		DWORD ignored = 0;
		if (!VirtualProtect(trampoline, 16, PAGE_EXECUTE_READ, &ignored))
		{
			VirtualFree(trampoline, 0, MEM_RELEASE);
			return false;
		}
		FlushInstructionCache(GetCurrentProcess(), trampoline, 16);
		FileInfoTrampoline = trampoline;
		OriginalPush = reinterpret_cast<Push>(base + LocalAddresses::OpenNet::MetadataPush);
		CreateBranch(patches[0].after, 0xe9, patches[0].address, &FileInfoHook);
		for (int i = 1; i < 4; ++i)
			CreateBranch(patches[i].after, 0xe8, patches[i].address, &MetadataPushHook);
		// The loader calls PostInit before the user can open Theater. Keep the
		// hook code resident for the lifetime of the process.
		HMODULE pinned = nullptr;
		if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCSTR>(&Init), &pinned))
		{
			VirtualFree(trampoline, 0, MEM_RELEASE);
			return false;
		}
		int done = 0;
		for (; done < 5; ++done)
			if (!WritePatch(patches[done]))
				break;
		if (done != 5)
		{
			while (done > 0)
			{
				--done;
				WritePatch(patches[done], true);
			}
			Log("DISABLED: memory patch failed; previous sites restored.");
			return false;
		}
		Inited = true;
		StatsInited = true;
		Log("T6_TheaterFix 0.3.1 ACTIVE: metadata buffers + per-film summary identity + recording dates. OpenNet base=%p", net);
		return true;
	}
#pragma endregion

}
