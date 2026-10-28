// Copyright © 2026 Dupl3xx
#include "sftpconn.h"
#include "sftpglue.h"
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef NO_OPENSSL
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#endif

CSftpConnection::HostKeyVerifyFn CSftpConnection::HostKeyCb = nullptr;
void* CSftpConnection::HostKeyCtx = nullptr;
CSftpConnection::KbdPromptFn CSftpConnection::KbdCb = nullptr;
void* CSftpConnection::KbdCtx = nullptr;
std::string CSftpConnection::KnownHostsFile;
int CSftpConnection::Encoding = 0;

void CSftpConnection::SetKnownHostsFile(const char* path)
{
    KnownHostsFile = (path != nullptr) ? path : "";
}

std::string CSftpConnection::ToServerEnc(const char* in)
{
    if (Encoding == 2 || in == nullptr || in[0] == 0)
        return (in != nullptr) ? in : "";
    wchar_t w[2 * MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, in, -1, w, 2 * MAX_PATH) == 0)
        return in;
    char out[4 * MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof(out), nullptr, nullptr);
    return out;
}

std::string CSftpConnection::ToDisplayEnc(const char* in)
{
    if (Encoding == 2 || in == nullptr || in[0] == 0)
        return (in != nullptr) ? in : "";
    wchar_t w[2 * MAX_PATH];
    // only convert valid UTF-8; otherwise keep as-is (server may return other encodings)
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in, -1, w, 2 * MAX_PATH) == 0)
        return in;
    char out[4 * MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, w, -1, out, sizeof(out), nullptr, nullptr);
    return out;
}

static std::string ShellQuote(const char* s); // definition below (with SCP helpers)

bool CSftpConnection::ReportProgress(const char* name, unsigned __int64 done, unsigned __int64 total)
{
    if (Progress == nullptr)
        return true;
    return Progress(ProgressCtx, name, done, total);
}

CSftpConnection::CSftpConnection()
    : ScpMode(false), Sock(INVALID_SOCKET), Session(nullptr), Sftp(nullptr), Progress(nullptr), ProgressCtx(nullptr) {}

CSftpConnection::~CSftpConnection() { Disconnect(); }

bool CSftpConnection::GlobalInit()
{
    SftpTraceLog("CSftpConnection::GlobalInit: start");
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        SftpTraceLog("CSftpConnection::GlobalInit: WSAStartup failed");
        return false;
    }
#ifndef NO_OPENSSL
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                        OPENSSL_INIT_ADD_ALL_CIPHERS |
                        OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
#endif
    SftpTraceLog("CSftpConnection::GlobalInit: after WSAStartup, before libssh2_init");
    int rc = libssh2_init(0);
    SftpTraceLog("CSftpConnection::GlobalInit: after libssh2_init rc=%d", rc);
    return rc == 0;
}

void CSftpConnection::GlobalExit()
{
    SftpTraceLog("CSftpConnection::GlobalExit: calling libssh2_exit and WSACleanup");
    libssh2_exit();
#ifndef NO_OPENSSL
    OPENSSL_thread_stop();
#endif
    WSACleanup();
}

void CSftpConnection::SetError(const char* ctx)
{
    char buf[512];
    unsigned long sftpErr = 0;
    if (Sftp)
        sftpErr = libssh2_sftp_last_error(Sftp);

    const char* sftpDesc = nullptr;
    switch (sftpErr)
    {
    case LIBSSH2_FX_EOF: sftpDesc = "End of file"; break;
    case LIBSSH2_FX_NO_SUCH_FILE: sftpDesc = "No such file or directory"; break;
    case LIBSSH2_FX_PERMISSION_DENIED: sftpDesc = "Permission denied"; break;
    case LIBSSH2_FX_FAILURE: sftpDesc = "Generic failure"; break;
    case LIBSSH2_FX_BAD_MESSAGE: sftpDesc = "Bad message"; break;
    case LIBSSH2_FX_NO_CONNECTION: sftpDesc = "No connection"; break;
    case LIBSSH2_FX_CONNECTION_LOST: sftpDesc = "Connection lost"; break;
    case LIBSSH2_FX_OP_UNSUPPORTED: sftpDesc = "Operation unsupported"; break;
    case LIBSSH2_FX_INVALID_HANDLE: sftpDesc = "Invalid handle"; break;
    case LIBSSH2_FX_NO_SUCH_PATH: sftpDesc = "No such path"; break;
    case LIBSSH2_FX_FILE_ALREADY_EXISTS: sftpDesc = "File already exists"; break;
    case LIBSSH2_FX_WRITE_PROTECT: sftpDesc = "Write protect"; break;
    case LIBSSH2_FX_NO_MEDIA: sftpDesc = "No media"; break;
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: sftpDesc = "No space on filesystem"; break;
    case LIBSSH2_FX_QUOTA_EXCEEDED: sftpDesc = "Quota exceeded"; break;
    case LIBSSH2_FX_UNKNOWN_PRINCIPAL: sftpDesc = "Unknown principal"; break;
    case LIBSSH2_FX_LOCK_CONFLICT: sftpDesc = "Lock conflict"; break;
    case LIBSSH2_FX_DIR_NOT_EMPTY: sftpDesc = "Directory not empty"; break;
    case LIBSSH2_FX_NOT_A_DIRECTORY: sftpDesc = "Not a directory"; break;
    }

    if (sftpDesc != nullptr)
    {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s: %s (SFTP %lu)", ctx, sftpDesc, sftpErr);
    }
    else if (Session)
    {
        char* msg = nullptr;
        int len = 0;
        libssh2_session_last_error(Session, &msg, &len, 0);
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s: %s", ctx, msg ? msg : "(unknown error)");
    }
    else
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s", ctx);
    ErrorMsg = buf;
    SftpTraceLog("CSftpConnection::SetError: %s", ErrorMsg.c_str());
}

