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
#define SFTP_MAX_ENCPASSWORD 1024 // max size of the encrypted password blob (the password itself is max 255 chars)
struct CSftpSavedProfile
{
    char Name[128]; // display name in list
    char Host[256];
    int Port;
    char User[128];
    BYTE EncPassword[SFTP_MAX_ENCPASSWORD]; // blob from Salamander password manager (AES or scrambled), never plaintext
    int EncPasswordSize;                    // 0 = no stored password
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

// Password manager helpers (main thread only; background threads use the plaintext that was
// already copied into CSftpProfile). 'parent' is the owner of the master password prompt.
// Decrypts the stored password into 'out'; asks for the master password if needed and 'allowAsk'.
// Returns false if a stored password exists but cannot be decrypted (MP refused/wrong); 'out' is
// then empty. No stored password -> true with empty 'out'. Caller must SecureZeroMemory 'out'.
bool SftpSavedGetPassword(HWND parent, CSftpSavedProfile& src, char* out, int outSize, bool allowAsk);
// Stores 'plain' as encrypted blob (AES if master password is in use, else scrambled).
// Returns false (blob unchanged) if the master password is needed but was not entered.
bool SftpSavedSetPassword(HWND parent, CSftpSavedProfile& dst, const char* plain);
// True if the stored password decrypts (never asks for the master password) and equals 'plain'.
bool SftpSavedPasswordEquals(CSftpSavedProfile& src, const char* plain);
// Scrambles a legacy plaintext registry password into the blob (needs no master password).
void SftpSavedSetLegacyPassword(CSftpSavedProfile& dst, const char* legacyPlain);
// PME_MASTERPASSWORD* handling: converts all blobs; returns false if some could not be decrypted.
bool SftpSavedReencryptAll(HWND parent, int event);

// Fills connection profile from the saved one; decrypts the password (may ask for the master
// password; if refused the password stays empty and the normal authentication prompt is used).
void SftpProfileFromSaved(HWND parent, CSftpProfile& dst, CSftpSavedProfile& src);

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
// Same for a LOCAL Windows path: name is one component from an untrusted server (rejects ..,
// separators, ':' and other reserved chars, device names, trailing space/dot); out is cleared on failure
bool LocalJoinName(const char* base, const char* name, char* out, int outSize);
void SftpParent(const char* path, char* out, int outSize); // up-dir
void SftpFixTypedPath(char* path); // path starting with backslash = Windows-style typed path: backslash -> '/'; else literal
void SftpRemotePart(const char* userPart, char* out, int outSize); // strip //host, apply the rule above
bool SftpParseHostSpec(const char* userPart, char* user, int userSize, char* host, int hostSize, int* port);
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
