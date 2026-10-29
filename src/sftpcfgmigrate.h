// SPDX-License-Identifier: GPL-2.0-or-later
// Helpers for the one-time migration of the plugin configuration from the pre-rename key
// "Plugins Configuration\SFTP" to "Plugins Configuration\SFTPX". Header-only, depends only on the
// Windows API and the STL, so it can be unit-tested stand-alone (see test\build_test_cfgmigrate.bat).
#pragma once

#include <windows.h>
#include <string>

// Pure string helper. 'ntPath' is the kernel name of the plugin's own configuration key, e.g.
//   \REGISTRY\USER\S-1-5-21-...\Software\Open Salamander Samandarin\5.0-samandarin-0.16\Plugins Configuration\SFTPX
// On success 'subKey' is the path of the sibling key 'siblingName' under the same "Plugins Configuration"
// key, relative to HKEY_USERS. Fails (false) for anything else (e.g. HKLM, other layout).
inline bool SftpSiblingConfigSubKey(const std::wstring& ntPath, const wchar_t* siblingName, std::wstring& subKey)
{
    static const wchar_t prefix[] = L"\\REGISTRY\\USER\\";
    static const wchar_t parentName[] = L"Plugins Configuration";
    const size_t prefixLen = sizeof(prefix) / sizeof(prefix[0]) - 1;
    if (ntPath.size() <= prefixLen || _wcsnicmp(ntPath.c_str(), prefix, prefixLen) != 0)
        return false;
    size_t lastSlash = ntPath.rfind(L'\\');
    if (lastSlash == std::wstring::npos || lastSlash + 1 >= ntPath.size() || lastSlash < prefixLen)
        return false;
    std::wstring parent = ntPath.substr(0, lastSlash);
    size_t parentSlash = parent.rfind(L'\\');
    if (parentSlash == std::wstring::npos || parentSlash < prefixLen ||
        _wcsicmp(parent.c_str() + parentSlash + 1, parentName) != 0)
        return false;
    subKey = parent.substr(prefixLen) + L"\\" + siblingName;
    return true;
}

// Kernel name of an open registry key (NtQueryKey, KeyNameInformation; ntdll is loaded dynamically).
// Fails for handles that are not real registry keys (e.g. Salamander's virtual keys for a config.reg file).
inline bool SftpGetKeyNtPath(HKEY key, std::wstring& path)
{
    typedef LONG(NTAPI * FNtQueryKey)(HANDLE, int, PVOID, ULONG, PULONG);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FNtQueryKey fn = ntdll != NULL ? (FNtQueryKey)GetProcAddress(ntdll, "NtQueryKey") : NULL;
    if (fn == NULL || key == NULL)
        return false;
    const int KeyNameInformation = 3;
    BYTE buf[4096];
    ULONG len = 0;
    if (fn((HANDLE)key, KeyNameInformation, buf, sizeof(buf), &len) < 0) // NTSTATUS failure
        return false;
    ULONG nameBytes = *(ULONG*)buf; // KEY_NAME_INFORMATION { ULONG NameLength; WCHAR Name[1]; }
    if (nameBytes == 0 || nameBytes > sizeof(buf) - sizeof(ULONG))
        return false;
    path.assign((const wchar_t*)(buf + sizeof(ULONG)), nameBytes / sizeof(wchar_t));
    return true;
}

// Opens (read-only) the existing key 'siblingName' next to the plugin's own key 'ownKey'.
// Returns false (silently) when the layout is unexpected or the key does not exist.
inline bool SftpOpenSiblingConfigKey(HKEY ownKey, const wchar_t* siblingName, HKEY& out)
{
    std::wstring nt, sub;
    if (!SftpGetKeyNtPath(ownKey, nt) || !SftpSiblingConfigSubKey(nt, siblingName, sub))
        return false;
    return RegOpenKeyExW(HKEY_USERS, sub.c_str(), 0, KEY_READ, &out) == ERROR_SUCCESS;
}