bool CSftpConnection::Connect(const char* host, int port, const char* user, const char* password, const char* keyFile,
                              bool useCompression, int protocol, bool scpFallback, const char* sftpServer)
{
    SftpTraceLog("CSftpConnection::Connect: start host=%s port=%d user=%s", host ? host : "", port, user ? user : "");
    Disconnect();
    ScpMode = false;

    struct addrinfo hints = {0}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    _snprintf_s(portstr, sizeof(portstr), _TRUNCATE, "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
    {
        ErrorMsg = "Cannot resolve hostname.";
        SftpTraceLog("CSftpConnection::Connect: Cannot resolve hostname");
        return false;
    }
    SftpTraceLog("CSftpConnection::Connect: hostname resolved");
    Sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (Sock == INVALID_SOCKET || connect(Sock, res->ai_addr, (int)res->ai_addrlen) != 0)
    {
        ErrorMsg = "Cannot establish TCP connection.";
        SftpTraceLog("CSftpConnection::Connect: TCP connect failed");
        freeaddrinfo(res);
        Disconnect();
        return false;
    }
    freeaddrinfo(res);
    SftpTraceLog("CSftpConnection::Connect: TCP connected, sock=%llu", (unsigned long long)Sock);

    // Enable TCP keepalive to prevent NAT / firewall timeouts during idle
    BOOL optval = TRUE;
    setsockopt(Sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(optval));
    setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval, sizeof(optval));
    struct tcp_keepalive ka;
    ka.onoff = 1;
    ka.keepalivetime = 15000;    // send probe after 15s idle
    ka.keepaliveinterval = 5000; // probe interval 5s
    DWORD bytesReturned = 0;
    WSAIoctl(Sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), NULL, 0, &bytesReturned, NULL, NULL);
    SftpTraceLog("CSftpConnection::Connect: keepalive configured");

    Session = libssh2_session_init();
    if (!Session) { ErrorMsg = "libssh2_session_init failed."; SftpTraceLog("CSftpConnection::Connect: libssh2_session_init failed"); Disconnect(); return false; }
    libssh2_session_set_blocking(Session, 1);
    libssh2_session_set_timeout(Session, 60000); // 60s timeout for socket operations
    SftpTraceLog("CSftpConnection::Connect: libssh2_session_init ok, session=%p", Session);

    // optional zlib compression (prefer, but allow without compression)
    if (useCompression)
    {
        libssh2_session_method_pref(Session, LIBSSH2_METHOD_COMP_CS, "zlib@openssh.com,zlib,none");
        libssh2_session_method_pref(Session, LIBSSH2_METHOD_COMP_SC, "zlib@openssh.com,zlib,none");
    }

    SftpTraceLog("CSftpConnection::Connect: calling libssh2_session_handshake");
    if (libssh2_session_handshake(Session, Sock)) { SetError("SSH handshake"); Disconnect(); return false; }
    SftpTraceLog("CSftpConnection::Connect: libssh2_session_handshake ok");

    // Enable SSH keepalive (every 10s, want_reply = 0 to avoid SSH_MSG_REQUEST_FAILURE from OpenSSH)
    libssh2_keepalive_config(Session, 0, 10);

    // host key verification (known_hosts + user prompt)
    SftpTraceLog("CSftpConnection::Connect: calling VerifyHostKey");
    if (!VerifyHostKey(host, port)) { Disconnect(); return false; }
    SftpTraceLog("CSftpConnection::Connect: VerifyHostKey ok");

    // authentication: key / password / keyboard-interactive
    if (!Authenticate(user, password, keyFile)) { Disconnect(); return false; }

    SftpServerCmd = (sftpServer && *sftpServer) ? sftpServer : "";

    if (protocol == 1)
    {
        // explicit SCP - do not use SFTP subsystem
        ScpMode = true;
        return true;
    }
    if (sftpServer && *sftpServer)
    {
        Sftp = libssh2_sftp_init_ex(Session, "exec", sftpServer, (unsigned int)strlen(sftpServer));
    }
    else
        Sftp = libssh2_sftp_init(Session);

    if (!Sftp)
    {
        if (scpFallback)
        {
            // server has no SFTP subsystem -> fallback to SCP
            ScpMode = true;
            return true;
        }
        ErrorMsg = "Server does not support SFTP subsystem or failed to start custom SFTP server.\nSelect SCP protocol or enable the \"Allow fallback SCP\" option.";
        Disconnect();
        return false;
    }
    return true;
}

void CSftpConnection::Disconnect()
{
    ScpMode = false;
    SftpServerCmd.clear();
    if (Sftp) { libssh2_sftp_shutdown(Sftp); Sftp = nullptr; }
    if (Session)
    {
        libssh2_session_disconnect(Session, "Goodbye");
        libssh2_session_free(Session);
        Session = nullptr;
    }
    if (Sock != INVALID_SOCKET) { closesocket(Sock); Sock = INVALID_SOCKET; }
}

bool CSftpConnection::SendKeepalive()
{
    if (Sock == INVALID_SOCKET || Session == nullptr)
        return false;

    // Send keepalive packet (keeps SSH session active on server side like TrueNAS ClientAliveInterval)
    int seconds_to_next = 0;
    int rc = libssh2_keepalive_send(Session, &seconds_to_next);
    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN)
        return false;

    return true;
}

bool CSftpConnection::IsConnected() const
{
    if (Sock == INVALID_SOCKET || Session == nullptr)
        return false;
    if (!ScpMode && Sftp == nullptr)
        return false;

    // Fast non-blocking check if socket has been closed remotely (FIN/RST) or encountered errors
    fd_set rfd, efd;
    FD_ZERO(&rfd);
    FD_ZERO(&efd);
    FD_SET(Sock, &rfd);
    FD_SET(Sock, &efd);
    timeval tv = {0, 0};
    int sel = select((int)Sock + 1, &rfd, NULL, &efd, &tv);
    if (sel > 0)
    {
        if (FD_ISSET(Sock, &efd))
            return false;
        if (FD_ISSET(Sock, &rfd))
        {
            char peekBuf[1];
            int r = recv(Sock, peekBuf, 1, MSG_PEEK);
            if (r == 0)
                return false; // Connection closed gracefully by peer (FIN)
            if (r < 0)
            {
                int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK)
                    return false; // Connection reset or socket error
            }
        }
    }
    else if (sel < 0)
    {
        return false;
    }

    // Send keepalive packet if interval has elapsed (keeps SSH session active and verifies transport)
    int rc = libssh2_keepalive_send(Session, nullptr);
    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN)
        return false;

    return true;
}

// ===================== Host key verification (known_hosts) =====================

// maps libssh2 key type to bit for known_hosts (LIBSSH2_KNOWNHOST_KEY_*)
static int HostKeyBit(int ktype)
{
    switch (ktype)
    {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
#endif
    default: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    }
}

static const char* HostKeyTypeName(int ktype)
{
    switch (ktype)
    {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return "RSA";
    case LIBSSH2_HOSTKEY_TYPE_DSS: return "DSS";
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return "ECDSA";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return "ED25519";
#endif
    default: return "unknown";
    }
}

