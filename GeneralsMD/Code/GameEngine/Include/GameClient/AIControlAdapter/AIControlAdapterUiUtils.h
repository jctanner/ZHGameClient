#pragma once

#include <string>

class UnicodeString;

class AIControlAdapterUiUtils
{
public:
	static std::string NormalizeAsciiLower(const std::string& value);
	static std::string FormatColorHex(unsigned int argb);
	static std::string UnicodeToUtf8(const UnicodeString& text);
};
