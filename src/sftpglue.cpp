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
    if (out == NULL || outSize <= 0 || base == NULL || base[0] == 0 || name == NULL || name[0] == 0 ||
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

// Parses the "//[user@]host[:port]" prefix of an address-bar path. Port defaults to 22 when absent
// or empty. An IPv6 literal is written "[addr]" / "[addr]:port" and returned without the brackets.
// Returns false (caller must not switch server) if there is no host, the port is not a number in
// 1..65535, or the user part contains ':' (user:password is not supported).
bool SftpParseHostSpec(const char* userPart, char* user, int userSize, char* host, int hostSize, int* port)
{
    user[0] = 0;
    host[0] = 0;
    *port = 22;
    if (userPart == NULL || (userPart[0] != '/' && userPart[0] != '\\') ||
        (userPart[1] != '/' && userPart[1] != '\\'))
        return false;
    const char* p = userPart + 2;
    const char* slash = p;
    while (*slash != 0 && *slash != '/' && *slash != '\\')
        slash++;
    int len = (int)(slash - p);
    char buf[320];
    if (len <= 0 || len >= (int)sizeof(buf))
        return false;
    memcpy(buf, p, len);
    buf[len] = 0;
    char* hostpart = buf;
    char* at = strrchr(buf, '@'); // the user name may contain '@'
    if (at != NULL)
    {
        *at = 0;
        if (strchr(buf, ':') != NULL)
            return false;
        lstrcpyn(user, buf, userSize);
        hostpart = at + 1;
    }
    char* colon;
    if (hostpart[0] == '[')
    {
        char* close = strchr(hostpart, ']');
        if (close == NULL || close == hostpart + 1 || (close[1] != 0 && close[1] != ':'))
            return false;
        *close = 0;
        if (strchr(hostpart + 1, ':') == NULL) // an IPv6 literal contains ':' (only look inside the brackets)
            return false;
        hostpart++; // drop '['
        colon = (close[1] == ':') ? close + 1 : NULL; // the ':' before the port, as in the plain case
    }
    else
        colon = strchr(hostpart, ':');
    if (colon != NULL)
    {
        *colon = 0;
        if (colon[1] != 0)
        {
            long v = 0;
            for (const char* d = colon + 1; *d != 0; d++)
            {
                if (*d < '0' || *d > '9' || (v = v * 10 + (*d - '0')) > 65535)
                    return false;
            }
            if (v <= 0)
                return false;
            *port = (int)v;
        }
    }
    lstrcpyn(host, hostpart, hostSize);
    return host[0] != 0;
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
        if (SalamanderGeneral != NULL)
            SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), full,
                                             "SFTP CRITICAL HEAP CORRUPTION", MB_OK | MB_ICONERROR);
        else
            MessageBoxA(NULL, full, "SFTP CRITICAL HEAP CORRUPTION", MB_OK | MB_ICONERROR);
    }
}

void SftpCheckHeap(const char* where)
{
    SftpTraceLog("HeapCheck: %s", where);
}

// ---------------------------------------------------------------------------
// Saved profile passwords (Salamander password manager). Main thread only.
// CSftpSavedProfile (registry/dialog list) keeps only the encrypted blob. The runtime
// CSftpProfile still holds the decrypted password while a connection uses it; the helpers
// here zero their own temporary buffers after use.

// stores a blob returned by EncryptPassword() into the profile; frees the Salamander buffer
static bool SftpStoreBlob(CSftpSavedProfile& dst, BYTE* blob, int blobSize)
{
    bool ok = blob != NULL && blobSize > 0 && blobSize <= SFTP_MAX_ENCPASSWORD;
    if (ok)
    {
        memcpy(dst.EncPassword, blob, blobSize);
        dst.EncPasswordSize = blobSize;
    }
    if (blob != NULL) // release the buffer allocated in EncryptPassword()
    {
        SecureZeroMemory(blob, blobSize);
        SalamanderGeneral->Free(blob);
    }
    return ok;
}

bool SftpSavedSetPassword(HWND parent, CSftpSavedProfile& dst, const char* plain)
{
    if (plain == NULL || plain[0] == 0)
    {
        dst.EncPasswordSize = 0;
        return true;
    }
    CSalamanderPasswordManagerAbstract* pm = SalamanderGeneral->GetSalamanderPasswordManager();
    BOOL aes = pm->IsUsingMasterPassword();
    if (aes && !pm->IsMasterPasswordSet() && !pm->AskForMasterPassword(parent))
        return false; // AES needs the master password; keep the old blob
    BYTE* blob = NULL;
    int blobSize = 0;
    if (!pm->EncryptPassword(plain, &blob, &blobSize, aes))
        return false;
    return SftpStoreBlob(dst, blob, blobSize);
}

bool SftpSavedPasswordEquals(CSftpSavedProfile& src, const char* plain)
{
    if (src.EncPasswordSize <= 0 || plain == NULL)
        return false;
    char cur[256];
    bool same = SftpSavedGetPassword(NULL, src, cur, sizeof(cur), false) && strcmp(cur, plain) == 0;
    SecureZeroMemory(cur, sizeof(cur));
    return same;
}