bool CSftpConnection::VerifyHostKey(const char* host, int port)
{
    size_t klen = 0;
    int ktype = 0;
    const char* hk = libssh2_session_hostkey(Session, &klen, &ktype);
    if (hk == nullptr) { ErrorMsg = "Cannot retrieve server host key."; return false; }

    // SHA256 fingerprint for display to user
    char fpstr[128];
    const unsigned char* fp = (const unsigned char*)libssh2_hostkey_hash(Session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (fp != nullptr)
    {
        char* p = fpstr;
        for (int i = 0; i < 32; i++)
            p += sprintf_s(p, 4, "%02x%s", fp[i], i < 31 ? ":" : "");
    }
    else
        lstrcpynA(fpstr, "(unknown)", sizeof(fpstr));

    int keybit = HostKeyBit(ktype);
    int status = 0; // 0 = unknown, 1 = changed

    LIBSSH2_KNOWNHOSTS* kh = libssh2_knownhost_init(Session);
    if (kh != nullptr && !KnownHostsFile.empty())
        libssh2_knownhost_readfile(kh, KnownHostsFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    struct libssh2_knownhost* khentry = nullptr;
    int check = LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;
    if (kh != nullptr)
        check = libssh2_knownhost_checkp(kh, host, port, hk, klen,
                                         LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keybit,
                                         &khentry);

    if (check == LIBSSH2_KNOWNHOST_CHECK_MATCH)
    {
        if (kh != nullptr) libssh2_knownhost_free(kh);
        return true; // server is known and key matches
    }
    status = (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) ? 1 : 0;

    // ask user (without callback: trust unknown once, reject changed)
    int decision;
    if (HostKeyCb != nullptr)
        decision = HostKeyCb(HostKeyCtx, host, port, HostKeyTypeName(ktype), fpstr, status);
    else
        decision = (status == 1) ? 0 : 1;

    if (decision == 0)
    {
        ErrorMsg = (status == 1)
                       ? "Server host key has CHANGED - connection rejected (possible MITM attack)."
                       : "Server host key is not trusted - connection rejected.";
        if (kh != nullptr) libssh2_knownhost_free(kh);
        return false;
    }

    if (decision == 2 && kh != nullptr) // trust and save
    {
        if (khentry != nullptr) // overwrite changed entry
            libssh2_knownhost_del(kh, khentry);
        libssh2_knownhost_addc(kh, host, nullptr, hk, klen, nullptr, 0,
                               LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | keybit, nullptr);
        if (!KnownHostsFile.empty())
            libssh2_knownhost_writefile(kh, KnownHostsFile.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    }
    if (kh != nullptr) libssh2_knownhost_free(kh);
    return true;
}

// ===================== Authentication (password / key / keyboard-interactive) =====================

// data passed to KBI callback via session abstract
struct CKbdData
{
    const char* password;
    bool used;
    CSftpConnection::KbdPromptFn cb;
    void* cbctx;
};

static void KbdInteractiveThunk(const char* /*name*/, int /*name_len*/,
                                const char* /*instruction*/, int /*instruction_len*/,
                                int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                                LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract)
{
    CKbdData* d = (abstract != nullptr) ? (CKbdData*)*abstract : nullptr;
    for (int i = 0; i < num_prompts; i++)
    {
        char buf[1024];
        buf[0] = 0;
        bool got = false;
        if (d != nullptr && d->password != nullptr && !d->used)
        {
            // first prompt = typically password -> use stored password
            lstrcpynA(buf, d->password, sizeof(buf));
            d->used = true;
            got = true;
        }
        else if (d != nullptr && d->cb != nullptr)
        {
            std::string pr((const char*)prompts[i].text, prompts[i].length);
            got = d->cb(d->cbctx, pr.c_str(), prompts[i].echo != 0, buf, sizeof(buf));
        }
        if (got)
        {
            size_t l = strlen(buf);
            responses[i].text = (char*)malloc(l + 1);
            if (responses[i].text != nullptr)
            {
                memcpy(responses[i].text, buf, l + 1);
                responses[i].length = (unsigned int)l;
            }
        }
        else
        {
            responses[i].text = nullptr;
            responses[i].length = 0;
        }
    }
}

bool CSftpConnection::Authenticate(const char* user, const char* password, const char* keyFile)
{
    unsigned ulen = (unsigned)strlen(user);
    char* authlist = libssh2_userauth_list(Session, user, ulen);
    bool hasPwd = authlist != nullptr && strstr(authlist, "password") != nullptr;
    bool hasKbd = authlist != nullptr && strstr(authlist, "keyboard-interactive") != nullptr;

    // 1) key authentication (including .ppk)
    if (keyFile != nullptr && keyFile[0] != 0)
    {
        int ppk = TryPpkAuth(user, keyFile, password);
        if (ppk == 1) return true;
        if (ppk == 0) return false; // .ppk recognized but failed (error is set)
        // ppk == -1: not .ppk -> standard OpenSSH/PEM key
        if (libssh2_userauth_publickey_fromfile(Session, user, nullptr, keyFile,
                                                (password != nullptr && password[0] != 0) ? password : nullptr) == 0)
            return true;
        SetError("Key authentication");
        return false;
    }

    // 2) password
    if (authlist == nullptr || hasPwd)
    {
        if (libssh2_userauth_password(Session, user, password) == 0)
            return true;
    }

    // 3) keyboard-interactive (also as fallback after failed password)
    if (hasKbd || authlist == nullptr)
    {
        CKbdData d;
        d.password = (password != nullptr && password[0] != 0) ? password : nullptr;
        d.used = false;
        d.cb = KbdCb;
        d.cbctx = KbdCtx;
        void** abstract = libssh2_session_abstract(Session);
        void* saved = (abstract != nullptr) ? *abstract : nullptr;
        if (abstract != nullptr) *abstract = &d;
        int rc = libssh2_userauth_keyboard_interactive(Session, user, &KbdInteractiveThunk);
        if (abstract != nullptr) *abstract = saved;
        if (rc == 0) return true;
    }

    SetError("Authentication failed");
    return false;
}

// ===================== PuTTY .ppk keys =====================

// read SSH string/mpint from blob (uint32 length + data); advance offset
static bool SshReadField(const std::vector<unsigned char>& blob, size_t& off,
                         const unsigned char** data, unsigned& len)
{
    if (off + 4 > blob.size()) return false;
    len = ((unsigned)blob[off] << 24) | ((unsigned)blob[off + 1] << 16) |
          ((unsigned)blob[off + 2] << 8) | (unsigned)blob[off + 3];
    off += 4;
    if (off + len > blob.size()) return false;
    *data = blob.data() + off;
    off += len;
    return true;
}

#ifndef NO_OPENSSL
// base64 decoding (OpenSSL)
static std::vector<unsigned char> B64Decode(const std::string& s)
{
    std::vector<unsigned char> out(s.size());
    int n = EVP_DecodeBlock((unsigned char*)out.data(), (const unsigned char*)s.data(), (int)s.size());
    if (n < 0) return std::vector<unsigned char>();
    // EVP_DecodeBlock aligns to 3 bytes; subtract padding '='
    size_t pad = 0;
    if (!s.empty() && s[s.size() - 1] == '=') pad++;
    if (s.size() >= 2 && s[s.size() - 2] == '=') pad++;
    out.resize((size_t)n - pad);
    return out;
}

// key derivation for encrypted PPK v2 (AES-256-CBC, SHA1)
static void Ppk2DeriveKey(const std::string& passphrase, unsigned char outKey[32])
{
    for (int seq = 0; seq < 2; seq++)
    {
        SHA_CTX c;
        SHA1_Init(&c);
        unsigned char idx[4] = {0, 0, 0, (unsigned char)seq};
        SHA1_Update(&c, idx, 4);
        SHA1_Update(&c, passphrase.data(), passphrase.size());
        unsigned char dig[20];
        SHA1_Final(dig, &c);
        memcpy(outKey + seq * 20, dig, seq == 0 ? 20 : 12);
    }
}

// key+IV derivation for PPK v3 (Argon2id) - returns false if OpenSSL lacks Argon2
static bool Ppk3DeriveKey(const std::string& passphrase, const std::vector<unsigned char>& salt,
                          unsigned mem, unsigned passes, unsigned par, unsigned char out[80])
{
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (kdf == nullptr) return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (ctx == nullptr) return false;
    OSSL_PARAM params[7];
    int i = 0;
    params[i++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, (void*)passphrase.data(), passphrase.size());
    params[i++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void*)salt.data(), salt.size());
    params[i++] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ARGON2_MEMCOST, &mem);
    params[i++] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ITER, &passes);
    params[i++] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_ARGON2_LANES, &par);
    unsigned threads = par;
    params[i++] = OSSL_PARAM_construct_uint(OSSL_KDF_PARAM_THREADS, &threads);
    params[i] = OSSL_PARAM_construct_end();
    int rc = EVP_KDF_derive(ctx, out, 80, params);
    EVP_KDF_CTX_free(ctx);
    return rc > 0;
}

