#pragma once

#include <string>

class UnicodeString;

class AIControlAdapterUiUtils
{
public:
	static std::string NormalizeAsciiLower(const std::string& value);
	static std::string FormatColorHex(unsigned int argb);
	static std::string FormatPlayerTickRequestId(const char* prefix, int playerIndex, unsigned int tick);
	static std::string UnicodeToUtf8(const UnicodeString& text);
};
