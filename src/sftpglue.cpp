// Copyright © 2026 Dupl3xx
#include "precomp.h"
#include "sftpglue.h"
#include <string.h>

CSftpSavedProfile SftpProfiles[SFTP_MAX_PROFILES];
int SftpProfileCount = 0;
char SftpDefaultSession[128] = "";
char SftpFolders[SFTP_MAX_FOLDERS][128];
int SftpFolderCount = 0;
int SftpEncoding = 0; // 0 = Auto (UTF-8), 1 = UTF-8, 2 = Off (no conversion)

// host key trust prompt
static int SftpHostKeyCallback(void* /*ctx*/, const char* host, int port, const char* type,
                               const char* fp, int status)
{
    HWND parent = SalamanderGeneral->GetMsgBoxParent();
    char msg[1024];
    if (status == 1) // key changed
    {
        _snprintf_s(msg, _TRUNCATE,
                    "WARNING: Host key for %s:%d has CHANGED!\n\n"
                    "Key type: %s\nFingerprint (SHA256):\n%s\n\n"
                    "This could be a MITM attack. Really connect and save the new key?",
                    host, port, type, fp);
        return SalamanderGeneral->SalMessageBox(parent, msg, "SFTP – host key changed",
                                                MB_YESNO | MB_ICONWARNING) == IDYES
                   ? 2
                   : 0;
    }
    _snprintf_s(msg, _TRUNCATE,
                "Server %s:%d is not yet known.\n\nKey type: %s\nFingerprint (SHA256):\n%s\n\n"
                "Trust this server?\n"
                "Yes = save key and connect\nNo = connect this time only\nCancel = reject connection",
                host, port, type, fp);
    int r = SalamanderGeneral->SalMessageBox(parent, msg, "SFTP – unknown server",
                                             MB_YESNOCANCEL | MB_ICONQUESTION);
    return r == IDYES ? 2 : (r == IDNO ? 1 : 0);
}

// keyboard-interactive: server prompt (e.g. 2FA code) -> ask user
static bool SftpKbdPromptCallback(void* /*ctx*/, const char* prompt, bool echo, char* out, int outSize)
{
    return SftpInputDialog(SalamanderGeneral->GetMsgBoxParent(), prompt, echo, out, outSize);
}

// path to known_hosts file (in user profile)
static const char* SftpKnownHostsPath()
{
    static char path[MAX_PATH] = "";
    if (path[0] == 0)
    {
        const char* appdata = getenv("APPDATA");
        if (appdata != nullptr && appdata[0] != 0)
        {
            _snprintf_s(path, _TRUNCATE, "%s\\OpenSalamander-SFTP", appdata);
            CreateDirectoryA(path, nullptr);
            _snprintf_s(path, _TRUNCATE, "%s\\OpenSalamander-SFTP\\known_hosts", appdata);
        }
    }
    return path;
}

