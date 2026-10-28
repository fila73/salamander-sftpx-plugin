// Live transfer test against a real server. Env: SFTP_LIVE_HOST, SFTP_LIVE_USER, SFTP_LIVE_KEY,
// SFTP_LIVE_REMOTE_DIR (existing dir; only NEW uniquely named files are created), SFTP_LIVE_LOCAL_DIR.
// Args: <mode 0=SFTP|1=SCP> <runid>. Never deletes/overwrites anything on the server.
#include "sftpconn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <random>
#include <windows.h>

void WrapCommandWithSftpServerPrefix(const char*, const char*, char* o, size_t n) { if (o && n) o[0] = 0; }
void SftpTraceLog(const char*, ...) {}

static std::string Env(const char* n) { const char* v = getenv(n); return v ? v : ""; }
static int HostCb(void*, const char*, int, const char*, const char*, int status) { return status == 1 ? 0 : 1; }
static std::string L, R, RUN;

static void WriteFile(const std::string& p, size_t n, bool text)
{
    FILE* f = nullptr; fopen_s(&f, p.c_str(), "wb"); if (!f) { printf("cannot create %s\n", p.c_str()); exit(2); }
    if (text) fputs("small text file\nline2\n", f);
    else { std::mt19937 g(12345); for (size_t i = 0; i < n; i++) fputc((int)(g() & 0xFF), f); }
    fclose(f);
}
static long long Size(const std::string& p)
{
    FILE* f = nullptr; fopen_s(&f, p.c_str(), "rb"); if (!f) return -1;
    _fseeki64(f, 0, SEEK_END); long long s = _ftelli64(f); fclose(f); return s;
}
static void Report(const char* test, bool ok, bool expectOk, ULONGLONG t0, CSftpConnection& c)
{
    ULONGLONG ms = GetTickCount64() - t0;
    bool pass = (ok == expectOk);
    printf("RESULT | %-34s | %s | %5llu ms | %s%s\n", test, pass ? "PASS" : "FAIL", ms,
           ok ? "ok" : "failed", ok ? "" : (std::string(" | err=") + c.LastError()).c_str());
    fflush(stdout);
}
#define T(name, expectOk, expr) do { ULONGLONG t0_ = GetTickCount64(); bool ok_ = (expr); Report(name, ok_, expectOk, t0_, c); } while (0)

int main(int argc, char** argv)
{
    int mode = argc > 1 ? atoi(argv[1]) : 0; RUN = argc > 2 ? argv[2] : "0";
    L = Env("SFTP_LIVE_LOCAL_DIR"); R = Env("SFTP_LIVE_REMOTE_DIR");
    if (R.empty() || R[0] != '/') { printf("bad SFTP_LIVE_REMOTE_DIR [%s]\n", R.c_str()); return 2; }
    if (!CSftpConnection::GlobalInit()) return 1;
    CSftpConnection::SetHostKeyCallback(HostCb, nullptr);
    CSftpConnection::SetKnownHostsFile((L + "/known_hosts_unused").c_str());
    CSftpConnection c;
    std::string key = Env("SFTP_LIVE_KEY");
    if (!c.Connect(Env("SFTP_LIVE_HOST").c_str(), 22, Env("SFTP_LIVE_USER").c_str(), "", key.c_str(), false, mode, false))
    { printf("connect FAIL: %s\n", c.LastError()); return 1; }
    printf("connected, scpMode=%d\n", (int)c.IsScpMode());

    std::string pre = std::string("m") + (mode ? "scp" : "sftp") + "_" + RUN + "_";
    const char* names[3] = { "big.bin", "zero.bin", "small.txt" };
    for (int i = 0; i < 3; i++) {
        std::string lp = L + "/src_" + names[i];
        if (i == 0) WriteFile(lp, 5 * 1024 * 1024, false); else if (i == 1) WriteFile(lp, 0, false); else WriteFile(lp, 0, true);
    }
    for (int i = 0; i < 3; i++) {
        std::string lp = L + "/src_" + names[i], rp = R + "/" + pre + names[i];
        T((std::string("upload ") + names[i]).c_str(), true, c.Upload(lp.c_str(), rp.c_str()));
    }
    for (int i = 0; i < 3; i++) {
        std::string lp = L + "/dl_" + pre + names[i], rp = R + "/" + pre + names[i];
        T((std::string("download ") + names[i]).c_str(), true, c.Download(rp.c_str(), lp.c_str()));
    }
    { std::string lp = L + "/dl_" + pre + "missing"; T("download nonexistent", false, c.Download((R + "/" + pre + "does_not_exist").c_str(), lp.c_str()));
      printf("   local file exists after failed dl: size=%lld\n", Size(lp)); }
    if (mode == 1) T("upload into nonexistent dir", false, c.Upload((L + "/src_small.txt").c_str(), (R + "/nope_" + RUN + "/x").c_str()));
    T("upload to /proc/claude_x", false, c.Upload((L + "/src_small.txt").c_str(), "/proc/claude_x"));
    T("upload to /sys/claude_x", false, c.Upload((L + "/src_small.txt").c_str(), "/sys/claude_x"));
    // resume
    printf("resume start\n"); fflush(stdout);
    {
        std::string rp = R + "/" + pre + "big.bin", src = L + "/src_big.bin", lp = L + "/res_" + pre + "partial.bin";
        FILE* in = nullptr; fopen_s(&in, src.c_str(), "rb"); FILE* o = nullptr; fopen_s(&o, lp.c_str(), "wb");
        const size_t part = 1000000; std::string buf(part, 0); fread(&buf[0], 1, part, in); fwrite(buf.data(), 1, part, o); fclose(in); fclose(o);
        T("resume download at 1000000", true, c.Download(rp.c_str(), lp.c_str(), part));
        printf("   resumed size=%lld (expect 5242880)\n", Size(lp));
        std::string lp2 = L + "/res_" + pre + "full.bin";
        CopyFileA(src.c_str(), lp2.c_str(), FALSE);
        T("resume download offset==full size", true, c.Download(rp.c_str(), lp2.c_str(), 5242880));
        printf("   size=%lld (expect 5242880)\n", Size(lp2));
    }
    c.Disconnect();
    CSftpConnection::GlobalExit();
    return 0;
}