int CSftpConnection::TryPpkAuth(const char* user, const char* keyFile, const char* passphrase)
{
    // load file
    FILE* f = nullptr;
    fopen_s(&f, keyFile, "rb");
    if (f == nullptr) return -1;
    std::string text;
    char rb[4096];
    size_t rn;
    while ((rn = fread(rb, 1, sizeof(rb), f)) > 0) text.append(rb, rn);
    fclose(f);

    if (text.compare(0, 20, "PuTTY-User-Key-File-") != 0)
        return -1; // not .ppk

    // simple parser for "Key: value" line headers + multi-line base64 sections
    int version = (text[20] == '3') ? 3 : 2;
    std::string algo, encryption, pubB64, privB64;
    std::string a2salt; unsigned a2mem = 0, a2pass = 0, a2par = 0;
    {
        size_t pos = 0;
        auto getline = [&](std::string& line) -> bool {
            if (pos >= text.size()) return false;
            size_t e = text.find('\n', pos);
            line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = (e == std::string::npos) ? text.size() : e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        };
        std::string line;
        while (getline(line))
        {
            size_t colon = line.find(": ");
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 2);
            if (key.compare(0, 20, "PuTTY-User-Key-File-") == 0) algo = val;
            else if (key == "Encryption") encryption = val;
            else if (key == "Argon2-Salt") a2salt = val;
            else if (key == "Argon2-Memory") a2mem = atoi(val.c_str());
            else if (key == "Argon2-Passes") a2pass = atoi(val.c_str());
            else if (key == "Argon2-Parallelism") a2par = atoi(val.c_str());
            else if (key == "Public-Lines" || key == "Private-Lines")
            {
                int n = atoi(val.c_str());
                std::string blob;
                for (int k = 0; k < n; k++) { std::string l2; if (getline(l2)) blob += l2; }
                if (key == "Public-Lines") pubB64 = blob; else privB64 = blob;
            }
        }
    }

    std::vector<unsigned char> pub = B64Decode(pubB64);
    std::vector<unsigned char> priv = B64Decode(privB64);
    if (pub.empty() || priv.empty()) { SetError("Invalid .ppk file"); return 0; }

    bool encrypted = (encryption == "aes256-cbc");
    if (encrypted)
    {
        if (passphrase == nullptr || passphrase[0] == 0)
        {
            ErrorMsg = ".ppk key is encrypted - enter the key passphrase in the Password field.";
            return 0;
        }
        unsigned char keyiv[80];
        unsigned char* aeskey = keyiv;
        unsigned char iv[16];
        memset(iv, 0, sizeof(iv));
        if (version == 3)
        {
            std::vector<unsigned char> salt = B64Decode(a2salt);
            if (!Ppk3DeriveKey(passphrase, salt, a2mem, a2pass, a2par ? a2par : 1, keyiv))
            {
                ErrorMsg = "Encrypted .ppk v3 (Argon2) - this version of OpenSSL does not support it. Convert the key with PuTTYgen to unencrypted or OpenSSH format.";
                return 0;
            }
            aeskey = keyiv;
            memcpy(iv, keyiv + 32, 16);
        }
        else
        {
            Ppk2DeriveKey(passphrase, keyiv); // 32 bytes key, IV zeros
            aeskey = keyiv;
        }
        // decrypt priv (AES-256-CBC, no padding)
        std::vector<unsigned char> dec(priv.size());
        EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
        int outl = 0, tmpl = 0;
        EVP_DecryptInit_ex(c, EVP_aes_256_cbc(), nullptr, aeskey, iv);
        EVP_CIPHER_CTX_set_padding(c, 0);
        EVP_DecryptUpdate(c, dec.data(), &outl, priv.data(), (int)priv.size());
        EVP_DecryptFinal_ex(c, dec.data() + outl, &tmpl);
        EVP_CIPHER_CTX_free(c);
        dec.resize(outl + tmpl);
        priv = dec;
    }

    // parse public blob: first field = algorithm name
    size_t po = 0;
    const unsigned char* d0;
    unsigned l0;
    if (!SshReadField(pub, po, &d0, l0)) { SetError("Invalid .ppk (public)"); return 0; }
    std::string keyalg((const char*)d0, l0);

    EVP_PKEY* pkey = nullptr;
    RSA* rsaForPem = nullptr;
    if (keyalg == "ssh-rsa")
    {
        const unsigned char *eD, *nD;
        unsigned eL, nL;
        if (!SshReadField(pub, po, &eD, eL) || !SshReadField(pub, po, &nD, nL)) { SetError("Invalid RSA .ppk"); return 0; }
        size_t pp = 0;
        const unsigned char *dD, *pD, *qD, *iD;
        unsigned dL, pL, qL, iL;
        // private blob: mpint d, p, q, iqmp
        if (!SshReadField(priv, pp, &dD, dL) || !SshReadField(priv, pp, &pD, pL) ||
            !SshReadField(priv, pp, &qD, qL) || !SshReadField(priv, pp, &iD, iL))
        { SetError("Invalid RSA .ppk (private)"); return 0; }
        BIGNUM* bn_e = BN_bin2bn(eD, eL, nullptr);
        BIGNUM* bn_n = BN_bin2bn(nD, nL, nullptr);
        BIGNUM* bn_d = BN_bin2bn(dD, dL, nullptr);
        BIGNUM* bn_p = BN_bin2bn(pD, pL, nullptr);
        BIGNUM* bn_q = BN_bin2bn(qD, qL, nullptr);
        BIGNUM* bn_iqmp = BN_bin2bn(iD, iL, nullptr);
        // compute CRT exponents: dmp1 = d mod (p-1), dmq1 = d mod (q-1)
        BN_CTX* bnctx = BN_CTX_new();
        BIGNUM* p1 = BN_dup(bn_p); BN_sub_word(p1, 1);
        BIGNUM* q1 = BN_dup(bn_q); BN_sub_word(q1, 1);
        BIGNUM* dmp1 = BN_new(); BN_mod(dmp1, bn_d, p1, bnctx);
        BIGNUM* dmq1 = BN_new(); BN_mod(dmq1, bn_d, q1, bnctx);
        BN_free(p1); BN_free(q1); BN_CTX_free(bnctx);
        RSA* rsa = RSA_new();
        RSA_set0_key(rsa, bn_n, bn_e, bn_d);          // takes ownership
        RSA_set0_factors(rsa, bn_p, bn_q);
        RSA_set0_crt_params(rsa, dmp1, dmq1, bn_iqmp);
        rsaForPem = rsa;
        pkey = EVP_PKEY_new();
        EVP_PKEY_assign_RSA(pkey, rsa);
    }
    else if (keyalg == "ssh-ed25519")
    {
        const unsigned char* pubKey;
        unsigned pubL;
        if (!SshReadField(pub, po, &pubKey, pubL)) { SetError("Invalid ed25519 .ppk"); return 0; }
        size_t pp = 0;
        const unsigned char* privKey;
        unsigned privL;
        if (!SshReadField(priv, pp, &privKey, privL) || privL < 32) { SetError("Invalid ed25519 .ppk (private)"); return 0; }
        pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, privKey, 32);
    }
    else
    {
        ErrorMsg = std::string("Unsupported .ppk key type: ") + keyalg + " (supported: ssh-rsa, ssh-ed25519).";
        return 0;
    }
    if (pkey == nullptr) { SetError("Cannot build key from .ppk"); return 0; }

    // export to PEM (PKCS#8) and pass to libssh2
    BIO* bio = BIO_new(BIO_s_mem());
    int wrote = (rsaForPem != nullptr)
                    ? PEM_write_bio_RSAPrivateKey(bio, rsaForPem, nullptr, nullptr, 0, nullptr, nullptr)
                    : PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    if (!wrote)
    {
        char errbuf[256];
        unsigned long e = ERR_get_error();
        ERR_error_string_n(e, errbuf, sizeof(errbuf));
        char diag[400];
        sprintf_s(diag, "Cannot convert .ppk to PEM [bio=%p pkey=%p rsa=%p err=0x%lx %s]",
                  (void*)bio, (void*)pkey, (void*)rsaForPem, e, errbuf);
        BIO_free(bio);
        EVP_PKEY_free(pkey);
        ErrorMsg = diag;
        return 0;
    }
    char* pem = nullptr;
    long pemlen = BIO_get_mem_data(bio, &pem);
    int rc = libssh2_userauth_publickey_frommemory(Session, user, strlen(user),
                                                   nullptr, 0, pem, pemlen, nullptr);
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    if (rc == 0) return 1; // success
    SetError(".ppk key authentication failed");
    return 0;
}
#else
int CSftpConnection::TryPpkAuth(const char* /*user*/, const char* /*keyFile*/, const char* /*passphrase*/)
{
    return -1;
}
#endif

