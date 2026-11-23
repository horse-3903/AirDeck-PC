#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

inline bool isElevated()
{
#ifdef _WIN32
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation{};
	DWORD size = 0;
	const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
	CloseHandle(token);
	return ok && elevation.TokenIsElevated != 0;
#else
	return geteuid() == 0;
#endif
}
