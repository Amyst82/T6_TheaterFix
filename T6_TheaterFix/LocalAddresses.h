#pragma once
#include <cstdint>

namespace LocalAddresses
{

	// Absolute addresses apply only to the checked t6mpv43 image at 0x00400000.
	namespace Game
	{

		inline constexpr uintptr_t ImageBase = 0x00400000;
		inline constexpr uint32_t TimeDateStamp = 0x536E8FB4;
		inline constexpr uint32_t ImageSize = 0x03FEC000;

		inline constexpr uintptr_t GetXuid = 0x004EA8C0;
		inline constexpr uintptr_t GetGamertag = 0x004B58D0;
		inline constexpr uintptr_t SummaryXuidCall = 0x00602A99;
		inline constexpr uintptr_t MatchRecordValid = 0x0182D85C;
		inline constexpr uintptr_t MatchRecordState = 0x01814838;
		inline constexpr uintptr_t MatchRecordData = 0x01814850;

	}

	// OpenNet is relocatable. Add these RVAs to its actual loaded module base.
	namespace OpenNet
	{

		inline constexpr uint32_t ImageSize = 0x003D5000;
		inline constexpr uintptr_t FileInfo = 0x000252C0;
		inline constexpr uintptr_t MetadataPush = 0x00028B30;
		inline constexpr uintptr_t MetadataPushCalls[] = {

			0x000283AE, // listFilesByOwners
			0x0002DF76, // getFileMetaDataByID
			0x00033195	// getPooledMetaDataByID
		};

	}

}