bool CSftpConnection::RemoteFileSize(const char* remotePath, unsigned __int64& size)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    size = 0;
    if (ScpMode)
    {
        std::string cmd = "stat -c %s -- " + ShellQuote(remotePath);
        std::string out;
        int code = -1;
        if (!ExecRaw(cmd.c_str(), out, &code) || code != 0) return false;
        size = _strtoui64(out.c_str(), nullptr, 10);
        return true;
    }
    LIBSSH2_SFTP_ATTRIBUTES a;
    memset(&a, 0, sizeof(a));
    if (libssh2_sftp_stat(Sftp, remotePath, &a) != 0) return false;
    if (a.flags & LIBSSH2_SFTP_ATTR_SIZE) size = a.filesize;
    return true;
}

// shell-quoting for path (POSIX): wrap in '...' and escape inner '
static std::string ShellQuote(const char* s)
{
    std::string q = "'";
    for (const char* p = s; *p; p++)
    {
        if (*p == '\'')
            q += "'\\''";
        else
            q += *p;
    }
    q += "'";
    return q;
}

// convert rwx permission string (e.g. "rwxr-x---") to octal mode
static unsigned long RwxToMode(const char* rwx)
{
    unsigned long m = 0;
    for (int i = 0; i < 9 && rwx[i]; i++)
    {
        m <<= 1;
        char c = rwx[i];
        if (c != '-' && c != ' ')
            m |= 1; // r/w/x/s/t/S/T -> bit set
    }
    return m;
}

bool CSftpConnection::ExecRaw(const char* command, std::string& out, int* exitCode)
{
    out.clear();
    if (exitCode != nullptr)
        *exitCode = -1;
    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(Session);
    if (!ch) { SetError("Opening channel"); return false; }
    if (libssh2_channel_exec(ch, command))
    {
        SetError("Executing command");
        libssh2_channel_free(ch);
        return false;
    }
    char buf[8192];
    while (!libssh2_channel_eof(ch))
    {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, (size_t)n);
        else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN)
            break;
        n = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, (size_t)n);
        else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN)
            break;
    }
    ssize_t n;
    while ((n = libssh2_channel_read(ch, buf, sizeof(buf))) > 0)
        out.append(buf, (size_t)n);
    while ((n = libssh2_channel_read_stderr(ch, buf, sizeof(buf))) > 0)
        out.append(buf, (size_t)n);

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_close(ch);
    libssh2_channel_wait_closed(ch);
    if (exitCode != nullptr)
        *exitCode = libssh2_channel_get_exit_status(ch);
    libssh2_channel_free(ch);
    return true;
}

bool CSftpConnection::ExecSimple(const char* command)
{
    std::string out;
    int code = -1;
    if (!ExecRaw(command, out, &code))
        return false;
    if (code != 0)
    {
        ErrorMsg = out.empty() ? "Command on server failed." : out;
        return false;
    }
    return true;
}

bool CSftpConnection::FastDirSize(const char* remotePath, unsigned __int64& outBytes, int& outFiles, int& outDirs, bool countItems)
{
    outBytes = 0;
    outFiles = 0;
    outDirs = 0;
    if (Sock == INVALID_SOCKET || Session == nullptr)
        return false;

    std::string _rp = ToServerEnc(remotePath);
    std::string q = ShellQuote(_rp.c_str());

    // Try server-side du command first: du -sb -- <path> (GNU du / Linux / TrueNAS SCALE)
    // or du -sk -- <path> (POSIX / BSD / FreeBSD / TrueNAS CORE)
    std::string rawCmd = "du -sb -- " + q + " 2>/dev/null || du -sk -- " + q + " 2>/dev/null";
    char wrappedCmd[2048];
    WrapCommandWithSftpServerPrefix(SftpServerCmd.c_str(), rawCmd.c_str(), wrappedCmd, sizeof(wrappedCmd));

    std::string out;
    int code = -1;
    if (!ExecRaw(wrappedCmd, out, &code) || code != 0 || out.empty())
        return false;

    unsigned __int64 val = _strtoui64(out.c_str(), nullptr, 10);
    if (val == 0 && out[0] != '0')
        return false;

    // Check whether du output was in exact bytes (-b) or 1024-byte blocks (-k)
    bool isBytes = (out.find("invalid option") == std::string::npos && out.find("illegal option") == std::string::npos);
    outBytes = isBytes ? val : (val * 1024);

    // Count files and directories via find when requested (for Calculate Size dialog)
    if (countItems)
    {
        std::string rawCnt = "find " + q + " -type f 2>/dev/null | wc -l; find " + q + " -mindepth 1 -type d 2>/dev/null | wc -l";
        char wrappedCnt[2048];
        WrapCommandWithSftpServerPrefix(SftpServerCmd.c_str(), rawCnt.c_str(), wrappedCnt, sizeof(wrappedCnt));

        std::string cntOut;
        int cntCode = -1;
        if (ExecRaw(wrappedCnt, cntOut, &cntCode) && cntCode == 0 && !cntOut.empty())
        {
            int f = 0, d = 0;
            if (sscanf_s(cntOut.c_str(), "%d\n%d", &f, &d) >= 1)
            {
                outFiles = f;
                outDirs = d;
            }
        }
    }

    return true;
}

// SCP directory listing via "ls -la" (epoch time -> single field, handles spaces in names)
bool CSftpConnection::ScpListDir(const char* remotePath, std::vector<CSftpEntry>& out)
{
    out.clear();
    std::string cmd = "ls -la --time-style=+%s -- " + ShellQuote(remotePath);
    std::string text;
    int code = -1;
    if (!ExecRaw(cmd.c_str(), text, &code))
        return false;
    if (code != 0)
    {
        ErrorMsg = "Cannot list directory (SCP/ls).";
        return false;
    }
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.compare(0, 6, "total ") == 0)
            continue;
        // fields: perms links owner group size mtime name...
        char perms[16] = {0}, owner[128] = {0}, group[128] = {0};
        unsigned __int64 size = 0;
        unsigned long mtime = 0;
        int links = 0;
        // manually parse first 6 fields, rest = name
        std::vector<std::string> f;
        {
            size_t i = 0;
            while (i < line.size() && f.size() < 6)
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
                size_t start = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
                if (i > start) f.push_back(line.substr(start, i - start));
            }
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            if (f.size() < 6) continue; // unexpected line
            std::string name = line.substr(i);
            lstrcpynA(perms, f[0].c_str(), sizeof(perms));
            links = atoi(f[1].c_str());
            lstrcpynA(owner, f[2].c_str(), sizeof(owner));
            lstrcpynA(group, f[3].c_str(), sizeof(group));
            size = _strtoui64(f[4].c_str(), nullptr, 10);
            mtime = (unsigned long)_strtoui64(f[5].c_str(), nullptr, 10);

            CSftpEntry e;
            e.IsDir = (perms[0] == 'd');
            e.IsLink = (perms[0] == 'l');
            if (e.IsLink)
            {
                // "name -> target" -> take only name before " -> "
                size_t arrow = name.find(" -> ");
                if (arrow != std::string::npos)
                    name.resize(arrow);
                e.IsDir = false;
            }
            if (name == "." || name == "..")
                continue;
            e.Name = ToDisplayEnc(name.c_str());
            e.Size = size;
            e.MTime = mtime;
            e.Permissions = RwxToMode(perms + 1);
            e.Owner = owner;
            e.Group = group;
            out.push_back(e);
        }
        (void)links;
    }
    return true;
}