bool SftpEnsureConnected(HWND parent, CSftpConnection& conn, CSftpProfile& profile)
{
    SftpTraceLog("SftpEnsureConnected: start host=%s port=%d user=%s isConnected=%d",
                 profile.Host, profile.Port, profile.User, (int)conn.IsConnected());
    if (conn.IsConnected())
        return true;
    if (!profile.Valid || profile.Host[0] == 0)
    {
        SftpTraceLog("SftpEnsureConnected: invalid profile");
        SalamanderGeneral->SalMessageBox(parent, "No SFTP connection configured.\nOpen sftp: path and enter server.",
                                         LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return false;
    }
    static bool initialized = false;
    if (!initialized)
    {
        SftpTraceLog("SftpEnsureConnected: performing one-time initialization");
        CSftpConnection::GlobalInit();
        CSftpConnection::SetKnownHostsFile(SftpKnownHostsPath());
        CSftpConnection::SetHostKeyCallback(SftpHostKeyCallback, nullptr);
        CSftpConnection::SetKbdPromptCallback(SftpKbdPromptCallback, nullptr);
        initialized = true;
        SftpTraceLog("SftpEnsureConnected: one-time initialization complete");
    }
    CSftpConnection::SetEncoding(SftpEncoding); // filename encoding
    SftpTraceLog("SftpEnsureConnected: calling conn.Connect");
    if (!conn.Connect(profile.Host, profile.Port, profile.User, profile.Password, profile.KeyFile,
                      profile.UseCompression, profile.Protocol, profile.ScpFallback, profile.SftpServer))
    {
        SftpTraceLog("SftpEnsureConnected: conn.Connect failed: %s", conn.LastError());
        char buf[600];
        _snprintf_s(buf, _TRUNCATE, "Cannot connect to SFTP server %s:%d.\n\n%s",
                    profile.Host, profile.Port, conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, buf, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return false;
    }
    SftpTraceLog("SftpEnsureConnected: conn.Connect succeeded!");
    return true;
}

void SftpNormalize(char* path)
{
    // split into components by "/", resolve "." and ".."
    // backslash is NOT a separator: on a POSIX server it is an ordinary filename character
    char tmp[MAX_PATH];
    lstrcpyn(tmp, (path && *path) ? path : "/", MAX_PATH);

    const char* parts[256];
    int n = 0;
    char* save = NULL;
    char* tok = strtok_s(tmp, "/", &save);
    while (tok != NULL && n < 256)
    {
        if (strcmp(tok, ".") == 0)
        {
            // skip
        }
        else if (strcmp(tok, "..") == 0)
        {
            if (n > 0)
                n--;
        }
        else
            parts[n++] = tok;
        tok = strtok_s(NULL, "/", &save);
    }
    char* out = path;
    *out++ = '/';
    for (int i = 0; i < n; i++)
    {
        if (i > 0)
            *out++ = '/';
        int len = (int)strlen(parts[i]);
        memmove(out, parts[i], len); // memmove: source and dest in same buffer (tmp was a copy)
        out += len;
    }
    *out = 0;
}

void SftpJoin(const char* base, const char* name, char* out, int outSize)
{
    char b[MAX_PATH];
    lstrcpyn(b, (base && *base) ? base : "/", MAX_PATH);
    int bl = (int)strlen(b);
    if (bl > 0 && b[bl - 1] == '/')
        b[bl - 1] = 0; // remove trailing slash (except root "" -> joins to "/name")
    _snprintf_s(out, outSize, _TRUNCATE, "%s/%s", b, (name && *name) ? name : "");
    SftpNormalize(out);
}

bool SftpJoinName(const char* base, const char* name, char* out, int outSize)
{
    // 'name' is ONE literal path component (e.g. from a server listing): never normalized,
    // so it cannot climb ("..") or split ("/"); backslash is kept as an ordinary character
    if (out != NULL && outSize > 0)
        out[0] = 0;
    if (name == NULL || name[0] == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        strchr(name, '/') != NULL)
        return false;
    char b[MAX_PATH];
    lstrcpyn(b, (base && *base) ? base : "/", MAX_PATH);
    int bl = (int)strlen(b);
    if (bl > 0 && b[bl - 1] == '/')
        b[bl - 1] = 0; // remove trailing slash (root "/" -> "" -> joins to "/name")
    if (_snprintf_s(out, outSize, _TRUNCATE, "%s/%s", b, name) < 0)
    {
        out[0] = 0; // too long
        return false;
    }
    return true;
}

bool LocalJoinName(const char* base, const char* name, char* out, int outSize)
{
    // 'name' is ONE literal local path component taken from an untrusted server: rejected (never
    // sanitized) when it could escape the directory, address a device / stream or be altered by Win32
    if (out != NULL && outSize > 0)
        out[0] = 0;
    if (out == NULL || outSize <= 0 || base == NULL || name == NULL || name[0] == 0 ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    for (const char* p = name; *p != 0; p++)
    {
        unsigned char c = (unsigned char)*p; // char is unsigned under /J, but do not rely on it
        if (c < 0x20 || strchr("\\/:*?\"<>|", c) != NULL)
            return false;
    }
    size_t nl = strlen(name);
    if (name[nl - 1] == ' ' || name[nl - 1] == '.')
        return false;
    // reserved device name: part before the first '.' (trailing spaces ignored), e.g. "con.txt", "NUL "
    char devBase[8];
    size_t bl = 0;
    while (name[bl] != 0 && name[bl] != '.')
        bl++;
    while (bl > 0 && name[bl - 1] == ' ')
        bl--;
    if (bl == 3 || bl == 4)
    {
        memcpy(devBase, name, bl);
        devBase[bl] = 0;
        _strupr_s(devBase, sizeof(devBase));
        if (bl == 3 && (strcmp(devBase, "CON") == 0 || strcmp(devBase, "PRN") == 0 ||
                        strcmp(devBase, "AUX") == 0 || strcmp(devBase, "NUL") == 0))
            return false;
        if (bl == 4 && (strncmp(devBase, "COM", 3) == 0 || strncmp(devBase, "LPT", 3) == 0))
        {
            unsigned char d = (unsigned char)devBase[3]; // digit or superscript 1/2/3 (CP1252 0xB9 0xB2 0xB3)
            if ((d >= '0' && d <= '9') || d == 0xB9 || d == 0xB2 || d == 0xB3)
                return false;
        }
    }
    size_t basel = strlen(base);
    bool sep = basel > 0 && base[basel - 1] != '\\';
    if (basel + (sep ? 1 : 0) + nl + 1 > (size_t)outSize)
        return false;
    memcpy(out, base, basel);
    if (sep)
        out[basel++] = '\\';
    memcpy(out + basel, name, nl + 1);
    return true;
}

void SftpParent(const char* path, char* out, int outSize)
{
    lstrcpyn(out, (path && *path) ? path : "/", outSize);
    char* slash = strrchr(out, '/');
    if (slash == out)
        *(slash + 1) = 0; // keep root "/"
    else if (slash != NULL)
        *slash = 0;
}

// Typed-path rule: a remote path part that STARTS with '\' is a Windows-style typed path (e.g.
// "sftp:\\host\home\u"), so all its '\' become '/'; otherwise '\' is a literal name character
// (panel-generated paths always start with '/').
void SftpFixTypedPath(char* path)
{
    if (path == NULL || path[0] != '\\')
        return;
    for (char* s = path; *s; s++)
        if (*s == '\\')
            *s = '/';
}

// "//user@host/path" (either slash kind) -> path part after the host, typed-path rule applied;
// input without host prefix is treated as the path part. Empty path after host -> "/".
void SftpRemotePart(const char* userPart, char* out, int outSize)
{
    const char* p = (userPart != NULL) ? userPart : "";
    if ((p[0] == '/' || p[0] == '\\') && (p[1] == '/' || p[1] == '\\'))
    {
        p += 2;
        while (*p != 0 && *p != '/' && *p != '\\')
            p++;
        if (*p == 0)
            p = "/";
    }
    lstrcpyn(out, p, outSize);
    SftpFixTypedPath(out);
}

bool SftpIsSamePath(const char* a, const char* b)
{
    char na[MAX_PATH], nb[MAX_PATH];
    lstrcpyn(na, a ? a : "", MAX_PATH);
    lstrcpyn(nb, b ? b : "", MAX_PATH);
    SftpNormalize(na);
    SftpNormalize(nb);
    return strcmp(na, nb) == 0; // SFTP is case-sensitive
}

bool SftpIsRoot(const char* path)
{
    char n[MAX_PATH];
    lstrcpyn(n, path ? path : "", MAX_PATH);
    SftpNormalize(n);
    return strcmp(n, "/") == 0;
}

void WrapCommandWithSftpServerPrefix(const char* sftpServer, const char* rawCmd, char* outBuf, size_t outSize)
{
    if (sftpServer == NULL || sftpServer[0] == 0)
    {
        lstrcpyn(outBuf, rawCmd, (int)outSize);
        return;
    }

    const char* sftpPos = strstr(sftpServer, "sftp-server");
    if (sftpPos == NULL)
    {
        lstrcpyn(outBuf, rawCmd, (int)outSize);
        return;
    }

    const char* p = sftpPos;
    while (p > sftpServer && *(p - 1) != ' ' && *(p - 1) != '\t')
        p--;

    std::string prefix(sftpServer, p - sftpServer);
    while (!prefix.empty() && (prefix.back() == ' ' || prefix.back() == '\t'))
        prefix.pop_back();

    if (prefix.empty())
    {
        lstrcpyn(outBuf, rawCmd, (int)outSize);
        return;
    }

    std::string escaped;
    for (const char* c = rawCmd; *c; c++)
    {
        if (*c == '\'')
            escaped += "'\\''";
        else
            escaped += *c;
    }

    if (prefix.length() >= 2 && prefix.substr(prefix.length() - 2) == "-c")
    {
        _snprintf_s(outBuf, outSize, _TRUNCATE, "%s '%s'", prefix.c_str(), escaped.c_str());
    }
    else
    {
        _snprintf_s(outBuf, outSize, _TRUNCATE, "%s sh -c '%s'", prefix.c_str(), escaped.c_str());
    }
}

void SftpTraceLog(const char* fmt, ...)
{
    static FILE* f = nullptr;
    static bool triedOpen = false;
    if (!triedOpen)
    {
        triedOpen = true;
        const char* localApp = getenv("LOCALAPPDATA");
        char path[MAX_PATH];
        if (localApp && localApp[0])
            _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\Open Salamander\\sftp_trace.log", localApp);
        else
            strcpy(path, "sftp_trace.log");
        fopen_s(&f, path, "a");
    }
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    BOOL heapOk = HeapValidate(GetProcessHeap(), 0, NULL);
    char full[1280];
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(full, sizeof(full), _TRUNCATE, "[%02d:%02d:%02d.%03d] [%s] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                heapOk ? "HEAP_OK" : "HEAP_CORRUPT!", buf);

    OutputDebugStringA(full);
    if (f)
    {
        fputs(full, f);
        fflush(f);
    }
    if (!heapOk)
    {
        MessageBoxA(NULL, full, "SFTP CRITICAL HEAP CORRUPTION", MB_OK | MB_ICONERROR);
    }
}

void SftpCheckHeap(const char* where)
{
    SftpTraceLog("HeapCheck: %s", where);
}
