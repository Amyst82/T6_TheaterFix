#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace T6_TheaterFix
{
	constexpr size_t SummaryBytes = 102352;
	constexpr size_t PlayerBase = 511;
	constexpr size_t PlayerStride = 17259;
	constexpr size_t PlayerSlots = 30;

	inline uint64_t ReadBits(const unsigned char* data, size_t size, size_t offset, unsigned count)
	{
		offset += 384;
		if (count > 64 || offset + count > size * 8)
			return 0;
		uint64_t result = 0;
		for (unsigned i = 0; i < count; ++i)
			result |= static_cast<uint64_t>((data[(offset + i) / 8] >> ((offset + i) % 8)) & 1) << i;
		return result;
	}
	inline bool HasDdlHeader(const unsigned char* data, size_t size, unsigned char version)
	{
		const unsigned char header[] = {0, 0, 0, 0, 0, 0, 0, version, 1, 0, 0, 2, 0xbe, 0xef};
		return data && size >= sizeof(header) && std::memcmp(data, header, sizeof(header)) == 0;
	}
	inline bool GetPooledAuthor(const unsigned char* data, size_t size, uint64_t& owner)
	{
		owner = 0;
		if (size != 255 || !HasDdlHeader(data, size, 7) || ReadBits(data, size, 0, 1) != 1)
			return false;
		owner = ReadBits(data, size, 257, 64);
		return owner != 0;
	}
	inline bool NormalizeName(const char* source, size_t available, char (&out)[64])
	{
		size_t n = 0;
		if (!source)
			return false;
		for (size_t i = 0; i < available; ++i)
		{
			unsigned char c = static_cast<unsigned char>(source[i]);
			if (!c)
			{
				out[n] = 0;
				return n != 0;
			}
			if (c == '^' && i + 1 < available && source[i + 1] >= '0' && source[i + 1] <= '9')
			{
				++i;
				continue;
			}
			if (n + 1 >= sizeof(out) || c < 32)
				return false;
			if (c >= 'A' && c <= 'Z')
				c += 32;
			out[n++] = static_cast<char>(c);
		}
		return false; // a truncated / unterminated name cannot establish identity
	}
	struct StatsChoice
	{
		uint64_t xuid;
		int slot;
		uint32_t kills, deaths, team;
		bool mapped;
	};
	inline StatsChoice SelectStatsXuid(
		const unsigned char* data, size_t size, uint64_t current, uint64_t owner, const char* currentName, size_t nameAvailable, bool valid)
	{
		StatsChoice result{current, -1, 0, 0, 0, false};
		if (!valid || !current || size != SummaryBytes || !HasDdlHeader(data, size, 38))
			return result;
		// Exact account identity always takes priority, including on somebody
		// else's recording and when multiple participants used the same name.
		for (size_t slot = 0; slot < PlayerSlots; ++slot)
		{
			if (ReadBits(data, size, PlayerBase + slot * PlayerStride, 64) == current)
			{
				result.slot = static_cast<int>(slot);
				return result;
			}
		}
		if (!owner || owner == current || (owner >> 56) == 0xff)
			return result;
		char wanted[64]{};
		if (!NormalizeName(currentName, nameAvailable, wanted))
			return result;
		int authorSlot = -1, nameMatches = 0, ownerMatches = 0;
		for (size_t slot = 0; slot < PlayerSlots; ++slot)
		{
			const size_t base = PlayerBase + slot * PlayerStride;
			const uint64_t id = ReadBits(data, size, base, 64);
			char recorded[16]{};
			for (unsigned i = 0; i < 16; ++i)
				recorded[i] = static_cast<char>(ReadBits(data, size, base + 416 + i * 8, 8));
			char normalized[64]{};
			const bool sameName = NormalizeName(recorded, sizeof(recorded), normalized) && std::strcmp(wanted, normalized) == 0;
			if (id && (id >> 56) != 0xff && sameName)
				++nameMatches;
			if (id == owner)
			{
				++ownerMatches;
				if (sameName)
					authorSlot = static_cast<int>(slot);
			}
		}
		if (authorSlot < 0 || ownerMatches != 1 || nameMatches != 1)
			return result;
		const size_t base = PlayerBase + static_cast<size_t>(authorSlot) * PlayerStride;
		result.xuid = owner;
		result.slot = authorSlot;
		result.mapped = true;
		result.kills = static_cast<uint32_t>(ReadBits(data, size, base + 1043, 16));
		result.deaths = static_cast<uint32_t>(ReadBits(data, size, base + 1067, 16));
		result.team = static_cast<uint32_t>(ReadBits(data, size, base + 544, 8));
		return result;
	}
}