bool CSftpConnection::ScpDownload(const char* remotePath, const char* localPath)
{
    libssh2_struct_stat st;
    memset(&st, 0, sizeof(st));
    LIBSSH2_CHANNEL* ch = libssh2_scp_recv2(Session, remotePath, &st);
    if (!ch) { SetError("SCP download (open)"); return false; }
    unsigned __int64 total = (unsigned __int64)st.st_size;
    unsigned __int64 done = 0;

    if (!ReportProgress(localPath, 0, total))
    {
        ErrorMsg = "Cancelled by user.";
        libssh2_channel_free(ch);
        return false;
    }

    FILE* f = nullptr;
    fopen_s(&f, localPath, "wb");
    if (!f) { ErrorMsg = "Cannot create local file."; libssh2_channel_free(ch); return false; }
    char buf[65536];
    bool ok = true;
    while (done < total)
    {
        size_t want = (size_t)((total - done) < sizeof(buf) ? (total - done) : sizeof(buf));
        ssize_t n = libssh2_channel_read(ch, buf, want);
        if (n < 0) { SetError("SCP read"); ok = false; break; }
        if (n == 0) break;
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ErrorMsg = "Disk write failed."; ok = false; break; }
        done += (unsigned __int64)n;
        if (!ReportProgress(localPath, done, total)) { ErrorMsg = "Cancelled by user."; ok = false; break; }
    }
    if (ok && done < total)
    {
        ErrorMsg = "SCP download truncated (connection closed early).";
        ok = false;
    }
    if (fclose(f) != 0 && ok)
    {
        ErrorMsg = "Disk write failed.";
        ok = false;
    }
    if (ok)
    {
        // SCP protocol: after the data the source sends a status byte (0 = OK, 1/2 + message = error);
        // the sink answers with 0.
        char status = 0;
        ssize_t sn = libssh2_channel_read(ch, &status, 1);
        if (sn == 1 && status != 0)
        {
            char msg[256] = {0};
            ssize_t mn = libssh2_channel_read(ch, msg, sizeof(msg) - 1);
            if (mn < 0) mn = 0;
            msg[mn] = 0;
            while (mn > 0 && (msg[mn - 1] == '\n' || msg[mn - 1] == '\r')) msg[--mn] = 0;
            ErrorMsg = std::string("SCP remote error: ") + (msg[0] ? msg : "transfer failed");
            ok = false;
        }
        else if (sn < 0)
        {
            SetError("SCP download (status)");
            ok = false;
        }
        else if (sn == 1)
        {
            char ack = 0;
            libssh2_channel_write(ch, &ack, 1);
            libssh2_channel_send_eof(ch);
        }
    }
    libssh2_channel_close(ch);
    if (ok)
    {
        libssh2_channel_wait_closed(ch);
        int rc = libssh2_channel_get_exit_status(ch);
        if (rc != 0)
        {
            ErrorMsg = "SCP remote command failed (exit status " + std::to_string(rc) + ").";
            ok = false;
        }
    }
    libssh2_channel_free(ch);
    if (!ok && done == 0)
    {
        DeleteFileA(localPath);
    }
    return ok;
}

bool CSftpConnection::ScpUpload(const char* localPath, const char* remotePath)
{
    FILE* f = nullptr;
    fopen_s(&f, localPath, "rb");
    if (!f) { ErrorMsg = "Cannot open local file."; return false; }
    _fseeki64(f, 0, SEEK_END);
    unsigned __int64 total = (unsigned __int64)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    if (!ReportProgress(localPath, 0, total))
    {
        ErrorMsg = "Cancelled by user.";
        fclose(f);
        return false;
    }

    LIBSSH2_CHANNEL* ch = libssh2_scp_send64(Session, remotePath, 0644, (libssh2_int64_t)total, 0, 0);
    if (!ch) { SetError("SCP upload (open)"); fclose(f); return false; }
    char buf[65536];
    bool ok = true;
    unsigned __int64 done = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        char* p = buf;
        size_t left = n;
        while (left > 0)
        {
            ssize_t w = libssh2_channel_write(ch, p, left);
            if (w < 0) { SetError("SCP write"); ok = false; break; }
            p += w; left -= (size_t)w;
        }
        if (!ok) break;
        done += (unsigned __int64)n;
        if (!ReportProgress(localPath, done, total)) { ErrorMsg = "Cancelled by user."; ok = false; break; }
    }
    if (ok && ferror(f))
    {
        ErrorMsg = "Local file read failed.";
        ok = false;
    }
    fclose(f);
    if (ok && done < total)
    {
        ErrorMsg = "Local file size changed during upload.";
        ok = false;
    }
    if (ok)
    {
        if (libssh2_channel_send_eof(ch) < 0 ||
            libssh2_channel_wait_eof(ch) < 0 ||
            libssh2_channel_wait_closed(ch) < 0)
        {
            SetError("SCP upload (finish)");
            ok = false;
        }
        else
        {
            int rc = libssh2_channel_get_exit_status(ch);
            if (rc != 0)
            {
                ErrorMsg = "SCP remote command failed (exit status " + std::to_string(rc) + ").";
                ok = false;
            }
        }
    }
    libssh2_channel_free(ch);
    return ok;
}

bool CSftpConnection::ListDir(const char* remotePath, std::vector<CSftpEntry>& out)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ScpListDir(remotePath, out);
    out.clear();
    LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(Sftp, remotePath);
    if (!dir) { SetError("Opening directory"); return false; }
    char name[1024];
    char longentry[2048];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc;
    while ((rc = libssh2_sftp_readdir_ex(dir, name, sizeof(name), longentry, sizeof(longentry), &attrs)) > 0)
    {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        CSftpEntry e;
        e.Name = ToDisplayEnc(name);
        e.IsDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        e.IsLink = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISLNK(attrs.permissions);
        e.Size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attrs.filesize : 0;
        e.MTime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? (unsigned long)attrs.mtime : 0;
        e.Permissions = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? attrs.permissions : 0;

        // owner/group: try from "ls -l" longentry (fields 2 and 3), else UID/GID
        e.Owner.clear();
        e.Group.clear();
        if (longentry[0] != 0)
        {
            char tmp[2048];
            lstrcpynA(tmp, longentry, sizeof(tmp));
            char* ctx = NULL;
            char* tok = strtok_s(tmp, " \t", &ctx);
            int field = 0;
            while (tok != NULL)
            {
                if (field == 2)
                    e.Owner = tok;
                else if (field == 3)
                {
                    e.Group = tok;
                    break;
                }
                field++;
                tok = strtok_s(NULL, " \t", &ctx);
            }
        }
        if (e.Owner.empty() && (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID))
        {
            char b[32];
            sprintf_s(b, "%lu", attrs.uid);
            e.Owner = b;
            sprintf_s(b, "%lu", attrs.gid);
            e.Group = b;
        }
        if (e.IsLink)
            e.IsDir = false; // symlink displayed as file (target not resolved)
        out.push_back(e);
    }
    libssh2_sftp_closedir(dir);
    return true;
}

