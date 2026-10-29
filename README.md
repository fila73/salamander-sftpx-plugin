# SFTP/SCP Plugin for Open Salamander (x64)

A full-featured **SFTP and SCP** client as a file-system plugin for [Open Salamander 5.0](https://github.com/OpenSalamander/salamander) (x64) and Altap Salamander 4.x. Built on top of **libssh2 + OpenSSL**, providing modern cryptography, high transfer speed, and maximum stability.

> [!NOTE]
> **Fork Notice & Attribution**:  
> This project is an enhanced and maintained fork of [Dupl3xx/salamander-sftp-plugin](https://github.com/Dupl3xx/salamander-sftp-plugin).  
> All credit for the original plugin architecture, SDK integration, and base implementation goes to **Dupl3xx**.  
> Core custom components Copyright © 2026 Dupl3xx & contributors.

---

## 🚀 Key Differences & Enhancements (vs. Upstream)

This fork introduces significant stability fixes, architecture improvements, enhanced performance, and new features:

| Feature / Area | Original Upstream ([Dupl3xx](https://github.com/Dupl3xx/salamander-sftp-plugin)) | This Enhanced Fork ([fila73](https://github.com/fila73/salamander-sftp-plugin)) |
|---|---|---|
| **Build & Dependencies** | Required external `libssh2.dll` and dynamic MinGW runtime DLLs | **Standalone zero-dependency build**: `libssh2` is statically embedded; the C/C++ runtime is linked statically (`/MT`). Only `libcrypto-3-x64.dll` is required. |
| **Custom SFTP Server & Elevation** | Only default SSHD SFTP subsystem | **Custom `sftp-server` command support**: Execute SFTP under `sudo` / `su -c` or specific user (e.g. `www-data`), custom binary paths on NAS/BSD, and automatic prefix propagation to command-line bar executions. |
| **Command Execution** | Basic execution | **Asynchronous non-blocking background execution** with dedicated SSH connection; resizable streaming console dialog (Consolas font, text wrap, Cancel button). |
| **Connection Keepalive** | Basic TCP / idle handling | **Active periodic FS timer keepalive** (8s interval) preventing disconnects on TrueNAS / OpenSSH (`ClientAliveInterval`) and stateful firewalls; non-blocking socket health check & auto-reconnect. |
| **Directory Size Calculation** | Standard manual traversal | **Fast server-side calculation** (`FastDirSize` via SSH `du -sb`), non-blocking cancelable progress dialog, symlink cycle protection, **Spacebar on folder** calculation with auto-advance, and **`Ctrl+Shift+F10`** hotkey + context menu integration. |
| **Server File Execution (Enter)** | All files downloaded to Windows | **Direct server execution for `+x` files upon Enter**: If a file has executable permissions (scripts, binaries), pressing Enter runs `./file` directly on the server in the streaming console (with `sudo` support). Non-executable files continue opening locally in Windows. Fully configurable per profile. |
| **Directory Navigation** | Reset focus on parent entry | **Preserves cursor focus** on the exited folder when navigating up (`..`). |
| **Tab Titles & Viewer Cache (F3)** | Corrupted tab names (`oot@...`), stale file contents from other servers | **Connection Name `[NAS]` in Tab Titles & Reliable Cache**: Implementation of `GetPathForMainWindowTitle` with connection profile prefix (e.g. `[NAS] Season 29`) to easily distinguish servers (test/stage/prod), address bar parser fix; disk cache invalidation including host, user, port, remote path, file size, and modification timestamp. |
| **Concurrent Multiple Connections (Per-instance)** | Single global singleton; opening second panel or disconnect affected all sessions | **Independent Per-instance Connections**: Every FS panel and tab owns its isolated `CSftpConnection` instance, `CSftpProfile` configuration, keepalive timer, and instanced progress callbacks. Unlimited simultaneous open server sessions. |
| **Asynchronous Background Transfers (Phase B)** | Blocking synchronous transfers; panels frozen during download/upload | **Fully Asynchronous Background Transfers & Multi-Transfer Concurrency**: Every transfer operation spawns its dedicated worker thread (`CSftpTransferWorker`) on its own independent SSH session `WorkerConn` with its own modeless progress dialog. Thread initialization uses CRT `_beginthreadex` and handshake random generation is routed directly to native Windows CSPRNG `BCryptGenRandom` to completely prevent OpenSSL DRBG lock crashes. Unlimited concurrent downloads/uploads can run simultaneously without queue or progress interference. Dialogs are independent unowned windows (clicking main window brings Salamander to foreground without dialogs staying forced on top), featuring taskbar buttons and title bar minimize support. Modeless dialog with **"Background"** button (`IDB_BACKGROUND`), menu item **Plugins ➔ SFTP ➔ Show Transfers...** to bring back all hidden windows from any panel, instant panel interactivity during transfer, and automatic panel refresh via `PostChangeOnPathNotification`. |
| **Transfer Progress, Dual Modes & Dark Mode** | Recycled delete dialog with hardcoded "Deleting" text, no speed, ETA or total progress; 0-byte ghost files created upon cancel; no Dark Mode support | **Full Dual-Mode Progress Dialog (Simple/Detailed) on Dedicated UI Thread, Dynamic Resizing and Full Dark Mode**: Dialog runs on its own UI thread (`CSftpProgressDlgThread`) with its own message loop matching Salamander FTP plugin architecture (no Salamander main thread interference, menus do not collapse, cursor does not flicker). Supports dynamic resizing (`LayoutDialog`) where Connections and Operations lists proportionally share vertical height (35% / 65%) with auto-adjusting column widths. Displays rich transfer details: filename, transfer speed (MB/s), percentages, remaining time (ETA), overall progress, action buttons (`Stop`, `Pause`, `Retry`), error filtering, and persistent `[x] Close this dialog when finished` checkbox. Seamless keyboard navigation (`Tab`, `Shift+Tab`, arrow keys). |
| **Existing File Conflict (Overwrite vs. Resume)** | Previously automatic overwrite or resume without asking | **Full File Conflict Resolution Dialog matching FTP plugin (`IDD_CONFLICTDLG`)**: Choose Overwrite, Overwrite All, Skip, Cancel, generate alternate filename into editbox, and split dropdown **Retry** button with menu: *Retry*, *Resume*, *Resume or Overwrite*, and *Use Alternate Name*. Includes checkbox to remember decision for all subsequent conflicts. |
| **Server-to-Server Transfers** | Target URL truncated, copied data back to source server triggering overwrite | **Direct Server-to-Server Copy & Move**: Transferring files between two panels connected to different SFTP servers (e.g. `[HOP Test]` -> `[HOP Stage]`) detects the target server, resolves active connection or profile, displays distinct `From: [Source]` and `To: [Destination]` paths, performs buffered streaming transfer via local `%TEMP%`, and automatically refreshes both panels upon completion. |
| **Leaving Panel & Detached FS (`TryCloseOrDetach`)** | Panel silently disconnected and closed upon drive change | **Prompt to disconnect or keep connection in background**: Changing drive or leaving SFTP panel offers **Disconnect** / **Keep** / **Cancel** (matching Salamander FTP plugin behavior). Choosing Keep transitions connection into Detached FS mode (stays alive in memory with periodic keepalive, accessible anytime via `Alt+F1`/`Alt+F2` drive menu with clean profile name `SFTP:[NAS] /path`). Choice can be permanently remembered (`LeavePanelAction` in registry). |

---

## Features

### Protocols
- **SFTP** (SSH File Transfer Protocol) – default, high-performance v3 protocol
- **SCP** – directory listing via shell (`ls`/`stat`), file transfer via `libssh2_scp_*`, operations (`mkdir`/`rm`/`mv`/`chmod`) via shell
- **Fallback SCP** – automatic transparent fallback to SCP when the remote server does not support the SFTP subsystem

### Custom SFTP Server Command (Privilege Escalation & User Switching)
In the connection dialog, you can configure the **SFTP Server** field for any profile:
1. **Manage files as `root` via `sudo`**:
   - `sudo /usr/lib/openssh/sftp-server`
   - `sudo su -c /usr/lib/openssh/sftp-server`
   - Ideal for servers where direct SSH root login is disabled (`PermitRootLogin no`). Authenticate as a normal user with SSH keys, while the SFTP session operates with full root privileges.
2. **Switch to service / application user**:
   - `sudo -u www-data /usr/lib/openssh/sftp-server`
   - Manage web directories directly under the webserver identity (new files automatically receive `www-data:www-data` ownership).
3. **Non-standard binary paths on NAS & BSD systems**:
   - Synology NAS: `/usr/syno/sbin/sftp-server`
   - QNAP / macOS / BSD: `/usr/libexec/sftp-server`
   - OpenWrt / BusyBox: `/usr/lib/ssh/sftp-server`
4. **Custom server flags & options**:
   - `/usr/lib/openssh/sftp-server -u 0022` (set default umask for created files)
   - `/usr/lib/openssh/sftp-server -l DEBUG3` (verbose server-side logging)
5. **Automatic prefix propagation to command line**:
   - Commands executed from Open Salamander's bottom command line bar automatically inherit the `sudo` / `su` prefix (e.g. `sudo su -c '<command>'`), executing in the same target user context.

### Cryptography (via OpenSSL Backend)
Negotiated automatically based on server capabilities:
- **Key Exchange**: curve25519-sha256, ECDH (nistp256/384/521), DH group14/16/18
- **Key Types**: ed25519, ECDSA, RSA (rsa-sha2-256/512)
- **Ciphers**: ChaCha20-Poly1305, AES-GCM, AES-CTR
- **Compression**: zlib (optional)

### Authentication
- **Password**
- **Private Key** – OpenSSH/PEM and **PuTTY `.ppk`** (RSA + ed25519, v2/v3, including encrypted keys – v2 SHA1/AES, v3 Argon2id via OpenSSL)
- **Keyboard-Interactive** – including 2FA / MFA (first prompt auto-filled with password, subsequent prompts handled interactively via dialog)

### Security
- **Host Key Verification** against `known_hosts` (`%APPDATA%\OpenSalamander-SFTP\known_hosts`)
- Trust prompt for unknown servers (Save / Just once / Reject), warning on key change (MITM detection)
- Fingerprint display (SHA256 / SHA1), key type, and server identification banner

### File Operations & Features
- Remote file browsing (permissions, owner, group columns), downloading, uploading
- **Transfer progress with live speed gauge**, overwrite confirmations
- **Resume interrupted transfers** – byte-exact resume from last position (SFTP)
- Delete, create directory, rename, **change permissions (`chmod`)**, properties
- **Edit file on server** (`F4` – downloads to temp, opens configured editor, automatically re-uploads on save)
- **Calculate directory size** (`Ctrl+Shift+F10` / Spacebar on folder, fast server-side `du` with recursive fallback, non-blocking progress dialog with Cancel button, symlink cycle protection, and panel size updates)
- **Remote Command Execution**:
  - Direct execution via Open Salamander command line bar below panels (with automatic `sudo`/`su` prefix wrapping)
  - Context menu item **`Execute`** (located right after `Open`)
  - Real-time command output console dialog with clean monospace font (Consolas) and cancel/interrupt support

### Connection Management & UI
- Login dialog with categorized tree view and saved connections (New / Edit / Delete / Rename / Set as Default)
- **Keepalive & Connection Stability**: Active periodic FS timer keepalive (8s) prevents idle disconnects on firewalls and TrueNAS / OpenSSH servers (`ClientAliveInterval`); non-blocking socket health check and automatic transparent reconnect & retry; fix for libssh2 timer preventing spurious `Timed out waiting on socket` errors during slow server disk flushes.
- Password visibility toggle (eye icon)
- Dark mode support matching Open Salamander dark theme
- Multi-language localization (`.slg` modules for English, Czech, and all 11 Salamander languages)

---

## Source Code Structure (`src/`)

### Plugin Core
| File | Purpose |
|------|---------|
| **`sftpconn.h/.cpp`** | **Connection layer over libssh2.** Connect (handshake, host key verification, authentication), ListDir, Download/Upload (with resume + progress), SCP operations, chmod/stat, known_hosts verification, PuTTY `.ppk` parser, streaming command exec, active keepalive, fast directory size, custom `sftp-server` execution. |
| **`sftpglue.h/.cpp`** | **Glue** between Open Salamander FS and `CSftpConnection`. POSIX path helpers, `SftpEnsureConnected`, `WrapCommandWithSftpServerPrefix`, KBI dialog callbacks, connection profiles and saved sessions. |
| **`dialogs.h/.cpp`** | Real-time command execution console dialog (`ShowCommandExecDialog`), worker streaming thread, custom monospace font, and dark mode handling. |

### Salamander Integration
| File | Purpose |
|------|---------|
| `sftp.cpp` | Plugin entry point, registration (FS name `dfs`), menu command routing, window message hook for Spacebar calculation, registry configuration loading/saving. |
| `fs1.cpp` | **Login dialog** (`ConnectDlgProc`) – category tree, connection profiles, custom SFTP server field, saved sessions management, active FS tracking, directory navigation focus preservation. |
| `fs2.cpp` | **FS interface implementation** – ChangePath, ListCurrentPath, copy/download/upload (with resume/overwrite dialogs), Delete, CreateDir, QuickRename, ChangeAttributes (chmod), ShowProperties, SftpOnSpacePressedOnFolder, context menu. |
| `menu.cpp` | Menu command handlers (Edit file, Calculate size, Disconnect, Execute). |
| `sftp.h` | Shared declarations, `CFSData` (column attributes), command constants. |

### Resources and Build Files
| File | Purpose |
|------|---------|
| `lang/lang.rc`, `lang.rc2`, `lang.rh` | Dialog templates, string tables, Czech/English translations compiled into `.slg`. |
| `res/fs.ico`, `dir.ico`, `file.ico` | Icons for filesystem and dialogs. |
| `versinfo.rh2` | Version, copyright, and description resource header. |
| `vcxproj/sftp.vcxproj` | Visual Studio MSBuild project file. |

---

## Building

**Requirements:** Visual Studio 2022 or newer (or Build Tools) with the x64 C++ toolset and a Windows 10/11 SDK. No vcpkg: libssh2 is compiled from `src/libssh2` and the import library for Open Salamander's `libcrypto-3-x64.dll` is generated from `libcrypto-3-x64.def` during the build. The plugin SDK is taken from the samandarin fork (`..\..\samandarin\src`, i.e. `plugins\shared` there); override with `/p:SalamanderSdkDir=<dir containing plugins\shared>`.

```powershell
MSBuild srccxproj\sftp.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output goes to `build\Release_x64\` (`sftp.spl`, `lang\english.slg`, `lang\czech.slg`). The plugin is linked with the static CRT (`/MT`) and libssh2 is embedded, so it only needs `libcrypto-3-x64.dll` from Open Salamander.

---

## License & Credits
- **Upstream Repository**: [https://github.com/Dupl3xx/salamander-sftp-plugin](https://github.com/Dupl3xx/salamander-sftp-plugin)
- Based on Open Salamander SDK (SPDX / GPL-2.0-or-later).
- Original core implementation Copyright © 2026 Dupl3xx.
- Enhancements and maintenance Copyright © 2026 fila73 & contributors.
