#include "PreRTS.h"
#include "GameClient/AIControlAdapter/AIControlAdapterUiUtils.h"

#include "Common/UnicodeString.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

std::string AIControlAdapterUiUtils::NormalizeAsciiLower(const std::string& value)
{
	std::string out = value;
	std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

std::string AIControlAdapterUiUtils::FormatColorHex(unsigned int argb)
{
	char buffer[16];
	sprintf_s(buffer, "#%08X", argb);
	return std::string(buffer);
}

std::string AIControlAdapterUiUtils::FormatPlayerTickRequestId(const char* prefix, int playerIndex, unsigned int tick)
{
	char buffer[96];
	sprintf_s(
		buffer,
		"%s_%08X_%08X",
		prefix != nullptr && prefix[0] != '\0' ? prefix : "request",
		static_cast<unsigned int>(playerIndex),
		tick);
	return std::string(buffer);
}

std::string AIControlAdapterUiUtils::UnicodeToUtf8(const UnicodeString& text)
{
	const WideChar* wide = text.str();
	if (wide == nullptr || wide[0] == 0)
	{
		return std::string();
	}

	const int utf8LenWithNull = ::WideCharToMultiByte(
		CP_UTF8,
		0,
		wide,
		-1,
		nullptr,
		0,
		nullptr,
		nullptr);
	if (utf8LenWithNull <= 1)
	{
		return std::string();
	}

	std::string out;
	out.resize(static_cast<std::size_t>(utf8LenWithNull));
	::WideCharToMultiByte(
		CP_UTF8,
		0,
		wide,
		-1,
		&out[0],
		utf8LenWithNull,
		nullptr,
		nullptr);
	if (!out.empty() && out[out.size() - 1] == '\0')
	{
		out.resize(out.size() - 1);
	}
	return out;
}
