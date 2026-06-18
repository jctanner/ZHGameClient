#include "GameClient/AIControlAdapter/AIControlAdapterUiUtils.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	void expectEq(const std::string& actual, const std::string& expected, const char* message)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << message << " actual=\"" << actual << "\" expected=\"" << expected << "\"\n";
			std::exit(1);
		}
	}

	void testNormalizeAsciiLower()
	{
		expectEq(
			AIControlAdapterUiUtils::NormalizeAsciiLower("Sprawl_BALANCED"),
			"sprawl_balanced",
			"Profile names should normalize to lowercase ASCII");
		expectEq(
			AIControlAdapterUiUtils::NormalizeAsciiLower("GLA-USA 2v2"),
			"gla-usa 2v2",
			"Mixed labels should normalize lowercase while preserving punctuation");
		std::cout << "PASS: testNormalizeAsciiLower\n";
	}

	void testFormatColorHex()
	{
		expectEq(
			AIControlAdapterUiUtils::FormatColorHex(0xAABBCCDDu),
			"#AABBCCDD",
			"ARGB color should format as eight-digit uppercase hex");
		expectEq(
			AIControlAdapterUiUtils::FormatColorHex(0x00000000u),
			"#00000000",
			"Zero color should keep all eight digits");
		std::cout << "PASS: testFormatColorHex\n";
	}

	void testFormatPlayerTickRequestId()
	{
		expectEq(
			AIControlAdapterUiUtils::FormatPlayerTickRequestId("auto_worker", 7, 0x1234u),
			"auto_worker_00000007_00001234",
			"Request ids should include prefix, player index, and tick as uppercase hex");
		expectEq(
			AIControlAdapterUiUtils::FormatPlayerTickRequestId(nullptr, 2, 0u),
			"request_00000002_00000000",
			"Null request id prefix should use fallback");
		std::cout << "PASS: testFormatPlayerTickRequestId\n";
	}
}

int main()
{
	testNormalizeAsciiLower();
	testFormatColorHex();
	testFormatPlayerTickRequestId();
	std::cout << "All AIControlAdapterUiUtils tests passed\n";
	return EXIT_SUCCESS;
}