void SftpSavedSetLegacyPassword(CSftpSavedProfile& dst, const char* legacyPlain)
{
    // legacy plaintext registry value: scramble only (no master password needed during load)
    CSalamanderPasswordManagerAbstract* pm = SalamanderGeneral->GetSalamanderPasswordManager();
    BYTE* blob = NULL;
    int blobSize = 0;
    if (legacyPlain != NULL && legacyPlain[0] != 0 && pm->EncryptPassword(legacyPlain, &blob, &blobSize, FALSE))
        SftpStoreBlob(dst, blob, blobSize);
}

bool SftpSavedGetPassword(HWND parent, CSftpSavedProfile& src, char* out, int outSize, bool allowAsk)
{
    out[0] = 0;
    if (src.EncPasswordSize <= 0)
        return true;
    CSalamanderPasswordManagerAbstract* pm = SalamanderGeneral->GetSalamanderPasswordManager();
    if (pm->IsPasswordEncrypted(src.EncPassword, src.EncPasswordSize) &&
        pm->IsUsingMasterPassword() && !pm->IsMasterPasswordSet())
    {
        if (!allowAsk || !pm->AskForMasterPassword(parent))
            return false; // refused: stored blob stays untouched
    }
    char* plain = NULL;
    if (!pm->DecryptPassword(src.EncPassword, src.EncPasswordSize, &plain) || plain == NULL)
        return false; // wrong master password / corrupted blob: stored blob stays untouched
    lstrcpynA(out, plain, outSize);
    SecureZeroMemory(plain, lstrlenA(plain));
    SalamanderGeneral->Free(plain);
    // upgrade a scrambled blob to AES once the master password is known
    if (out[0] != 0 && pm->IsUsingMasterPassword() && pm->IsMasterPasswordSet() &&
        !pm->IsPasswordEncrypted(src.EncPassword, src.EncPasswordSize))
        SftpSavedSetPassword(parent, src, out);
    return true;
}

bool SftpSavedReencryptAll(HWND parent, int event)
{
    bool allOk = true;
    CSalamanderPasswordManagerAbstract* pm = SalamanderGeneral->GetSalamanderPasswordManager();
    for (int i = 0; i < SftpProfileCount; i++)
    {
        CSftpSavedProfile& p = SftpProfiles[i];
        if (p.EncPasswordSize <= 0)
            continue;
        char* plain = NULL;
        // AES -> plain -> scrambled (for created/changed/removed master password)
        if (pm->IsPasswordEncrypted(p.EncPassword, p.EncPasswordSize))
        {
            if (pm->DecryptPassword(p.EncPassword, p.EncPasswordSize, &plain) && plain != NULL)
            {
                BYTE* blob = NULL;
                int blobSize = 0;
                if (plain[0] == 0)
                    p.EncPasswordSize = 0;
                else if (!pm->EncryptPassword(plain, &blob, &blobSize, FALSE) || !SftpStoreBlob(p, blob, blobSize))
                    allOk = false; // blob left as it was (AES), warn the user
                SecureZeroMemory(plain, lstrlenA(plain));
                SalamanderGeneral->Free(plain);
            }
            else
            {
                allOk = false; // encrypted with another master password, keep the blob as-is
                continue;
            }
        }
        // scrambled -> plain -> AES (the master password is being created or changed)
        if (event == PME_MASTERPASSWORDCREATED || event == PME_MASTERPASSWORDCHANGED)
        {
            plain = NULL;
            if (p.EncPasswordSize > 0 &&
                pm->DecryptPassword(p.EncPassword, p.EncPasswordSize, &plain) && plain != NULL)
            {
                SftpSavedSetPassword(parent, p, plain);
                SecureZeroMemory(plain, lstrlenA(plain));
                SalamanderGeneral->Free(plain);
            }
        }
    }
    return allOk;
}

void SftpProfileFromSaved(HWND parent, CSftpProfile& dst, CSftpSavedProfile& src)
{
    lstrcpynA(dst.Name, src.Name, sizeof(dst.Name));
    lstrcpynA(dst.Host, src.Host, sizeof(dst.Host));
    dst.Port = src.Port;
    lstrcpynA(dst.User, src.User, sizeof(dst.User));
    // if the master password is refused the password stays empty -> normal authentication prompt
    SftpSavedGetPassword(parent, src, dst.Password, sizeof(dst.Password), true);
    lstrcpynA(dst.KeyFile, src.KeyFile, sizeof(dst.KeyFile));
    lstrcpynA(dst.Path, src.Path, sizeof(dst.Path));
    lstrcpynA(dst.SftpServer, src.SftpServer, sizeof(dst.SftpServer));
    dst.UseCompression = src.UseCompression;
    dst.Protocol = src.Protocol;
    dst.ScpFallback = src.ScpFallback;
    dst.ExecOnEnter = src.ExecOnEnter;
    dst.Valid = true;
}
