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

    // truncation: result longer than outSize -> false, out ""
    {
        char tiny[8];
        memset(tiny, 'x', sizeof(tiny));
        bool ok = SftpJoinName("/home/user", "file.txt", tiny, (int)sizeof(tiny));
        if (ok || tiny[0] != 0)
        {
            printf("FAIL truncation: ok=%d out=\"%s\"\n", ok, tiny);
            g_fail++;
        }
    }

    // typed-path rule: remote part starting with '\' is Windows-style, otherwise '\' is literal
    SftpRemotePart("\\\\host\\home\\u", out, MAX_PATH);
    CheckStr("remote part \\\\host\\home\\u", out, "/home/u");
    SftpRemotePart("//user@host/home/u", out, MAX_PATH);
    CheckStr("remote part //user@host/home/u", out, "/home/u");
    SftpRemotePart("//host", out, MAX_PATH);
    CheckStr("remote part //host", out, "/");
    SftpRemotePart("//host/a\\b", out, MAX_PATH);
    CheckStr("remote part /a\\b stays", out, "/a\\b");
    SftpRemotePart("/home/u/..\\..\\etc", out, MAX_PATH);
    CheckStr("remote part literal ..\\..\\etc stays", out, "/home/u/..\\..\\etc");
    SftpRemotePart("sub\\dir", out, MAX_PATH);
    CheckStr("remote part relative sub\\dir stays", out, "sub\\dir");
    SftpRemotePart("\\home\\u", out, MAX_PATH);
    CheckStr("remote part \\home\\u (no host)", out, "/home/u");

    // LocalJoinName: untrusted remote name -> local Windows path component
    {
        char lo[MAX_PATH];
        struct { const char* name; bool ok; } cases[] = {
            {"file.txt", true}, {"a.b.c", true}, {"..a", true}, {"a..b", true}, {"con1", true}, {"COM10", true},
            {"LPT", true}, {"comx", true}, {"nul_x", true}, {"\xC4\x8D\xC3\xA1st.txt", true}, {"na\xEFve", true},
            {"", false}, {".", false}, {"..", false}, {"..\\..\\x", false}, {"../x", false}, {"a\\b", false},
            {"a/b", false}, {"C:evil", false}, {"x:stream", false}, {"a*b", false}, {"a?b", false},
            {"a\"b", false}, {"a<b", false}, {"a>b", false}, {"a|b", false}, {"a\tb", false}, {"a\x01", false},
            {"foo.", false}, {"foo ", false}, {"foo...", false},
            {"CON", false}, {"con", false}, {"CON.txt", false}, {"con ", false}, {"con .txt", false}, {"Prn", false},
            {"AUX.tar.gz", false}, {"nul", false}, {"NUL.", false}, {"COM1", false}, {"com9.txt", false},
            {"COM0", false}, {"LPT1.tar.gz", false}, {"lpt9", false}, {"COM\xB9", false}, {"LPT\xB2.x", false},
            {"COM\xB3", false},
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            bool ok = LocalJoinName("C:\\tmp", cases[i].name, lo, MAX_PATH);
            if (ok != cases[i].ok || (!ok && lo[0] != 0))
            {
                printf("FAIL LocalJoinName(\"%s\"): got %d, want %d\n", cases[i].name, ok, cases[i].ok);
                g_fail++;
            }
        }
        CheckStr("local join adds backslash", LocalJoinName("C:\\tmp", "a.txt", lo, MAX_PATH) ? lo : "?", "C:\\tmp\\a.txt");
        CheckStr("local join keeps single backslash", LocalJoinName("C:\\tmp\\", "a.txt", lo, MAX_PATH) ? lo : "?", "C:\\tmp\\a.txt");
        CheckStr("local join root", LocalJoinName("C:\\", "a.txt", lo, MAX_PATH) ? lo : "?", "C:\\a.txt");
        // empty base would yield a relative path: rejected
        if (LocalJoinName("", "a.txt", lo, MAX_PATH) || lo[0] != 0)
        {
            printf("FAIL LocalJoinName: empty base accepted\n");
            g_fail++;
        }
        // overflow: "C:\tmp\a.txt" is 12 chars + NUL
        char tiny[13];
        memset(tiny, 'x', sizeof(tiny));
        bool okFit = LocalJoinName("C:\\tmp", "a.txt", tiny, 13);
        char tiny2[12];
        memset(tiny2, 'x', sizeof(tiny2));
        bool okOver = LocalJoinName("C:\\tmp", "a.txt", tiny2, 12);
        if (!okFit || okOver || tiny2[0] != 0)
        {
            printf("FAIL LocalJoinName overflow: fit=%d over=%d out=\"%s\"\n", okFit, okOver, tiny2);
            g_fail++;
        }
    }

    // SftpParseHostSpec
    {
        struct { const char* in; bool ok; const char* user; const char* host; int port; } c[] = {
            {"//u@h/p", true, "u", "h", 22},
            {"//h", true, "", "h", 22},
            {"\\\\u@h:2222/x", true, "u", "h", 2222},
            {"//a@b@h:22/x", true, "a@b", "h", 22},
            {"//h:/x", true, "", "h", 22},
            {"//h:0/x", false, "", "", 22},
            {"//h:65536/x", false, "", "", 22},
            {"//h:65535/x", true, "", "h", 65535},
            {"//h:-1/x", false, "", "", 22},
            {"//h:22x/x", false, "", "", 22},
            {"//h:99999999999999/x", false, "", "", 22},
            {"//u@/x", false, "", "", 22},
            {"/path", false, "", "", 22},
            {"", false, "", "", 22},
            {"//[::1]/x", true, "", "::1", 22},
            {"//u@[fe80::1]:2222/x", true, "u", "fe80::1", 2222},
            {"//[::1]:/x", true, "", "::1", 22},
            {"//[::1]:0/x", false, "", "", 22},
            {"//[::1]x/x", false, "", "", 22},
            {"//[::1/x", false, "", "", 22},
            {"//[]/x", false, "", "", 22},
            {"//[host]/x", false, "", "", 22},
            {"//::1/x", false, "", "", 22},
            {"//user:pw@h/x", false, "", "", 22},
            {"//h./x", true, "", "h.", 22},
            {"//MyHost.Example.COM/x", true, "", "MyHost.Example.COM", 22},
        };
        for (auto& t : c)
        {
            char u[128], h[256];
            int port;
            bool ok = SftpParseHostSpec(t.in, u, sizeof(u), h, sizeof(h), &port);
            bool good = ok == t.ok && (!ok || (strcmp(u, t.user) == 0 && strcmp(h, t.host) == 0 && port == t.port));
            printf("%s SftpParseHostSpec \"%s\" -> ok=%d user=\"%s\" host=\"%s\" port=%d\n", good ? "PASS" : "FAIL", t.in, ok, u, h, port);
            if (!good)
                g_fail++;
        }
    }

    printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
