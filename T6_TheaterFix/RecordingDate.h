#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "DemoStatistics.h"

namespace T6_TheaterFix
{
	inline uint32_t GetFilenameTime(const char* name)
	{
		if (!name)
			return 0;
		const size_t length = std::strlen(name);
		if (length < 20 || length >= MAX_PATH || _stricmp(name + length - 5, ".demo"))
			return 0;
		char copy[MAX_PATH]{};
		std::memcpy(copy, name, length - 5);
		unsigned values[5]{}; // month, day, year, hour, minute
		for (int i = 4; i >= 0; --i)
		{
			char* underscore = std::strrchr(copy, '_');
			if (!underscore || !underscore[1])
				return 0;
			for (const char* p = underscore + 1; *p; ++p)
				if (*p < '0' || *p > '9')
					return 0;
			char* end = nullptr;
			const unsigned long value = std::strtoul(underscore + 1, &end, 10);
			if (!end || *end || value > 9999)
				return 0;
			values[i] = static_cast<unsigned>(value);
			*underscore = 0;
		}
		const unsigned month = values[0], day = values[1], year = values[2];
		if (year < 1970 || month < 1 || month > 12 || values[3] > 23 || values[4] > 59)
			return 0;
		const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
		if (day < 1 || day > days[month - 1] + (month == 2 && leap ? 1u : 0u))
			return 0;
		SYSTEMTIME time{};
		time.wYear = static_cast<WORD>(year);
		time.wMonth = static_cast<WORD>(month);
		time.wDay = static_cast<WORD>(day);
		time.wHour = static_cast<WORD>(values[3]);
		time.wMinute = static_cast<WORD>(values[4]);
		FILETIME local{}, utc{};
		// The game's filename date uses local wall time. Metadata timestamps are
		// already Unix UTC; this conversion is used only for a missing sidecar.
		if (!SystemTimeToFileTime(&time, &local) || !LocalFileTimeToFileTime(&local, &utc))
			return 0;
		const uint64_t ticks = (static_cast<uint64_t>(utc.dwHighDateTime) << 32) | utc.dwLowDateTime;
		if (ticks < 116444736000000000ull)
			return 0;
		const uint64_t seconds = (ticks - 116444736000000000ull) / 10000000;
		return seconds <= 0xffffffffull ? static_cast<uint32_t>(seconds) : 0;
	}
	inline uint32_t GetRecordingTime(const unsigned char* data, size_t size, const char* filename)
	{
		if (size == 255 && HasDdlHeader(data, size, 7))
		{
			const uint32_t time = static_cast<uint32_t>(ReadBits(data, size, 1443, 32));
			if (time)
				return time;
		}
		return GetFilenameTime(filename);
	}
	inline bool ApplyRecordingTime(void* metadata, const unsigned char* data, size_t size, const char* filename)
	{
		auto* b = static_cast<unsigned char*>(metadata);
		// 0x40 is the file slot/index, NOT its category. The category is the
		// second uint16 field at 0x74 (verified in OpenNet's copy constructor).
		uint16_t category = 0;
		std::memcpy(&category, b + 0x74, 2);
		if (category != 1)
			return false; // FILM_MP only; preserve other categories
		const uint32_t time = GetRecordingTime(data, size, filename);
		if (!time)
			return false;
		std::memcpy(b + 0x10, &time, 4); // created
		std::memcpy(b + 0x14, &time, 4); // modified; no separate recording edit date
		return true;
	}
}
