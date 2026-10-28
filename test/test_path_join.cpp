// Standalone unit test of the POSIX path helpers from src/sftpglue.cpp.
// The helpers (SftpNormalize .. SftpParent) are extracted verbatim from sftpglue.cpp by
// build_test_path_join.bat into the generated sftpglue_paths.inc (sftpglue.cpp itself needs
// the whole plugin: Salamander interfaces, libssh2, ...).
#include <windows.h>
#include <string.h>
#include <stdio.h>

#include "sftpglue_paths.inc"

static int g_fail = 0;

static void CheckStr(const char* what, const char* got, const char* want)
{
    bool ok = strcmp(got, want) == 0;
    printf("%s %s: got \"%s\"\n", ok ? "PASS" : "FAIL", what, got);
    if (!ok)
    {
        printf("     want \"%s\"\n", want);
        g_fail++;
    }
}

static void CheckName(const char* what, const char* base, const char* name, bool wantOk, const char* wantPath)
{
    char out[MAX_PATH];
    bool ok = SftpJoinName(base, name, out, MAX_PATH);
    if (ok != wantOk)
    {
        printf("FAIL %s: SftpJoinName returned %d, want %d\n", what, ok, wantOk);
        g_fail++;
        return;
    }
    CheckStr(what, out, wantOk ? wantPath : ""); // out is cleared on failure
}

static void CheckNormalize(const char* what, const char* in, const char* want)
{
    char buf[MAX_PATH];
    lstrcpyn(buf, in, MAX_PATH);
    SftpNormalize(buf);
    CheckStr(what, buf, want);
}

int main()
{
    CheckName("normal join", "/home/u", "file.txt", true, "/home/u/file.txt");
    CheckName("join trailing slash", "/home/u/", "file.txt", true, "/home/u/file.txt");
    CheckName("root join", "/", "file.txt", true, "/file.txt");
    CheckName("empty base", "", "file.txt", true, "/file.txt");
    CheckName("name ..", "/a/b", "..", false, NULL);
    CheckName("name .", "/a/b", ".", false, NULL);
    CheckName("name empty", "/a/b", "", false, NULL);
    CheckName("name a/b", "/a/b", "a/b", false, NULL);
    CheckName("name ../x", "/a/b", "../x", false, NULL);
    CheckName("name ..\\..\\x (literal backslash name)", "/a/b", "..\\..\\x", true, "/a/b/..\\..\\x");
    CheckName("name a\\b preserved", "/a/b", "a\\b", true, "/a/b/a\\b");
    CheckName("name ..a is ordinary", "/a", "..a", true, "/a/..a");

    CheckNormalize("user /a/b/../c", "/a/b/../c", "/a/c");
    CheckNormalize("user /..", "/..", "/");
    CheckNormalize("user /a/./b", "/a/./b", "/a/b");
    CheckNormalize("user empty", "", "/");
    CheckNormalize("backslash is not a separator", "/a/b\\c/..", "/a");
    CheckNormalize("backslash name kept", "/a/b\\c", "/a/b\\c");
    CheckNormalize("backslash dotdot is a name", "/a/..\\..\\x", "/a/..\\..\\x");

    char out[MAX_PATH];
    SftpJoin("/a/b", "../c", out, MAX_PATH);
    CheckStr("user join ../c", out, "/a/c");
    SftpJoin("/a/b", "..", out, MAX_PATH);
    CheckStr("user join cd ..", out, "/a");
    SftpJoin("/", "..", out, MAX_PATH);
    CheckStr("user join / ..", out, "/");

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