int CSftpConnection::PathType(const char* remotePath)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (strcmp(remotePath, "/") == 0)
        return 2;
    if (ScpMode)
    {
        std::string q = ShellQuote(remotePath);
        std::string cmd = "if [ -d " + q + " ]; then echo D; elif [ -e " + q + " ]; then echo F; else echo N; fi";
        std::string out;
        int code = -1;
        if (!ExecRaw(cmd.c_str(), out, &code))
            return 0;
        if (out.find('D') != std::string::npos) return 2;
        if (out.find('F') != std::string::npos) return 1;
        return 0;
    }
    LIBSSH2_SFTP_ATTRIBUTES a;
    if (libssh2_sftp_stat(Sftp, remotePath, &a) != 0)
        return 0; // does not exist (or unavailable)
    if ((a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(a.permissions))
        return 2;
    return 1;
}

bool CSftpConnection::GetHomeDir(std::string& homeDir)
{
    if (!IsConnected()) return false;
    if (ScpMode)
    {
        homeDir = "/";
        return true;
    }
    char buf[1024];
    int rc = libssh2_sftp_realpath(Sftp, ".", buf, sizeof(buf));
    if (rc > 0)
    {
        buf[rc < (int)sizeof(buf) ? rc : (sizeof(buf) - 1)] = 0;
        homeDir = ToDisplayEnc(buf);
        return true;
    }
    homeDir = "/";
    return true;
}

bool CSftpConnection::Chmod(const char* remotePath, unsigned long mode)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
    {
        char oct[16];
        sprintf_s(oct, "%lo", mode & 0777);
        std::string cmd = "chmod " + std::string(oct) + " -- " + ShellQuote(remotePath);
        return ExecSimple(cmd.c_str());
    }
    LIBSSH2_SFTP_ATTRIBUTES a;
    memset(&a, 0, sizeof(a));
    a.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    a.permissions = mode;
    if (libssh2_sftp_setstat(Sftp, remotePath, &a) != 0)
    {
                SetError("Changing permissions");
        return false;
    }
    return true;
}

bool CSftpConnection::GetPermissions(const char* remotePath, unsigned long& mode)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
    {
        std::string cmd = "stat -c %a -- " + ShellQuote(remotePath);
        std::string out;
        int code = -1;
        if (!ExecRaw(cmd.c_str(), out, &code) || code != 0)
        {
            ErrorMsg = "Reading permissions (SCP/stat).";
            return false;
        }
        mode = strtoul(out.c_str(), nullptr, 8) & 0777;
        return true;
    }
    LIBSSH2_SFTP_ATTRIBUTES a;
    memset(&a, 0, sizeof(a));
    if (libssh2_sftp_stat(Sftp, remotePath, &a) != 0)
    {
        SetError("Reading permissions");
        return false;
    }
    mode = a.permissions & 0777;
    return true;
}

bool CSftpConnection::StatFull(const char* remotePath, unsigned __int64& size, unsigned long& perms,
                               unsigned long& uid, unsigned long& gid, unsigned long& mtime)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
    {
        std::string cmd = "stat -c '%a %s %u %g %Y' -- " + ShellQuote(remotePath);
        std::string out;
        int code = -1;
        if (!ExecRaw(cmd.c_str(), out, &code) || code != 0)
        {
            ErrorMsg = "Reading info (SCP/stat).";
            return false;
        }
        unsigned long pmode = 0;
        if (sscanf_s(out.c_str(), "%lo %llu %lu %lu %lu", &pmode, &size, &uid, &gid, &mtime) < 5)
        {
            ErrorMsg = "Unexpected stat output.";
            return false;
        }
        perms = pmode & 0777;
        return true;
    }
    LIBSSH2_SFTP_ATTRIBUTES a;
    memset(&a, 0, sizeof(a));
    if (libssh2_sftp_stat(Sftp, remotePath, &a) != 0)
    {
        SetError("Reading info");
        return false;
    }
    size = (a.flags & LIBSSH2_SFTP_ATTR_SIZE) ? a.filesize : 0;
    perms = (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? (a.permissions & 0777) : 0;
    uid = (a.flags & LIBSSH2_SFTP_ATTR_UIDGID) ? a.uid : 0;
    gid = (a.flags & LIBSSH2_SFTP_ATTR_UIDGID) ? a.gid : 0;
    mtime = (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? (unsigned long)a.mtime : 0;
    return true;
}

bool CSftpConnection::Download(const char* remotePath, const char* localPath, unsigned __int64 resumeOffset)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ScpDownload(remotePath, localPath); // SCP resume not supported
    LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(Sftp, remotePath, LIBSSH2_FXF_READ, 0);
    if (!h) { SetError("Opening remote file"); return false; }
    LIBSSH2_SFTP_ATTRIBUTES at;
    unsigned __int64 total = 0;
    if (libssh2_sftp_fstat(h, &at) == 0 && (at.flags & LIBSSH2_SFTP_ATTR_SIZE))
        total = at.filesize;
    if (resumeOffset > 0 && resumeOffset < total) // offset == size would transfer nothing
        libssh2_sftp_seek64(h, resumeOffset); // resume from given position
    else
        resumeOffset = 0;

    // Check progress/cancel BEFORE creating or truncating the local file!
    if (!ReportProgress(localPath, resumeOffset, total))
    {
        ErrorMsg = "Cancelled by user.";
        libssh2_sftp_close(h);
        return false;
    }

    FILE* f = nullptr;
    fopen_s(&f, localPath, resumeOffset > 0 ? "r+b" : "wb");
    if (!f) { ErrorMsg = "Cannot open local file."; libssh2_sftp_close(h); return false; }
    if (resumeOffset > 0) _fseeki64(f, (long long)resumeOffset, SEEK_SET);
    char buf[65536];
    bool ok = true;
    unsigned __int64 done = resumeOffset;
    for (;;)
    {
        ssize_t n = libssh2_sftp_read(h, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) { SetError("Reading remote file"); ok = false; break; }
        if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ErrorMsg = "Disk write failed."; ok = false; break; }
        done += (unsigned __int64)n;
        if (!ReportProgress(localPath, done, total)) { ErrorMsg = "Cancelled by user."; ok = false; break; }
    }
    if (ok && total > 0 && done < total)
    {
        ErrorMsg = "Download incomplete (remote file size changed or connection closed early).";
        ok = false;
    }
    if (fclose(f) != 0 && ok)
    {
        ErrorMsg = "Disk write failed.";
        ok = false;
    }
    libssh2_sftp_close(h);
    if (!ok && done == 0 && resumeOffset == 0)
    {
        DeleteFileA(localPath); // clean up 0-byte file if cancelled before any data was written
    }
    return ok;
}

