#pragma once
// Included after SelectSummaryXuid in the plugin implementation.
// Only replace the XUID supplied to fileshare_summary_downloaded. At this
// callsite the original callback's film/valid arguments are still on stack.
// The one-argument cdecl caller retains its stack and EDX:EAX return ABI.
// clang-format off
__declspec(naked) void SummaryXuidHook()
{
	__asm
	{
		push dword ptr [esp + 32] // callback valid
		push dword ptr [esp + 32] // film high after the first push
		push dword ptr [esp + 32] // film low
		push dword ptr [esp + 16] // controller
		call SelectSummaryXuid
		add esp, 16
		ret
	}
}
// clang-format on
