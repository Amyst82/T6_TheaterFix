#pragma once
#include <cstdint>
#include <cstddef>

namespace T6_TheaterFix
{
	// OpenNet was built with the 32-bit MSVC string ABI: 16-byte union,
	// uint32 length, uint32 capacity. Never allocate or free its memory here.
	struct String
	{
		union
		{
			char small[16];
			char* heap;
		} data;
		uint32_t size;
		uint32_t capacity;
		char* Bytes()
		{
			return capacity >= 16 ? data.heap : data.small;
		}
		const char* Bytes() const
		{
			return capacity >= 16 ? data.heap : data.small;
		}
	};
	static_assert(sizeof(void*) == 4, "x86 only");
	static_assert(sizeof(String) == 24, "OpenNet string ABI");
	inline void ClearString(String& s)
	{
		s.size = 0;
		s.Bytes()[0] = 0;
	}
	inline void ResetFileInfo(void* info)
	{
		auto* b = static_cast<unsigned char*>(info);
		ClearString(*reinterpret_cast<String*>(b + 0x48)); // summary
		ClearString(*reinterpret_cast<String*>(b + 0x60)); // thumbnail/DDL
		// bdTag contains a vtable plus two uint64 values; it owns no allocations.
		*reinterpret_cast<uint32_t*>(b + 0x7c) = *reinterpret_cast<uint32_t*>(b + 0x78);
	}
	inline String& GetPayload(void* metadata)
	{
		return *reinterpret_cast<String*>(static_cast<unsigned char*>(metadata) + 0x7c);
	}
	inline uint32_t GetDuration(const String& s)
	{
		if (s.size < 228)
			return 0;
		const auto* b = reinterpret_cast<const unsigned char*>(s.Bytes());
		uint64_t packedBits = 0;
		for (int i = 0; i < 5; ++i)
			packedBits |= static_cast<uint64_t>(b[223 + i]) << (8 * i);
		return static_cast<uint32_t>(packedBits >> 11);
	}
}