bool CSftpConnection::Upload(const char* localPath, const char* remotePath, unsigned __int64 resumeOffset)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ScpUpload(localPath, remotePath); // SCP resume not supported
    FILE* f = nullptr;
    fopen_s(&f, localPath, "rb");
    if (!f) { ErrorMsg = "Cannot open local file."; return false; }
    _fseeki64(f, 0, SEEK_END);
    unsigned __int64 total = (unsigned __int64)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    // Check progress/cancel BEFORE creating or truncating the remote file!
    if (!ReportProgress(localPath, resumeOffset, total))
    {
        ErrorMsg = "Cancelled by user.";
        fclose(f);
        return false;
    }

    // resume: open without TRUNC and seek to position; otherwise overwrite from beginning
    long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT;
    if (resumeOffset > 0 && resumeOffset < total)
        ; // ponech bez TRUNC
    else
    {
        flags |= LIBSSH2_FXF_TRUNC;
        resumeOffset = 0;
    }
    LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(Sftp, remotePath, flags,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!h && Sftp && libssh2_sftp_last_error(Sftp) == LIBSSH2_FX_NO_SUCH_FILE)
    {
        std::string rp = remotePath;
        size_t lastSlash = rp.rfind('/');
        if (lastSlash != std::string::npos && lastSlash > 0)
        {
            std::string parentDir = rp.substr(0, lastSlash);
            MakeDirRecursive(parentDir.c_str());
            h = libssh2_sftp_open(Sftp, remotePath, flags,
                LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
        }
    }
    if (!h) { SetError("Creating remote file"); fclose(f); return false; }
    if (resumeOffset > 0)
    {
        libssh2_sftp_seek64(h, resumeOffset);
        _fseeki64(f, (long long)resumeOffset, SEEK_SET);
    }
    char buf[65536];
    bool ok = true;
    unsigned __int64 done = resumeOffset;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        char* p = buf;
        size_t left = n;
        while (left > 0)
        {
            ssize_t w = libssh2_sftp_write(h, p, left);
            if (w < 0) { SetError("Writing remote file"); ok = false; break; }
            p += w; left -= (size_t)w;
        }
        if (!ok) break;
        done += (unsigned __int64)n;
        if (!ReportProgress(localPath, done, total)) { ErrorMsg = "Cancelled by user."; ok = false; break; }
    }
    if (ok && ferror(f))
    {
        ErrorMsg = "Local file read failed.";
        ok = false;
    }
    fclose(f);
    if (ok && done < total)
    {
        ErrorMsg = "Local file size changed during upload.";
        ok = false;
    }
    // close flushes pending writes; a failure here means the data may not be stored
    if (libssh2_sftp_close(h) < 0 && ok)
    {
        SetError("Closing remote file");
        ok = false;
    }
    if (!ok && done == 0 && resumeOffset == 0)
    {
        libssh2_sftp_unlink(Sftp, remotePath); // clean up 0-byte remote file if cancelled before any data was written
    }
    return ok;
}

bool CSftpConnection::GetSecurityInfo(std::string& out)
{
    out.clear();
    if (!Session)
        return false;
    char line[256];
    // typ host key + SHA256 otisk
    int ktype = 0;
    size_t klen = 0;
    const char* hk = libssh2_session_hostkey(Session, &klen, &ktype);
    if (!hk)
        return false;
    const char* ktypeName = "unknown";
    if (ktype == LIBSSH2_HOSTKEY_TYPE_RSA) ktypeName = "RSA";
    else if (ktype == LIBSSH2_HOSTKEY_TYPE_DSS) ktypeName = "DSS";
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
    else if (ktype == LIBSSH2_HOSTKEY_TYPE_ECDSA_256 || ktype == LIBSSH2_HOSTKEY_TYPE_ECDSA_384 || ktype == LIBSSH2_HOSTKEY_TYPE_ECDSA_521) ktypeName = "ECDSA";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
    else if (ktype == LIBSSH2_HOSTKEY_TYPE_ED25519) ktypeName = "ED25519";
#endif
    sprintf_s(line, "Typ host key: %s\r\n", ktypeName);
    out += line;

    const unsigned char* fp = (const unsigned char*)libssh2_hostkey_hash(Session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (fp)
    {
        out += "Otisk (SHA256): ";
        for (int i = 0; i < 32; i++)
        {
            sprintf_s(line, "%02x%s", fp[i], i < 31 ? ":" : "");
            out += line;
        }
        out += "\r\n";
    }
    const unsigned char* fp1 = (const unsigned char*)libssh2_hostkey_hash(Session, LIBSSH2_HOSTKEY_HASH_SHA1);
    if (fp1)
    {
        out += "Otisk (SHA1): ";
        for (int i = 0; i < 20; i++)
        {
            sprintf_s(line, "%02x%s", fp1[i], i < 19 ? ":" : "");
            out += line;
        }
        out += "\r\n";
    }
    const char* banner = libssh2_session_banner_get(Session);
    if (banner)
    {
        sprintf_s(line, "Banner serveru: %.200s\r\n", banner);
        out += line;
    }
    return true;
}

bool CSftpConnection::ExecCommandStream(const char* command, ExecStreamCallback callback, void* ctx, volatile bool* cancelFlag)
{
    if (!Session) { SetError("Not connected"); return false; }
    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(Session);
    if (!ch) { SetError("Opening channel"); return false; }
    if (libssh2_channel_exec(ch, command))
    {
        SetError("Executing command");
        libssh2_channel_free(ch);
        return false;
    }

    libssh2_session_set_blocking(Session, 0);

    char buf[4096];
    while (!cancelFlag || !*cancelFlag)
    {
        bool hasData = false;
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0)
        {
            hasData = true;
            if (callback)
                callback(ctx, buf, (size_t)n);
        }

        ssize_t ne = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
        if (ne > 0)
        {
            hasData = true;
            if (callback)
                callback(ctx, buf, (size_t)ne);
        }

        if (libssh2_channel_eof(ch) && n <= 0 && ne <= 0)
            break;

        if (!hasData)
        {
            Sleep(25);
        }
    }

    libssh2_channel_free(ch);
    libssh2_session_set_blocking(Session, 1);
    return true;
}

static bool StringAppendCallback(void* ctx, const char* data, size_t size)
{
    std::string* out = (std::string*)ctx;
    out->append(data, size);
    return true;
}

bool CSftpConnection::ExecCommand(const char* command, std::string& output)
{
    output.clear();
    return ExecCommandStream(command, StringAppendCallback, &output, nullptr);
}


bool CSftpConnection::MakeDir(const char* remotePath)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ExecSimple(("mkdir -- " + ShellQuote(remotePath)).c_str());
    if (libssh2_sftp_mkdir(Sftp, remotePath,
            LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH))
    { SetError("Creating directory"); return false; }
    return true;
}

bool CSftpConnection::MakeDirRecursive(const char* remotePath)
{
    if (!remotePath || !*remotePath)
        return false;
    std::string path = remotePath;
    for (size_t i = 0; i < path.size(); i++)
    {
        if (path[i] == '\\')
            path[i] = '/';
    }
    size_t pos = 1;
    while (pos < path.size())
    {
        size_t nextSlash = path.find('/', pos);
        if (nextSlash == std::string::npos)
            break;
        std::string sub = path.substr(0, nextSlash);
        if (!sub.empty() && sub != "/")
        {
            MakeDir(sub.c_str()); // ignore error if already exists
        }
        pos = nextSlash + 1;
    }
    return MakeDir(path.c_str());
}

bool CSftpConnection::RemoveDir(const char* remotePath)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ExecSimple(("rmdir -- " + ShellQuote(remotePath)).c_str());
    if (libssh2_sftp_rmdir(Sftp, remotePath)) { SetError("Deleting directory"); return false; }
    return true;
}

bool CSftpConnection::RemoveFile(const char* remotePath)
{
    std::string _rp = ToServerEnc(remotePath);
    remotePath = _rp.c_str();
    if (ScpMode)
        return ExecSimple(("rm -f -- " + ShellQuote(remotePath)).c_str());
    if (libssh2_sftp_unlink(Sftp, remotePath)) { SetError("Deleting file"); return false; }
    return true;
}

bool CSftpConnection::Rename(const char* oldPath, const char* newPath)
{
    std::string _op = ToServerEnc(oldPath), _np = ToServerEnc(newPath);
    oldPath = _op.c_str();
    newPath = _np.c_str();
    if (ScpMode)
        return ExecSimple(("mv -- " + ShellQuote(oldPath) + " " + ShellQuote(newPath)).c_str());
    if (libssh2_sftp_rename(Sftp, oldPath, newPath)) { SetError("Renaming"); return false; }
    return true;
}
