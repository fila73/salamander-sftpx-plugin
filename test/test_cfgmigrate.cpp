// Tests the sibling-key resolution used by the SFTP -> SFTPX configuration migration.
// Uses a temporary HKCU\Software\SftpxMigrationTest key (created and deleted here).
#include "../src/sftpcfgmigrate.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main()
{
    // pure string logic
    std::wstring sub;
    CHECK(SftpSiblingConfigSubKey(L"\\REGISTRY\\USER\\S-1-5-21-1\\Software\\X\\r\\Plugins Configuration\\SFTPX", L"SFTP", sub));
    CHECK(sub == L"S-1-5-21-1\\Software\\X\\r\\Plugins Configuration\\SFTP");
    CHECK(SftpSiblingConfigSubKey(L"\\registry\\user\\S-1\\a\\plugins configuration\\SFTPX (2)", L"SFTP (2)", sub));
    CHECK(!SftpSiblingConfigSubKey(L"\\REGISTRY\\MACHINE\\Software\\Plugins Configuration\\SFTPX", L"SFTP", sub));
    CHECK(!SftpSiblingConfigSubKey(L"\\REGISTRY\\USER\\S-1\\Software\\Other\\SFTPX", L"SFTP", sub));
    CHECK(!SftpSiblingConfigSubKey(L"\\REGISTRY\\USER\\S-1\\Plugins Configuration\\", L"SFTP", sub));
    CHECK(!SftpSiblingConfigSubKey(L"", L"SFTP", sub));

    // real registry
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\SftpxMigrationTest");
    const std::wstring base = L"Software\\SftpxMigrationTest\\Plugins Configuration";
    HKEY hOld, hNew, hSib;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, (base + L"\\SFTP").c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hOld, NULL) == ERROR_SUCCESS);
    DWORD v = 4;
    RegSetValueExW(hOld, L"Version", 0, REG_DWORD, (BYTE*)&v, 4);
    RegCloseKey(hOld);
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, (base + L"\\SFTPX").c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hNew, NULL) == ERROR_SUCCESS);

    std::wstring nt;
    CHECK(SftpGetKeyNtPath(hNew, nt));
    wprintf(L"nt path: %s\n", nt.c_str());
    CHECK(SftpOpenSiblingConfigKey(hNew, L"SFTP", hSib));
    DWORD got = 0, sz = 4;
    CHECK(RegQueryValueExW(hSib, L"Version", NULL, NULL, (BYTE*)&got, &sz) == ERROR_SUCCESS && got == 4);
    RegCloseKey(hSib);
    HKEY dummy;
    CHECK(!SftpOpenSiblingConfigKey(hNew, L"SFTP (2)", dummy));                // absent sibling
    CHECK(!SftpOpenSiblingConfigKey((HKEY)(UINT_PTR)0x1234, L"SFTP", dummy)); // not a real key
    RegCloseKey(hNew);

    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\SftpxMigrationTest");
    printf(failures ? "FAILED (%d)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
