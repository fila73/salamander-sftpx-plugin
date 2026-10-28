// Copyright © 2026 Dupl3xx
//
// Glue between Salamander FS and CSftpConnection + POSIX path helpers.
#pragma once
#include "sftpconn.h"

// Connection profile specified in dialog.
struct CSftpProfile
{
    char Name[128];      // connection profile name (e.g. "NAS"), empty if ad-hoc
    char Host[256];
    int Port;
    char User[128];
    char Password[256];
    char KeyFile[260]; // private key (optional)
    char Path[260];
    char SftpServer[260]; // custom sftp-server command/path (optional, e.g. sudo su -c /usr/lib/openssh/sftp-server)
    bool UseCompression; // zlib compression
    int Protocol;        // 0 = SFTP, 1 = SCP
    bool ScpFallback;    // fallback to SCP on SFTP failure
    bool ExecOnEnter;    // execute (+x) files on server on Enter
    bool Valid;
};

// Saved server (profile in connection manager).
#define SFTP_MAX_PROFILES 100
struct CSftpSavedProfile
{
    char Name[128]; // display name in list
    char Host[256];
    int Port;
    char User[128];
    char Password[256];
    char KeyFile[260];
    char Path[260];
    char SftpServer[260];
    bool UseCompression; // zlib compression
    int Protocol;        // 0 = SFTP, 1 = SCP
    bool ScpFallback;    // fallback to SCP on SFTP failure
    bool ExecOnEnter;    // execute (+x) files on server on Enter
    char Folder[128];    // folder group name (empty = root)
};
extern CSftpSavedProfile SftpProfiles[SFTP_MAX_PROFILES];
extern int SftpProfileCount;
extern char SftpDefaultSession[128]; // name of profile to auto-fill in connect dialog

inline void SftpProfileFromSaved(CSftpProfile& dst, const CSftpSavedProfile& src)
{
    lstrcpynA(dst.Name, src.Name, sizeof(dst.Name));
    lstrcpynA(dst.Host, src.Host, sizeof(dst.Host));
    dst.Port = src.Port;
    lstrcpynA(dst.User, src.User, sizeof(dst.User));
    lstrcpynA(dst.Password, src.Password, sizeof(dst.Password));
    lstrcpynA(dst.KeyFile, src.KeyFile, sizeof(dst.KeyFile));
    lstrcpynA(dst.Path, src.Path, sizeof(dst.Path));
    lstrcpynA(dst.SftpServer, src.SftpServer, sizeof(dst.SftpServer));
    dst.UseCompression = src.UseCompression;
    dst.Protocol = src.Protocol;
    dst.ScpFallback = src.ScpFallback;
    dst.ExecOnEnter = src.ExecOnEnter;
    dst.Valid = true;
}

#define SFTP_MAX_FOLDERS 64
extern char SftpFolders[SFTP_MAX_FOLDERS][128]; // folder names (including empty)
extern int SftpFolderCount;

// Ensures connection per CSftpProfile. Returns TRUE if connected.
bool SftpEnsureConnected(HWND parent, CSftpConnection& conn, CSftpProfile& profile);

extern int SftpEncoding; // 0 = Auto/UTF-8, 1 = UTF-8, 2 = Off

// POSIX path helpers (forward slashes, absolute paths start with "/").
void SftpNormalize(char* path);                       // in-place normalize
void SftpJoin(const char* base, const char* name, char* out, int outSize); // USER-typed path, normalized
// Joins a single literal name (from a server listing or panel item). No normalization; returns false
// (out = "") for empty, ".", "..", names containing "/" and too long results. Callers must skip the entry.
bool SftpJoinName(const char* base, const char* name, char* out, int outSize);
void SftpParent(const char* path, char* out, int outSize); // up-dir
void SftpFixTypedPath(char* path); // path starting with backslash = Windows-style typed path: backslash -> '/'; else literal
void SftpRemotePart(const char* userPart, char* out, int outSize); // strip //host, apply the rule above
bool SftpIsSamePath(const char* a, const char* b);
bool SftpIsRoot(const char* path);

// Simple input dialog (for keyboard-interactive prompts). echo=show text.
// Returns true if user confirmed, false on cancel.
bool SftpInputDialog(HWND parent, const char* prompt, bool echo, char* out, int outSize);

class CPluginFSInterface;

// Plugin menu commands (bypass Salamander 5.0 kernel limits).
void SftpEditFile(HWND parent, CPluginFSInterface* fs, const char* remoteDir, const char* fileName);
void SftpSyncDir(HWND parent, CPluginFSInterface* fs, const char* remoteDir, const char* localDir, int direction);
void SftpCalcSize(HWND parent, CPluginFSInterface* fs, const char* remoteDir, int panel);

// Save configuration immediately to registry
void SaveSftpConfigurationImmediately(HWND parent);

// Wrap command line execution with custom sftp-server prefix (e.g. sudo -u hop)
void WrapCommandWithSftpServerPrefix(const char* sftpServer, const char* rawCmd, char* outBuf, size_t outSize);

// Diagnostic trace logging and heap validation
void SftpTraceLog(const char* fmt, ...);
void SftpCheckHeap(const char* where);
