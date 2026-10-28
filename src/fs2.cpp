// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"
#include "sftpglue.h"

static const DWORD SFTP_TIMER_KEEPALIVE = 1001;

//
// ****************************************************************************
// CDeleteProgressDlg
//

CDeleteProgressDlg::CDeleteProgressDlg(HWND parent, CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_PROGRESSDLG, parent, origin)
{
    ProgressBar = NULL;
    WantCancel = FALSE;
    LastTickCount = 0;
    TextCache[0] = 0;
    TextCacheIsDirty = FALSE;
    ProgressCache = 0;
    ProgressCacheIsDirty = FALSE;
}

void CDeleteProgressDlg::Set(const char* fileName, DWORD progress, BOOL dalayedPaint)
{
    lstrcpyn(TextCache, fileName != NULL ? fileName : "", MAX_PATH);
    TextCacheIsDirty = TRUE;

    if (progress != ProgressCache)
    {
        ProgressCache = progress;
        ProgressCacheIsDirty = TRUE;
    }

    if (!dalayedPaint)
        FlushDataToControls();
}

void CDeleteProgressDlg::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) // give the user a brief timeslice ...
            {
                if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
    }
}

BOOL CDeleteProgressDlg::GetWantCancel()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, TRUE)) // give the user a brief moment ...
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // repaint changed data (text + progress bars) every 100 ms
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        FlushDataToControls();
    }

    return WantCancel;
}

void CDeleteProgressDlg::FlushDataToControls()
{
    if (HWindow != NULL)
    {
        if (TextCacheIsDirty)
        {
            SetDlgItemText(HWindow, IDT_FILENAME, TextCache);
            TextCacheIsDirty = FALSE;
        }

        if (ProgressCacheIsDirty)
        {
            ProgressBar->SetProgress(ProgressCache, NULL);
            ProgressCacheIsDirty = FALSE;
        }
    }
}

INT_PTR
CDeleteProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CPathDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
#ifdef USE_DARKMODELIB
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        // use the Salamander-styled progress bar
        ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR);
        if (ProgressBar == NULL)
        {
            DestroyWindow(HWindow); // error -> do not show the dialog
            return FALSE;           // stop processing
        }

        break; // let DefDlgProc handle focus
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
#ifdef USE_DARKMODELIB
        RefreshWinLibDarkModeFromHost();
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        PluginDarkMode_HandleThemeMessage(HWindow, uMsg, lParam);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
#ifdef USE_DARKMODELIB
        LRESULT brush = 0;
        if (DarkModeHandleCtlColor(uMsg, wParam, lParam, brush))
            return (INT_PTR)brush;
#endif
        LRESULT darkBrush = 0;
        if (PluginDarkMode_HandleCtlColor(uMsg, wParam, lParam, &darkBrush))
            return (INT_PTR)darkBrush;
        if (PluginDarkMode_ShouldUseDark())
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(32, 32, 32));
            static HBRUSH s_darkBgBrush = CreateSolidBrush(RGB(32, 32, 32));
            return (INT_PTR)s_darkBgBrush;
        }
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            if (!WantCancel)
            {
                FlushDataToControls();

                if (SalamanderGeneral->SalMessageBox(HWindow, "Really cancel operation?", "SFTP",
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    WantCancel = TRUE;
                    EnableCancel(FALSE);
                }
            }
            return TRUE;
        }
        break;
    }

    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCalcSizeProgressDlg
//

CCalcSizeProgressDlg::CCalcSizeProgressDlg(HWND parent, CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_CALCSIZEDLG, parent, origin)
{
    ProgressBar = NULL;
    WantCancel = FALSE;
    LastTickCount = 0;
    TextCache[0] = 0;
    TextCacheIsDirty = FALSE;
    ProgressCache = 0;
    ProgressCacheIsDirty = FALSE;
}

void CCalcSizeProgressDlg::Set(const char* fileName, DWORD progress, BOOL dalayedPaint)
{
    lstrcpyn(TextCache, fileName != NULL ? fileName : "", MAX_PATH);
    TextCacheIsDirty = TRUE;

    if (progress != ProgressCache)
    {
        ProgressCache = progress;
        ProgressCacheIsDirty = TRUE;
    }

    if (!dalayedPaint)
        FlushDataToControls();
}

void CCalcSizeProgressDlg::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
    }
}

BOOL CCalcSizeProgressDlg::GetWantCancel()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, TRUE))
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        FlushDataToControls();
    }

    return WantCancel;
}

void CCalcSizeProgressDlg::FlushDataToControls()
{
    if (HWindow != NULL)
    {
        if (TextCacheIsDirty)
        {
            SetDlgItemText(HWindow, IDT_FILENAME, TextCache);
            TextCacheIsDirty = FALSE;
        }

        if (ProgressCacheIsDirty)
        {
            if (ProgressBar != NULL)
                ProgressBar->SetProgress(ProgressCache, NULL);
            ProgressCacheIsDirty = FALSE;
        }
    }
}

INT_PTR
CCalcSizeProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCalcSizeProgressDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
#ifdef USE_DARKMODELIB
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR);
        if (ProgressBar == NULL)
        {
            DestroyWindow(HWindow);
            return FALSE;
        }
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
#ifdef USE_DARKMODELIB
        RefreshWinLibDarkModeFromHost();
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        PluginDarkMode_HandleThemeMessage(HWindow, uMsg, lParam);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
#ifdef USE_DARKMODELIB
        LRESULT brush = 0;
        if (DarkModeHandleCtlColor(uMsg, wParam, lParam, brush))
            return (INT_PTR)brush;
#endif
        LRESULT darkBrush = 0;
        if (PluginDarkMode_HandleCtlColor(uMsg, wParam, lParam, &darkBrush))
            return (INT_PTR)darkBrush;
        if (PluginDarkMode_ShouldUseDark())
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(32, 32, 32));
            static HBRUSH s_darkBgBrush = CreateSolidBrush(RGB(32, 32, 32));
            return (INT_PTR)s_darkBgBrush;
        }
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            if (!WantCancel)
            {
                FlushDataToControls();
                WantCancel = TRUE;
                EnableCancel(FALSE);
            }
            return TRUE;
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CSftpTransferProgressDlg
//

static char g_ProgressConnName[128] = "";

CSftpTransferProgressDlg::CSftpTransferProgressDlg(HWND parent, CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_TRANSFERDLG, NULL, origin)
{
    CenterToWnd = parent != NULL ? parent : (SalamanderGeneral != NULL ? SalamanderGeneral->GetMainWindowHWND() : NULL);
    FileProgressBar = NULL;
    TotalProgressBar = NULL;
    WantCancel = FALSE;
    LastTickCount = 0;

    FromPathCache[0] = 0;
    ToPathCache[0] = 0;
    FileNameCache[0] = 0;
    StatusCache[0] = 0;
    TotalStatusCache[0] = 0;

    TextCacheIsDirty = FALSE;
    FileProgressCache = 0;
    TotalProgressCache = 0;
    ProgressCacheIsDirty = FALSE;

    IsUpload = false;
    StartTick = 0;
    FileStartTick = 0;
    FileDoneBytes = 0;
    FileTotalBytes = 0;
    TotalDoneBytes = 0;
    TotalExpectedBytes = 0;
    CurrentFileIndex = 0;
    TotalFilesCount = 1;

    Worker = NULL;
    OwnsWorker = false;
    FS = NULL;
    IsBackground = FALSE;
    ConnName[0] = 0;
    FromConnName[0] = 0;
    ToConnName[0] = 0;
    NotifyTargetPath[0] = 0;
    NotifySourcePath[0] = 0;
    NotifyIsMove = FALSE;

    SimpleLook = TRUE;
    SimpleDlgHeight = 0;
    DetailedDlgHeight = 0;
    ConsListView = NULL;
    ItemsListView = NULL;
    ShowOnlyErrors = FALSE;
    OperationFinished = FALSE;

    DlgThread = NULL;
    MinClientWidth = 0;
    MinClientHeight = 0;
    InLayout = FALSE;
}

CSftpTransferProgressDlg::~CSftpTransferProgressDlg()
{
    DetachWorker();
    if (FS != NULL)
    {
        FS->UnregisterTransferDlg(this);
    }
    FS = NULL;
}

void CSftpTransferProgressDlg::ShowFromBackground()
{
    IsBackground = FALSE;
    if (HWindow != NULL && IsWindow(HWindow))
    {
        if (IsIconic(HWindow))
            ShowWindow(HWindow, SW_RESTORE);
        else
            ShowWindow(HWindow, SW_SHOW);
        SetForegroundWindow(HWindow);
        UpdateFromWorker();
    }
}

// ----------------------------------------------------------------------------
// CSftpProgressDlgThread implementation
// ----------------------------------------------------------------------------

CSftpProgressDlgThread::CSftpProgressDlgThread(CSftpTransferProgressDlg* dlg, HWND parent)
{
    Dlg = dlg;
    ParentWnd = parent;
    ThreadHandle = NULL;
    ThreadId = 0;
    InitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (Dlg != NULL)
        Dlg->SetDlgThread(this);
}

CSftpProgressDlgThread::~CSftpProgressDlgThread()
{
    WaitForExit(1000);
    if (ThreadHandle != NULL)
    {
        CloseHandle(ThreadHandle);
        ThreadHandle = NULL;
    }
    if (InitEvent != NULL)
    {
        CloseHandle(InitEvent);
        InitEvent = NULL;
    }
}

BOOL CSftpProgressDlgThread::Start()
{
    ResetEvent(InitEvent);
    ThreadHandle = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, &ThreadId);
    if (ThreadHandle == NULL)
        return FALSE;
    WaitForSingleObject(InitEvent, 5000);
    return (Dlg != NULL && Dlg->HWindow != NULL);
}

void CSftpProgressDlgThread::WaitForExit(DWORD timeoutMs)
{
    if (ThreadHandle != NULL)
    {
        WaitForSingleObject(ThreadHandle, timeoutMs);
    }
}

unsigned __stdcall CSftpProgressDlgThread::ThreadProc(void* param)
{
    CSftpProgressDlgThread* pThis = (CSftpProgressDlgThread*)param;
    if (pThis != NULL)
        return pThis->Run();
    return 0;
}

unsigned CSftpProgressDlgThread::Run()
{
    if (Dlg == NULL)
    {
        SetEvent(InitEvent);
        return 0;
    }

    HWND hDlg = Dlg->Create();
    SetEvent(InitEvent);

    if (hDlg == NULL)
    {
        return 0;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (Dlg->HWindow == NULL || !IsWindow(Dlg->HWindow) || !IsDialogMessage(Dlg->HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    _endthreadex(0);
    return 0;
}

void CSftpTransferProgressDlg::SetConnName(const char* name)
{
    if (name != NULL)
    {
        lstrcpynA(ConnName, name, sizeof(ConnName));
        if (FromConnName[0] == 0)
            lstrcpynA(FromConnName, name, sizeof(FromConnName));
        if (ToConnName[0] == 0)
            lstrcpynA(ToConnName, name, sizeof(ToConnName));
    }
    else
    {
        ConnName[0] = 0;
    }
}

void CSftpTransferProgressDlg::DetachWorker()
{
    if (HWindow != NULL && IsWindow(HWindow))
    {
        KillTimer(HWindow, 101);
    }
    if (Worker != NULL)
    {
        CSftpTransferWorker* w = Worker;
        bool owns = OwnsWorker;
        Worker = NULL;
        OwnsWorker = false;
        w->SetDlgHwnd(NULL);
        w->SetObserver(NULL);
        if (owns)
        {
            w->Stop();
            delete w;
        }
    }
}

static bool SftpIsPathRemote(const char* p)
{
    if (p == NULL || p[0] == 0)
        return false;
    if (p[0] == '[')
        return false;
    if (strncmp(p, "sftp://", 7) == 0 || strncmp(p, "scp://", 6) == 0)
        return true;
    if (p[0] == '/')
        return true;
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        return false;
    if ((p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'))
        return false;
    return true;
}

static void SftpFormatTransferPath(const char* inPath, bool forceRemote, char* outBuf, int outBufSize, const char* connName = NULL)
{
    if (inPath == NULL || inPath[0] == 0)
    {
        lstrcpynA(outBuf, "-", outBufSize);
        return;
    }

    char full[MAX_PATH * 2];
    bool isRemote = forceRemote || SftpIsPathRemote(inPath);
    if (connName == NULL || connName[0] == 0)
        connName = g_ProgressConnName;

    if (isRemote && connName != NULL && connName[0] != 0 && inPath[0] != '[')
    {
        _snprintf_s(full, sizeof(full), _TRUNCATE, "[%s] %s", connName, inPath);
    }
    else
    {
        lstrcpynA(full, inPath, sizeof(full));
    }

    int maxChars = 80;
    if (maxChars >= outBufSize)
        maxChars = outBufSize - 1;
    PathCompactPathExA(outBuf, full, maxChars, 0);
}

void CSftpTransferProgressDlg::SetNotifyPaths(const char* targetPath, const char* sourcePath, BOOL isMove)
{
    if (targetPath != NULL)
        lstrcpynA(NotifyTargetPath, targetPath, sizeof(NotifyTargetPath));
    else
        NotifyTargetPath[0] = 0;
    if (sourcePath != NULL)
        lstrcpynA(NotifySourcePath, sourcePath, sizeof(NotifySourcePath));
    else
        NotifySourcePath[0] = 0;
    NotifyIsMove = isMove;
}

void CSftpTransferProgressDlg::SetOperationInfo(bool upload, const char* fromPath, const char* toPath,
                                               int totalFiles, unsigned __int64 totalExpectedBytes,
                                               const char* connName, const char* toConnName)
{
    if (connName != NULL && connName[0] != 0)
    {
        SetConnName(connName);
        lstrcpynA(FromConnName, connName, sizeof(FromConnName));
    }
    if (toConnName != NULL && toConnName[0] != 0)
    {
        lstrcpynA(ToConnName, toConnName, sizeof(ToConnName));
    }
    else if (FromConnName[0] != 0)
    {
        lstrcpynA(ToConnName, FromConnName, sizeof(ToConnName));
    }

    IsUpload = upload;
    StartTick = GetTickCount();
    FileStartTick = StartTick;
    TotalFilesCount = (totalFiles > 0) ? totalFiles : 1;
    TotalExpectedBytes = totalExpectedBytes;
    TotalDoneBytes = 0;
    CurrentFileIndex = 0;
    FileDoneBytes = 0;
    FileTotalBytes = 0;
    FileProgressCache = 0;
    TotalProgressCache = 0;

    bool fromIsRemote = !upload || SftpIsPathRemote(fromPath);
    bool toIsRemote = upload || SftpIsPathRemote(toPath);
    const char* fromConn = (fromIsRemote && FromConnName[0] != 0) ? FromConnName : (fromIsRemote && ConnName[0] != 0 ? ConnName : NULL);
    const char* toConn = (toIsRemote && ToConnName[0] != 0) ? ToConnName : (toIsRemote && ConnName[0] != 0 ? ConnName : NULL);

    SftpFormatTransferPath(fromPath, fromIsRemote, FromPathCache, sizeof(FromPathCache), fromConn);
    SftpFormatTransferPath(toPath, toIsRemote, ToPathCache, sizeof(ToPathCache), toConn);

    lstrcpynA(FileNameCache, "...", sizeof(FileNameCache));
    lstrcpynA(StatusCache, "Connecting...", sizeof(StatusCache));
    if (TotalFilesCount > 1)
        _snprintf_s(TotalStatusCache, _TRUNCATE, "Total: 0 / %d items", TotalFilesCount);
    else
        TotalStatusCache[0] = 0;

    TextCacheIsDirty = TRUE;
    ProgressCacheIsDirty = TRUE;

    if (HWindow != NULL)
    {
        FlushDataToControls();
    }
}

void CSftpTransferProgressDlg::SetCurrentFile(const char* fileName, unsigned __int64 fileSize)
{
    const char* base = strrchr(fileName, '\\');
    const char* b2 = strrchr(fileName, '/');
    if (b2 > base)
        base = b2;
    base = (base != NULL) ? base + 1 : fileName;

    lstrcpynA(FileNameCache, base, sizeof(FileNameCache));
    FileStartTick = GetTickCount();
    FileDoneBytes = 0;
    FileTotalBytes = fileSize;
    FileProgressCache = 0;

    CurrentFileIndex++;
    if (CurrentFileIndex > TotalFilesCount)
        TotalFilesCount = CurrentFileIndex;

    if (fileSize >= 1048576)
        _snprintf_s(StatusCache, _TRUNCATE, "0.0 / %.1f MB  (0.00 MB/s)", (double)fileSize / 1048576.0);
    else
        _snprintf_s(StatusCache, _TRUNCATE, "0 / %I64u kB  (0.00 MB/s)", fileSize / 1024);

    if (TotalFilesCount > 1)
        _snprintf_s(TotalStatusCache, _TRUNCATE, "Total: item %d of %d", CurrentFileIndex, TotalFilesCount);
    else
        TotalStatusCache[0] = 0;

    TextCacheIsDirty = TRUE;
    ProgressCacheIsDirty = TRUE;
    FlushDataToControls();
}

void CSftpTransferProgressDlg::UpdateFileProgress(unsigned __int64 done, unsigned __int64 total)
{
    FileDoneBytes = done;
    if (total > 0)
        FileTotalBytes = total;

    DWORD now = GetTickCount();
    DWORD elapsed = now - FileStartTick;
    double speedMB = elapsed > 0 ? ((double)done * 1000.0 / elapsed) / 1048576.0 : 0.0;

    char etaStr[64] = "";
    if (speedMB > 0.01 && FileTotalBytes > done)
    {
        unsigned __int64 remBytes = FileTotalBytes - done;
        int remSec = (int)((double)remBytes / (speedMB * 1048576.0));
        if (remSec >= 3600)
            _snprintf_s(etaStr, _TRUNCATE, " - ETA: %d:%02d:%02d", remSec / 3600, (remSec % 3600) / 60, remSec % 60);
        else
            _snprintf_s(etaStr, _TRUNCATE, " - ETA: %02d:%02d", remSec / 60, remSec % 60);
    }

    if (FileTotalBytes > 0)
    {
        if (FileTotalBytes >= 1048576)
        {
            _snprintf_s(StatusCache, _TRUNCATE, "%.1f / %.1f MB  (%.2f MB/s)%s",
                        (double)done / 1048576.0, (double)FileTotalBytes / 1048576.0, speedMB, etaStr);
        }
        else
        {
            _snprintf_s(StatusCache, _TRUNCATE, "%I64u / %I64u kB  (%.2f MB/s)%s",
                        done / 1024, FileTotalBytes / 1024, speedMB, etaStr);
        }
        FileProgressCache = (DWORD)(done * 1000 / FileTotalBytes);
    }
    else
    {
        _snprintf_s(StatusCache, _TRUNCATE, "%I64u kB  (%.2f MB/s)", done / 1024, speedMB);
        FileProgressCache = 0;
    }

    if (TotalFilesCount > 1)
    {
        _snprintf_s(TotalStatusCache, _TRUNCATE, "Total: item %d of %d", CurrentFileIndex, TotalFilesCount);
        TotalProgressCache = (DWORD)(((CurrentFileIndex - 1) * 1000 + FileProgressCache) / TotalFilesCount);
    }
    else
    {
        TotalProgressCache = FileProgressCache;
    }

    TextCacheIsDirty = TRUE;
    ProgressCacheIsDirty = TRUE;
}

void CSftpTransferProgressDlg::UpdateTotalProgress(int fileIndex, unsigned __int64 totalBytesDone)
{
    CurrentFileIndex = fileIndex;
    TotalDoneBytes = totalBytesDone;
    if (TotalFilesCount > 1)
    {
        _snprintf_s(TotalStatusCache, _TRUNCATE, "Total: item %d of %d", CurrentFileIndex, TotalFilesCount);
        TotalProgressCache = (DWORD)((CurrentFileIndex - 1) * 1000 / TotalFilesCount);
    }
    TextCacheIsDirty = TRUE;
    ProgressCacheIsDirty = TRUE;
}

void CSftpTransferProgressDlg::AttachWorker(CSftpTransferWorker* worker, bool ownsWorker)
{
    Worker = worker;
    OwnsWorker = ownsWorker;
    if (Worker != NULL)
    {
        Worker->SetObserver(this);
        if (HWindow != NULL)
        {
            Worker->SetDlgHwnd(HWindow);
        }
    }
}

static int SftpGetSysIconIndex(const char* fullPath, bool isDir)
{
    SHFILEINFOA sfi;
    memset(&sfi, 0, sizeof(sfi));
    DWORD attrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (SHGetFileInfoA(fullPath, attrs, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON))
    {
        return sfi.iIcon;
    }
    return 0;
}

void CSftpTransferProgressDlg::InitListViews()
{
    ConsListView = GetDlgItem(HWindow, IDL_CONNECTIONS);
    ItemsListView = GetDlgItem(HWindow, IDL_OPERATIONS);
    if (ConsListView == NULL || ItemsListView == NULL)
        return;

    ListView_SetExtendedListViewStyle(ConsListView,
        ListView_GetExtendedListViewStyle(ConsListView) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetExtendedListViewStyle(ItemsListView,
        ListView_GetExtendedListViewStyle(ItemsListView) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LV_COLUMN lvc;
    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;

    // ConsListView columns: ID (50px), Akce (200px), Stav (200px)
    lvc.iSubItem = 0; lvc.cx = 50; lvc.pszText = (LPSTR)"ID";
    ListView_InsertColumn(ConsListView, 0, &lvc);
    lvc.iSubItem = 1; lvc.cx = 200; lvc.pszText = (LPSTR)"Akce";
    ListView_InsertColumn(ConsListView, 1, &lvc);
    lvc.iSubItem = 2; lvc.cx = 250; lvc.pszText = (LPSTR)"Stav";
    ListView_InsertColumn(ConsListView, 2, &lvc);

    // ItemsListView columns: Popis (320px), Stav (250px)
    lvc.iSubItem = 0; lvc.cx = 320; lvc.pszText = (LPSTR)"Popis";
    ListView_InsertColumn(ItemsListView, 0, &lvc);
    lvc.iSubItem = 1; lvc.cx = 250; lvc.pszText = (LPSTR)"Stav";
    ListView_InsertColumn(ItemsListView, 1, &lvc);

    // System ImageList for small file icons
    SHFILEINFOA sfi;
    memset(&sfi, 0, sizeof(sfi));
    HIMAGELIST hSysIL = (HIMAGELIST)SHGetFileInfoA("C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
    if (hSysIL != NULL)
    {
        ListView_SetImageList(ItemsListView, hSysIL, LVSIL_SMALL);
    }
}

void CSftpTransferProgressDlg::SetColumnWidths()
{
    if (HWindow == NULL || !IsWindow(HWindow) || IsIconic(HWindow))
        return;

    RECT rc;
    if (ConsListView != NULL && IsWindow(ConsListView))
    {
        GetClientRect(ConsListView, &rc);
        int w = rc.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
        if (w < 200) w = 200;
        int cxID = 45;
        int cxAct = (int)((w - cxID) * 0.35);
        if (cxAct < 120) cxAct = 120;
        int cxStatus = w - cxID - cxAct;
        if (cxStatus < 140) cxStatus = 140;

        ListView_SetColumnWidth(ConsListView, 0, cxID);
        ListView_SetColumnWidth(ConsListView, 1, cxAct);
        ListView_SetColumnWidth(ConsListView, 2, cxStatus);
    }

    if (ItemsListView != NULL && IsWindow(ItemsListView))
    {
        GetClientRect(ItemsListView, &rc);
        int w = rc.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
        if (w < 200) w = 200;
        int cxDesc = (int)(w * 0.70);
        if (cxDesc < 180) cxDesc = 180;
        int cxStatus = w - cxDesc;
        if (cxStatus < 100) cxStatus = 100;

        ListView_SetColumnWidth(ItemsListView, 0, cxDesc);
        ListView_SetColumnWidth(ItemsListView, 1, cxStatus);
    }
}

void CSftpTransferProgressDlg::LayoutDialog()
{
    if (HWindow == NULL || !IsWindow(HWindow) || IsIconic(HWindow))
        return;

    InLayout = TRUE;
    RECT clientRect;
    GetClientRect(HWindow, &clientRect);
    int clientW = clientRect.right;
    int clientH = clientRect.bottom;

    if (MinClientWidth <= 0)
    {
        MinClientWidth = clientW > 400 ? clientW : 550;
        MinClientHeight = SimpleDlgHeight > 0 ? SimpleDlgHeight : 220;
    }

    HDWP hdwp = BeginDeferWindowPos(22);
    if (hdwp != NULL)
    {
        int marginX = 10;
        int fullW = clientW - 2 * marginX;
        int xText = 80;
        int wText = clientW - xText - marginX;
        if (wText < 100) wText = 100;

        // 1. Horní informační řádky
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_TR_FROM_PATH), NULL,
                              0, 0, wText, 14, SWP_NOZORDER | SWP_NOMOVE);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_TR_TO_PATH), NULL,
                              0, 0, wText, 14, SWP_NOZORDER | SWP_NOMOVE);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_TR_TIME_LEFT), NULL,
                              0, 0, wText, 14, SWP_NOZORDER | SWP_NOMOVE);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_TR_ELAPSED_TIME), NULL,
                              0, 0, wText, 14, SWP_NOZORDER | SWP_NOMOVE);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_TR_STATUS), NULL,
                              0, 0, wText, 14, SWP_NOZORDER | SWP_NOMOVE);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDP_TR_FILE_PROGRESS), NULL,
                              0, 0, wText, 18, SWP_NOZORDER | SWP_NOMOVE);

        // 2. Horní lišta tlačítek - ukotvena vpravo
        RECT rHelp;
        GetWindowRect(GetDlgItem(HWindow, IDHELP), &rHelp);
        int btnW = rHelp.right - rHelp.left;
        int btnH = rHelp.bottom - rHelp.top;
        if (btnW <= 0) btnW = 75;
        if (btnH <= 0) btnH = 24;
        int gap = 6;

        POINT ptHelp = { rHelp.left, rHelp.top };
        ScreenToClient(HWindow, &ptHelp);
        int btnY = ptHelp.y;

        int curX = clientW - marginX - btnW;
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDHELP), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);
        curX -= (btnW + gap);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDCANCEL), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);
        curX -= (btnW + gap);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_PAUSERESUME), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);
        curX -= (btnW + gap);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_SHOWERRORS), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);
        curX -= (btnW + gap);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_SHOWDETAILS), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);
        curX -= (btnW + gap);
        hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_BACKGROUND), NULL, curX, btnY, btnW, btnH, SWP_NOZORDER);

        // 3. Dělící linka
        HWND hSplit = GetDlgItem(HWindow, IDC_DLGSPLITBAR);
        int ySplit = 180;
        if (hSplit != NULL)
        {
            RECT rSplit;
            GetWindowRect(hSplit, &rSplit);
            POINT ptSplit = { rSplit.left, rSplit.top };
            ScreenToClient(HWindow, &ptSplit);
            ySplit = ptSplit.y;
            hdwp = DeferWindowPos(hdwp, hSplit, NULL, marginX, ySplit, fullW, 2, SWP_NOZORDER);
        }

        if (!SimpleLook && ConsListView != NULL && ItemsListView != NULL)
        {
            int yTopArea = ySplit + 10;
            int yBottomBtns = clientH - btnH - 10;

            // Popisek Spojení
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_CONNECTIONS), NULL, marginX, yTopArea, fullW, 16, SWP_NOZORDER);

            // Výpočet dostupné výšky pro obě ListView
            int fixedBetween = btnH + gap + 18 + gap;
            int totalListH = yBottomBtns - (yTopArea + 18) - fixedBetween - gap;
            if (totalListH < 120) totalListH = 120;

            int consH = (int)(totalListH * 0.35);
            if (consH < 50) consH = 50;
            int itemsH = totalListH - consH;
            if (itemsH < 70) itemsH = 70;

            int yCons = yTopArea + 18;
            hdwp = DeferWindowPos(hdwp, ConsListView, NULL, marginX, yCons, fullW, consH, SWP_NOZORDER);

            // Tlačítka pod Spojením
            int yConsBtns = yCons + consH + gap;
            int curRightX = clientW - marginX - btnW;
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPCONSPAUSERESUME), NULL, curRightX, yConsBtns, btnW, btnH, SWP_NOZORDER);
            curRightX -= (btnW + gap);
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPCONSSTOP), NULL, curRightX, yConsBtns, btnW, btnH, SWP_NOZORDER);
            curRightX -= (btnW + gap + 15);
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPCONSSOLVEERROR), NULL, curRightX, yConsBtns, btnW + 15, btnH, SWP_NOZORDER);

            // Popisek Operace
            int yOperTitle = yConsBtns + btnH + gap;
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDT_OPERATIONSTEXT), NULL, marginX, yOperTitle, fullW, 16, SWP_NOZORDER);

            // ListView Operace
            int yItems = yOperTitle + 18;
            hdwp = DeferWindowPos(hdwp, ItemsListView, NULL, marginX, yItems, fullW, itemsH, SWP_NOZORDER);

            // Spodní lišta pod Operacemi
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPOPERSSHOWONLYERR), NULL, marginX, yBottomBtns + 2, 170, 20, SWP_NOZORDER);

            int curBtmX = clientW - marginX - btnW;
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPOPERSSKIP), NULL, curBtmX, yBottomBtns, btnW, btnH, SWP_NOZORDER);
            curBtmX -= (btnW + gap);
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPOPERSRETRY), NULL, curBtmX, yBottomBtns, btnW, btnH, SWP_NOZORDER);
            curBtmX -= (btnW + gap + 15);
            hdwp = DeferWindowPos(hdwp, GetDlgItem(HWindow, IDB_OPOPERSSOLVEERROR), NULL, curBtmX, yBottomBtns, btnW + 15, btnH, SWP_NOZORDER);
        }

        EndDeferWindowPos(hdwp);
        SetColumnWidths();
    }
    InLayout = FALSE;
}

void CSftpTransferProgressDlg::RefreshListViews()
{
    if (Worker != NULL)
    {
        Worker->GetTasksSnapshot(CachedTasks);
    }

    DisplayedTaskIndices.clear();
    int doneCount = 0;
    for (size_t i = 0; i < CachedTasks.size(); i++)
    {
        if (CachedTasks[i].Status == CSftpTransferTask::StatusDone ||
            CachedTasks[i].Status == CSftpTransferTask::StatusSkipped)
        {
            doneCount++;
        }

        if (ShowOnlyErrors)
        {
            if (CachedTasks[i].Status == CSftpTransferTask::StatusError)
                DisplayedTaskIndices.push_back(i);
        }
        else
        {
            DisplayedTaskIndices.push_back(i);
        }
    }

    // Aktualizace nadpisu Operace: (hotovo / celkem) dle vzoru FTP pluginu
    char opTitle[128];
    _snprintf_s(opTitle, sizeof(opTitle), _TRUNCATE, LoadStr(IDS_OPERDLGOPERS_TITLE), doneCount, (int)CachedTasks.size());
    SetDlgItemText(HWindow, IDT_OPERATIONSTEXT, opTitle);

    if (ConsListView != NULL)
    {
        ListView_SetItemCountEx(ConsListView, 1, LVSICF_NOSCROLL);
        InvalidateRect(ConsListView, NULL, FALSE);
    }
    if (ItemsListView != NULL)
    {
        ListView_SetItemCountEx(ItemsListView, (int)DisplayedTaskIndices.size(), LVSICF_NOSCROLL);
        InvalidateRect(ItemsListView, NULL, FALSE);
    }

    // Enable / disable errors button
    bool hasAnyErrors = false;
    for (size_t i = 0; i < CachedTasks.size(); i++)
    {
        if (CachedTasks[i].Status == CSftpTransferTask::StatusError)
        {
            hasAnyErrors = true;
            break;
        }
    }
    EnableWindow(GetDlgItem(HWindow, IDB_SHOWERRORS), hasAnyErrors ? TRUE : FALSE);
}

static const int s_detailedCtrlIds[] = {
    IDC_DLGSPLITBAR,
    IDT_CONNECTIONS,
    IDL_CONNECTIONS,
    IDB_OPCONSSOLVEERROR,
    IDB_OPCONSSTOP,
    IDB_OPCONSPAUSERESUME,
    IDT_OPERATIONSTEXT,
    IDL_OPERATIONS,
    IDB_OPOPERSSHOWONLYERR,
    IDB_OPOPERSSOLVEERROR,
    IDB_OPOPERSRETRY,
    IDB_OPOPERSSKIP,
    -1
};

void CSftpTransferProgressDlg::ShowControlsAndChangeSize(BOOL simple)
{
    int show = simple ? SW_HIDE : SW_SHOW;
    for (int i = 0; s_detailedCtrlIds[i] != -1; i++)
    {
        HWND hCtrl = GetDlgItem(HWindow, s_detailedCtrlIds[i]);
        if (hCtrl != NULL)
        {
            ShowWindow(hCtrl, show);
            EnableWindow(hCtrl, !simple);
        }
    }

    RECT rDlg;
    GetWindowRect(HWindow, &rDlg);
    int targetHeight = simple ? SimpleDlgHeight : DetailedDlgHeight;
    if (targetHeight > 0)
    {
        SetWindowPos(HWindow, NULL, 0, 0, rDlg.right - rDlg.left, targetHeight,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    SetDlgItemText(HWindow, IDB_SHOWDETAILS, simple ? "Detaily >>" : "<< Detaily");
    if (!simple)
    {
        SftpApplyDarkModeToWindow(HWindow);
        if (PluginDarkMode_ShouldUseDark())
        {
            COLORREF textCol = RGB(220, 220, 220);
            COLORREF bgCol = RGB(32, 32, 32);
            if (ConsListView)
            {
                ListView_SetTextColor(ConsListView, textCol);
                ListView_SetTextBkColor(ConsListView, bgCol);
                ListView_SetBkColor(ConsListView, bgCol);
                SetWindowTheme(ConsListView, L"DarkMode_Explorer", NULL);
                HWND hHdr = ListView_GetHeader(ConsListView);
                if (hHdr) SetWindowTheme(hHdr, L"DarkMode_ItemsView", NULL);
            }
            if (ItemsListView)
            {
                ListView_SetTextColor(ItemsListView, textCol);
                ListView_SetTextBkColor(ItemsListView, bgCol);
                ListView_SetBkColor(ItemsListView, bgCol);
                SetWindowTheme(ItemsListView, L"DarkMode_Explorer", NULL);
                HWND hHdr = ListView_GetHeader(ItemsListView);
                if (hHdr) SetWindowTheme(hHdr, L"DarkMode_ItemsView", NULL);
            }
        }
        LayoutDialog();
        if (ItemsListView != NULL)
        {
            RefreshListViews();
        }
    }
}


void CSftpTransferProgressDlg::ToggleSimpleLook()
{
    SimpleLook = !SimpleLook;
    ShowControlsAndChangeSize(SimpleLook);
}

void CSftpTransferProgressDlg::ShowNextError()
{
    if (SimpleLook)
    {
        ToggleSimpleLook();
    }
    CheckDlgButton(HWindow, IDB_OPOPERSSHOWONLYERR, BST_CHECKED);
    ShowOnlyErrors = TRUE;
    RefreshListViews();

    if (ItemsListView != NULL && ListView_GetItemCount(ItemsListView) > 0)
    {
        ListView_SetItemState(ItemsListView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(ItemsListView, 0, FALSE);
        SetFocus(ItemsListView);
    }
}

void CSftpTransferProgressDlg::UpdateFromWorker()
{
    if (Worker == NULL)
        return;

    if (IsBackground || !IsWindowVisible(HWindow))
    {
        // When running in background, do not touch window controls or title!
        // This prevents menu dismissing and cursor flickering in Open Salamander.
        return;
    }

    CSftpTransferState snap;
    Worker->GetStateSnapshot(snap);

    if (snap.CurrentItemIndex > 0)
        CurrentFileIndex = snap.CurrentItemIndex;
    if (snap.TotalItemsCount > 0)
        TotalFilesCount = snap.TotalItemsCount;

    FileDoneBytes = snap.CurrentFileDone;
    FileTotalBytes = snap.CurrentFileTotal;
    TotalDoneBytes = snap.TotalBytesDone;
    if (snap.TotalBytesExpected > 0)
        TotalExpectedBytes = snap.TotalBytesExpected;

    if (TotalExpectedBytes > 0)
    {
        TotalProgressCache = (DWORD)(TotalDoneBytes * 1000 / TotalExpectedBytes);
        if (TotalProgressCache > 1000)
            TotalProgressCache = 1000;
    }
    else if (FileTotalBytes > 0)
    {
        TotalProgressCache = (DWORD)(FileDoneBytes * 1000 / FileTotalBytes);
        if (TotalProgressCache > 1000)
            TotalProgressCache = 1000;
    }
    else
    {
        TotalProgressCache = 0;
    }

    // 1. Elapsed time
    DWORD now = GetTickCount();
    if (StartTick == 0)
        StartTick = now;
    DWORD elapsedSec = (now - StartTick) / 1000;
    char elBuf[64];
    if (elapsedSec < 60)
        _snprintf_s(elBuf, sizeof(elBuf), _TRUNCATE, "%u s", (unsigned)elapsedSec);
    else if (elapsedSec < 3600)
        _snprintf_s(elBuf, sizeof(elBuf), _TRUNCATE, "%u min %02u s", (unsigned)elapsedSec / 60, (unsigned)elapsedSec % 60);
    else
        _snprintf_s(elBuf, sizeof(elBuf), _TRUNCATE, "%u h %02u min", (unsigned)elapsedSec / 3600, ((unsigned)elapsedSec % 3600) / 60);
    SetDlgItemText(HWindow, IDT_TR_ELAPSED_TIME, elBuf);

    // 2. Remaining time
    char remBuf[64];
    if (snap.IsPaused)
    {
        strcpy_s(remBuf, "(pozastaveno)");
    }
    else if (OperationFinished)
    {
        strcpy_s(remBuf, snap.HasError ? "(chyba)" : "(hotovo)");
    }
    else if (snap.BytesPerSec > 100.0 && TotalExpectedBytes > TotalDoneBytes)
    {
        int remSec = (int)((double)(TotalExpectedBytes - TotalDoneBytes) / snap.BytesPerSec);
        if (remSec < 60)
            _snprintf_s(remBuf, sizeof(remBuf), _TRUNCATE, "%d s", remSec);
        else if (remSec < 3600)
            _snprintf_s(remBuf, sizeof(remBuf), _TRUNCATE, "%d min %02d s", remSec / 60, remSec % 60);
        else
            _snprintf_s(remBuf, sizeof(remBuf), _TRUNCATE, "%d h %02d min", remSec / 3600, (remSec % 3600) / 60);
    }
    else if (TotalExpectedBytes == 0 || snap.IsRunning)
    {
        strcpy_s(remBuf, "(výpočet...)");
    }
    else
    {
        strcpy_s(remBuf, "(neznámý)");
    }
    SetDlgItemText(HWindow, IDT_TR_TIME_LEFT, remBuf);

    // 3. Status text
    size_t errCount = 0;
    for (size_t i = 0; i < CachedTasks.size(); i++)
    {
        if (CachedTasks[i].Status == CSftpTransferTask::StatusError)
            errCount++;
    }
    if (snap.HasError)
        errCount++;

    char doneStr[32], totalStr[32];
    if (TotalExpectedBytes >= 1048576)
    {
        _snprintf_s(doneStr, sizeof(doneStr), _TRUNCATE, "%.1f MB", (double)TotalDoneBytes / 1048576.0);
        _snprintf_s(totalStr, sizeof(totalStr), _TRUNCATE, "%.1f MB", (double)TotalExpectedBytes / 1048576.0);
    }
    else
    {
        _snprintf_s(doneStr, sizeof(doneStr), _TRUNCATE, "%I64u kB", TotalDoneBytes / 1024);
        _snprintf_s(totalStr, sizeof(totalStr), _TRUNCATE, "%I64u kB", TotalExpectedBytes / 1024);
    }

    _snprintf_s(StatusCache, sizeof(StatusCache), _TRUNCATE, LoadStr(IDS_OPERDLG_STATUSTEXT),
                doneStr, totalStr, snap.BytesPerSec / 1048576.0);
    if (errCount > 0)
    {
        char errSuffix[32];
        _snprintf_s(errSuffix, sizeof(errSuffix), _TRUNCATE, ", %u chyb", (unsigned)errCount);
        strcat_s(StatusCache, errSuffix);
    }
    SetDlgItemText(HWindow, IDT_TR_STATUS, StatusCache);

    // 4. Progress bar
    if (FileProgressBar != NULL)
        FileProgressBar->SetProgress(TotalProgressCache, NULL);

    // 5. Title bar percentage update
    int pct = (int)(TotalProgressCache / 10);
    char curTitle[128];
    if (GetWindowText(HWindow, curTitle, sizeof(curTitle)) > 0)
    {
        const char* pParen = strchr(curTitle, ')');
        const char* baseTitle = (pParen != NULL) ? pParen + 2 : curTitle;
        char newTitle[256];
        _snprintf_s(newTitle, sizeof(newTitle), _TRUNCATE, "(%d %%) %s", pct, baseTitle);
        if (strcmp(curTitle, newTitle) != 0)
            SetWindowText(HWindow, newTitle);
    }

    // 6. Refresh list views
    RefreshListViews();
}

void CSftpTransferProgressDlg::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
    }
}

BOOL CSftpTransferProgressDlg::GetWantCancel()
{
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, TRUE))
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 80)
    {
        LastTickCount = ticks;
        FlushDataToControls();
    }

    return WantCancel;
}

void CSftpTransferProgressDlg::FlushDataToControls()
{
    if (HWindow != NULL)
    {
        if (TextCacheIsDirty)
        {
            SetDlgItemText(HWindow, IDT_TR_FROM_PATH, FromPathCache);
            SetDlgItemText(HWindow, IDT_TR_TO_PATH, ToPathCache);
            SetDlgItemText(HWindow, IDT_TR_STATUS, StatusCache);
            TextCacheIsDirty = FALSE;
        }

        if (ProgressCacheIsDirty)
        {
            if (FileProgressBar != NULL)
                FileProgressBar->SetProgress(TotalProgressCache, NULL);
            ProgressCacheIsDirty = FALSE;
        }
    }
}

INT_PTR CSftpTransferProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
#ifdef USE_DARKMODELIB
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        SftpRegisterTransferDlgHwnd(HWindow);

        const char* fromCName = FromConnName[0] != 0 ? FromConnName : ConnName;
        const char* toCName = ToConnName[0] != 0 ? ToConnName : fromCName;
        if (fromCName[0] != 0 || toCName[0] != 0)
        {
            char curTitle[128];
            if (GetWindowText(HWindow, curTitle, sizeof(curTitle)) > 0 && curTitle[0] != '[')
            {
                char newTitle[256];
                if (fromCName[0] != 0 && toCName[0] != 0 && _stricmp(fromCName, toCName) != 0)
                {
                    _snprintf_s(newTitle, sizeof(newTitle), _TRUNCATE, "[%s -> %s] %s", fromCName, toCName, curTitle);
                }
                else
                {
                    const char* cname = fromCName[0] != 0 ? fromCName : toCName;
                    _snprintf_s(newTitle, sizeof(newTitle), _TRUNCATE, "[%s] %s", cname, curTitle);
                }
                SetWindowText(HWindow, newTitle);
            }
        }

        FileProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_TR_FILE_PROGRESS);
        TotalProgressBar = NULL;
        if (FileProgressBar == NULL)
        {
            DestroyWindow(HWindow);
            return FALSE;
        }

        RECT rDlg, rSplit;
        GetWindowRect(HWindow, &rDlg);
        HWND hSplit = GetDlgItem(HWindow, IDC_DLGSPLITBAR);
        if (hSplit != NULL)
        {
            GetWindowRect(hSplit, &rSplit);
            SimpleDlgHeight = rSplit.top - rDlg.top + 1;
        }
        else
        {
            SimpleDlgHeight = 220;
        }
        DetailedDlgHeight = rDlg.bottom - rDlg.top;

        InitListViews();
        CheckDlgButton(HWindow, IDC_TR_CLOSE_ON_FINISH, SftpCloseTransferDlgOnFinish ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(HWindow, IDB_SHOWERRORS), FALSE);

        SimpleLook = TRUE;
        ShowControlsAndChangeSize(SimpleLook);

        SetTimer(HWindow, 101, 100, NULL);
        if (Worker != NULL)
        {
            Worker->SetObserver(this);
            Worker->SetDlgHwnd(HWindow);
        }
        if (CenterToWnd != NULL)
        {
            SalamanderGeneral->MultiMonCenterWindow(HWindow, CenterToWnd, TRUE);
        }
        SetWindowLongPtr(HWindow, GWL_EXSTYLE, GetWindowLongPtr(HWindow, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        break;
    }

    case WM_SIZE:
    {
        if (wParam != SIZE_MINIMIZED && !InLayout)
        {
            LayoutDialog();
        }
        break;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
        if (MinClientWidth > 0)
        {
            RECT rcW = { 0, 0, MinClientWidth, SimpleLook ? SimpleDlgHeight : DetailedDlgHeight };
            AdjustWindowRectEx(&rcW, (DWORD)GetWindowLongPtr(HWindow, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtr(HWindow, GWL_EXSTYLE));
            mmi->ptMinTrackSize.x = rcW.right - rcW.left;
            mmi->ptMinTrackSize.y = rcW.bottom - rcW.top;
        }
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 101)
        {
            if (Worker != NULL)
                UpdateFromWorker();
            else
            {
                KillTimer(HWindow, 101);
                FlushDataToControls();
            }
        }
        return 0;
    }

    case WM_DESTROY:
    {
        KillTimer(HWindow, 101);
        SftpUnregisterTransferDlgHwnd(HWindow);
        if (Worker != NULL)
        {
            CSftpTransferWorker* w = Worker;
            bool owns = OwnsWorker;
            Worker = NULL;
            OwnsWorker = false;
            w->SetDlgHwnd(NULL);
            w->SetObserver(NULL);
            if (owns)
            {
                w->Stop();
                delete w;
            }
        }
        FileProgressBar = NULL;
        TotalProgressBar = NULL;
        if (FS != NULL)
        {
            FS->UnregisterTransferDlg(this);
        }
        FS = NULL;
        PostQuitMessage(0);
        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (pnm == NULL)
            break;

        if (pnm->idFrom == IDL_CONNECTIONS)
        {
            if (pnm->code == LVN_GETDISPINFOA || pnm->code == LVN_GETDISPINFOW)
            {
                CSftpTransferState snap;
                if (Worker != NULL)
                    Worker->GetStateSnapshot(snap);

                int subItem = (pnm->code == LVN_GETDISPINFOA) ? ((NMLVDISPINFOA*)lParam)->item.iSubItem : ((NMLVDISPINFOW*)lParam)->item.iSubItem;
                const char* act = "";
                static char actBuf[MAX_PATH + 64];
                static char statBuf[256];

                if (subItem == 0)
                {
                    act = "1";
                }
                else if (subItem == 1)
                {
                    if (OperationFinished)
                    {
                        act = snap.HasError ? "Chyba" : "Dokončeno";
                    }
                    else if (snap.IsRunning && (snap.CurrentRemoteFile[0] != 0 || snap.CurrentLocalFile[0] != 0))
                    {
                        const char* fn = snap.CurrentRemoteFile[0] != 0 ? snap.CurrentRemoteFile : snap.CurrentLocalFile;
                        const char* s1 = strrchr(fn, '/');
                        const char* s2 = strrchr(fn, '\\');
                        if (s2 > s1) s1 = s2;
                        if (s1 != NULL) fn = s1 + 1;

                        _snprintf_s(actBuf, sizeof(actBuf), _TRUNCATE, LoadStr(IDS_OPERDLGCOACT_COPYING), fn);
                        act = actBuf;
                    }
                    else
                    {
                        act = snap.IsPaused ? "Pozastaveno" : (IsUpload ? "Odesílání souborů" : "Stahování souborů");
                    }
                }
                else if (subItem == 2)
                {
                    if (OperationFinished)
                    {
                        act = snap.HasError ? "Chyba" : "Dokončeno";
                    }
                    else if (snap.IsPaused)
                    {
                        act = "Pozastaveno";
                    }
                    else if (snap.HasError)
                    {
                        act = "Chyba";
                    }
                    else if (snap.IsRunning)
                    {
                        char curDoneStr[32], curTotalStr[32], curRemStr[64];
                        double spdMB = snap.BytesPerSec / 1048576.0;

                        if (snap.CurrentFileTotal >= 1048576)
                        {
                            _snprintf_s(curDoneStr, sizeof(curDoneStr), _TRUNCATE, "%.1f MB", (double)snap.CurrentFileDone / 1048576.0);
                            _snprintf_s(curTotalStr, sizeof(curTotalStr), _TRUNCATE, "%.1f MB", (double)snap.CurrentFileTotal / 1048576.0);
                        }
                        else
                        {
                            _snprintf_s(curDoneStr, sizeof(curDoneStr), _TRUNCATE, "%I64u kB", snap.CurrentFileDone / 1024);
                            _snprintf_s(curTotalStr, sizeof(curTotalStr), _TRUNCATE, "%I64u kB", snap.CurrentFileTotal / 1024);
                        }

                        int pct = (snap.CurrentFileTotal > 0) ? (int)(snap.CurrentFileDone * 100 / snap.CurrentFileTotal) : 0;
                        if (pct > 100) pct = 100;

                        if (snap.BytesPerSec > 100.0 && snap.CurrentFileTotal > snap.CurrentFileDone)
                        {
                            int rem = (int)((snap.CurrentFileTotal - snap.CurrentFileDone) / snap.BytesPerSec);
                            if (rem < 60)
                                _snprintf_s(curRemStr, sizeof(curRemStr), _TRUNCATE, "%d s", rem);
                            else if (rem < 3600)
                                _snprintf_s(curRemStr, sizeof(curRemStr), _TRUNCATE, "%d min %02d sek", rem / 60, rem % 60);
                            else
                                _snprintf_s(curRemStr, sizeof(curRemStr), _TRUNCATE, "%d h %02d min", rem / 3600, (rem % 3600) / 60);
                        }
                        else
                        {
                            strcpy_s(curRemStr, "(výpočet...)");
                        }

                        _snprintf_s(statBuf, sizeof(statBuf), _TRUNCATE, LoadStr(IDS_OPERDLG_STATUSCONN),
                                    curDoneStr, curTotalStr, spdMB, pct, curRemStr);
                        act = statBuf;
                    }
                    else
                    {
                        act = "Čekání";
                    }
                }

                if (pnm->code == LVN_GETDISPINFOA)
                {
                    NMLVDISPINFOA* pDispA = (NMLVDISPINFOA*)lParam;
                    if (pDispA->item.mask & LVIF_TEXT)
                        lstrcpynA(pDispA->item.pszText, act, pDispA->item.cchTextMax);
                }
                else
                {
                    NMLVDISPINFOW* pDispW = (NMLVDISPINFOW*)lParam;
                    if ((pDispW->item.mask & LVIF_TEXT) && pDispW->item.pszText != NULL && pDispW->item.cchTextMax > 0)
                        MultiByteToWideChar(CP_ACP, 0, act, -1, pDispW->item.pszText, pDispW->item.cchTextMax);
                }
                return TRUE;
            }
        }
        else if (pnm->idFrom == IDL_OPERATIONS)
        {
            if (pnm->code == LVN_GETDISPINFOA || pnm->code == LVN_GETDISPINFOW)
            {
                int itemIdx = (pnm->code == LVN_GETDISPINFOA) ? ((NMLVDISPINFOA*)lParam)->item.iItem : ((NMLVDISPINFOW*)lParam)->item.iItem;
                int subItem = (pnm->code == LVN_GETDISPINFOA) ? ((NMLVDISPINFOA*)lParam)->item.iSubItem : ((NMLVDISPINFOW*)lParam)->item.iSubItem;
                UINT mask = (pnm->code == LVN_GETDISPINFOA) ? ((NMLVDISPINFOA*)lParam)->item.mask : ((NMLVDISPINFOW*)lParam)->item.mask;

                if (itemIdx >= 0 && (size_t)itemIdx < DisplayedTaskIndices.size())
                {
                    size_t actualIdx = DisplayedTaskIndices[itemIdx];
                    if (actualIdx < CachedTasks.size())
                    {
                        const CSftpTransferTask& t = CachedTasks[actualIdx];
                        if (mask & LVIF_IMAGE)
                        {
                            const char* p = !t.LocalPath.empty() ? t.LocalPath.c_str() : t.RemotePath.c_str();
                            int iconIdx = SftpGetSysIconIndex(p, t.IsDirectory);
                            if (pnm->code == LVN_GETDISPINFOA)
                                ((NMLVDISPINFOA*)lParam)->item.iImage = iconIdx;
                            else
                                ((NMLVDISPINFOW*)lParam)->item.iImage = iconIdx;
                        }

                        if (mask & LVIF_TEXT)
                        {
                            char textBuf[1024] = "";
                            if (subItem == 0)
                            {
                                const char* name = !t.LocalPath.empty() ? t.LocalPath.c_str() : t.RemotePath.c_str();
                                const char* s1 = strrchr(name, '/');
                                const char* s2 = strrchr(name, '\\');
                                if (s2 > s1) s1 = s2;
                                if (s1 != NULL) name = s1 + 1;

                                char sizeStr[64] = "";
                                if (t.FileSize > 0)
                                {
                                    if (t.FileSize >= 1048576)
                                        _snprintf_s(sizeStr, sizeof(sizeStr), _TRUNCATE, " (%.1f MB)", (double)t.FileSize / 1048576.0);
                                    else
                                        _snprintf_s(sizeStr, sizeof(sizeStr), _TRUNCATE, " (%I64u kB)", t.FileSize / 1024);
                                }

                                const char* fromPath = (t.TaskType == CSftpTransferTask::TaskDownload) ? t.RemotePath.c_str() : t.LocalPath.c_str();
                                const char* toPath = (t.TaskType == CSftpTransferTask::TaskDownload) ? t.LocalPath.c_str() : t.RemotePath.c_str();

                                char fromDir[MAX_PATH], toDir[MAX_PATH];
                                lstrcpynA(fromDir, fromPath, sizeof(fromDir));
                                char* pSlash = strrchr(fromDir, '/');
                                char* pBackslash = strrchr(fromDir, '\\');
                                if (pBackslash > pSlash) pSlash = pBackslash;
                                if (pSlash != NULL && pSlash != fromDir) *pSlash = 0;

                                lstrcpynA(toDir, toPath, sizeof(toDir));
                                pSlash = strrchr(toDir, '/');
                                pBackslash = strrchr(toDir, '\\');
                                if (pBackslash > pSlash) pSlash = pBackslash;
                                if (pSlash != NULL && pSlash != toDir) *pSlash = 0;

                                _snprintf_s(textBuf, sizeof(textBuf), _TRUNCATE,
                                            LoadStr(t.DeleteSourceOnSuccess ? IDS_OPERDOPDS_MOVE1 : IDS_OPERDOPDS_COPY1),
                                            name, sizeStr, fromDir, toDir);
                            }
                            else if (subItem == 1)
                            {
                                switch (t.Status)
                                {
                                case CSftpTransferTask::StatusWaiting:
                                    lstrcpynA(textBuf, LoadStr(IDS_OPERDLGOPSTS_WAITING), sizeof(textBuf));
                                    break;
                                case CSftpTransferTask::StatusRunning:
                                    lstrcpynA(textBuf, LoadStr(IDS_OPERDLGOPSTS_PROCESSING), sizeof(textBuf));
                                    break;
                                case CSftpTransferTask::StatusDone:
                                    lstrcpynA(textBuf, LoadStr(IDS_OPERDLGOPSTS_FINISHED), sizeof(textBuf));
                                    break;
                                case CSftpTransferTask::StatusError:
                                    if (!t.ErrorMsg.empty())
                                        _snprintf_s(textBuf, sizeof(textBuf), _TRUNCATE, LoadStr(IDS_OPERDLGOPSTS_FAILED), t.ErrorMsg.c_str());
                                    else
                                        lstrcpynA(textBuf, "Chyba", sizeof(textBuf));
                                    break;
                                case CSftpTransferTask::StatusSkipped:
                                    lstrcpynA(textBuf, LoadStr(IDS_OPERDLGOPSTS_SKIPPED), sizeof(textBuf));
                                    break;
                                }
                            }

                            if (pnm->code == LVN_GETDISPINFOA)
                            {
                                NMLVDISPINFOA* pA = (NMLVDISPINFOA*)lParam;
                                lstrcpynA(pA->item.pszText, textBuf, pA->item.cchTextMax);
                            }
                            else
                            {
                                NMLVDISPINFOW* pW = (NMLVDISPINFOW*)lParam;
                                if (pW->item.pszText != NULL && pW->item.cchTextMax > 0)
                                    MultiByteToWideChar(CP_ACP, 0, textBuf, -1, pW->item.pszText, pW->item.cchTextMax);
                            }
                        }
                    }
                }
                return TRUE;
            }
        }
        break;
    }


    case WM_APP_SFTP_WORKER_UPDATE:
    {
        UpdateFromWorker();
        return TRUE;
    }

    case WM_APP_SFTP_WORKER_FINISHED:
    {
        KillTimer(HWindow, 101);
        OperationFinished = TRUE;

        CSftpTransferState snap;
        if (Worker != NULL)
        {
            Worker->GetStateSnapshot(snap);
            UpdateFromWorker();
        }

        bool hasError = snap.HasError && !snap.Cancelled;
        bool closeOnFinish = (IsDlgButtonChecked(HWindow, IDC_TR_CLOSE_ON_FINISH) == BST_CHECKED);

        if (NotifyTargetPath[0] != 0)
            SalamanderGeneral->PostChangeOnPathNotification(NotifyTargetPath, TRUE);
        if (NotifyIsMove && NotifySourcePath[0] != 0)
            SalamanderGeneral->PostChangeOnPathNotification(NotifySourcePath, TRUE);

        if (!hasError && closeOnFinish && !snap.Cancelled)
        {
            if (CenterToWnd != NULL)
                EnableWindow(CenterToWnd, TRUE);
            DestroyWindow(HWindow);
            return TRUE;
        }

        // Keep dialog open!
        EnableWindow(GetDlgItem(HWindow, IDB_BACKGROUND), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDB_PAUSERESUME), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDB_OPCONSPAUSERESUME), FALSE);

        HWND hCancel = GetDlgItem(HWindow, IDCANCEL);
        if (hCancel != NULL)
        {
            SetWindowText(hCancel, "&Zavřít");
            EnableWindow(hCancel, TRUE);
            SendMessage(HWindow, DM_SETDEFID, IDCANCEL, 0);
            SendMessage(hCancel, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
            SetFocus(hCancel);
        }

        if (hasError)
        {
            SetDlgItemText(HWindow, IDT_TR_TIME_LEFT, "(chyba)");
            char curTitle[256];
            GetWindowText(HWindow, curTitle, sizeof(curTitle));
            char newTitle[300];
            _snprintf_s(newTitle, sizeof(newTitle), _TRUNCATE, "(chyba) %s", curTitle);
            SetWindowText(HWindow, newTitle);
            EnableWindow(GetDlgItem(HWindow, IDB_SHOWERRORS), TRUE);
        }
        else
        {
            SetDlgItemText(HWindow, IDT_TR_TIME_LEFT, "(hotovo)");
            char curTitle[256];
            GetWindowText(HWindow, curTitle, sizeof(curTitle));
            char newTitle[300];
            _snprintf_s(newTitle, sizeof(newTitle), _TRUNCATE, "(hotovo) %s", curTitle);
            SetWindowText(HWindow, newTitle);
        }

        if (hasError && snap.ErrorMsg[0] != 0 && !snap.Cancelled)
        {
            if (IsBackground)
            {
                ShowWindow(HWindow, SW_SHOW);
                IsBackground = FALSE;
            }
        }
        return TRUE;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
#ifdef USE_DARKMODELIB
        RefreshWinLibDarkModeFromHost();
        WinLibApplyDarkMode(HWindow);
#endif
        SftpApplyDarkModeToWindow(HWindow);
        PluginDarkMode_HandleThemeMessage(HWindow, uMsg, lParam);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    {
#ifdef USE_DARKMODELIB
        LRESULT brush = 0;
        if (DarkModeHandleCtlColor(uMsg, wParam, lParam, brush))
            return (INT_PTR)brush;
#endif
        LRESULT darkBrush = 0;
        if (PluginDarkMode_HandleCtlColor(uMsg, wParam, lParam, &darkBrush))
            return (INT_PTR)darkBrush;
        if (PluginDarkMode_ShouldUseDark())
        {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(32, 32, 32));
            static HBRUSH s_darkBgBrush = CreateSolidBrush(RGB(32, 32, 32));
            return (INT_PTR)s_darkBgBrush;
        }
        break;
    }

    case WM_CTLCOLORBTN:
    {
        // DO NOT return a solid dark brush for standard push buttons!
        // Button theming is handled via SetWindowTheme(child, L"DarkMode_Explorer", NULL).
        break;
    }

    case WM_COMMAND:
    {
        WORD cmdId = LOWORD(wParam);
        if (cmdId == IDB_BACKGROUND)
        {
            IsBackground = TRUE;
            if (CenterToWnd != NULL)
                EnableWindow(CenterToWnd, TRUE);
            ShowWindow(HWindow, SW_HIDE);
            return TRUE;
        }
        else if (cmdId == IDB_SHOWDETAILS)
        {
            ToggleSimpleLook();
            return TRUE;
        }
        else if (cmdId == IDB_SHOWERRORS)
        {
            ShowNextError();
            return TRUE;
        }
        else if (cmdId == IDB_PAUSERESUME || cmdId == IDB_OPCONSPAUSERESUME)
        {
            if (Worker != NULL)
            {
                bool paused = !Worker->IsPaused();
                Worker->SetPaused(paused);
                SetDlgItemText(HWindow, IDB_PAUSERESUME, paused ? "Pokračovat" : "Pauza");
                SetDlgItemText(HWindow, IDB_OPCONSPAUSERESUME, paused ? "Pokračovat" : "Pauza");
                UpdateFromWorker();
            }
            return TRUE;
        }
        else if (cmdId == IDB_OPCONSSTOP)
        {
            if (Worker != NULL)
            {
                if (SalamanderGeneral->SalMessageBox(HWindow, "Opravdu zastavit přenos na tomto spojení?", LoadStr(IDS_PLUGINNAME),
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    Worker->Cancel();
                }
            }
            return TRUE;
        }
        else if (cmdId == IDC_TR_CLOSE_ON_FINISH)
        {
            SftpCloseTransferDlgOnFinish = (IsDlgButtonChecked(HWindow, IDC_TR_CLOSE_ON_FINISH) == BST_CHECKED);
            SaveSftpConfigurationImmediately(HWindow);
            return TRUE;
        }
        else if (cmdId == IDB_OPOPERSSHOWONLYERR)
        {
            ShowOnlyErrors = (IsDlgButtonChecked(HWindow, IDB_OPOPERSSHOWONLYERR) == BST_CHECKED);
            RefreshListViews();
            return TRUE;
        }
        else if (cmdId == IDCANCEL)
        {
            if (OperationFinished)
            {
                if (CenterToWnd != NULL)
                    EnableWindow(CenterToWnd, TRUE);
                DestroyWindow(HWindow);
                return TRUE;
            }

            if (!WantCancel)
            {
                if (SalamanderGeneral->SalMessageBox(HWindow, "Opravdu přerušit operaci?", LoadStr(IDS_PLUGINNAME),
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    WantCancel = TRUE;
                    EnableCancel(FALSE);
                    if (Worker != NULL)
                        Worker->Cancel();
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// Transfer progress (with speed)
//

static CSftpTransferProgressDlg* g_ProgDlg = NULL;
static HWND g_ProgMainWnd = NULL;
static char g_ProgFile[MAX_PATH] = "";
static DWORD g_ProgStartTick = 0;

// file overwrite state during transfer
static HWND g_OvrParent = NULL;
static int g_OvrMode = 0;        // 0 = ask, 1 = overwrite all, 2 = skip all
static bool g_OvrCancel = false; // user chose Cancel
static bool g_ProgCancel = false; // user clicked Cancel in transfer progress dialog
static int g_SyncMode = 0;       // 1 = synchronization (skip matching, don't ask)

static bool SftpIsCancelled()
{
    if (g_OvrCancel || g_ProgCancel)
        return true;
    if (g_ProgDlg != NULL && g_ProgDlg->GetWantCancel())
    {
        g_ProgCancel = true;
        return true;
    }
    return false;
}

// returns: 1 = overwrite, 0 = skip, -1 = cancel whole operation
static int SftpAskOverwrite(const char* targetName)
{
    if (g_OvrMode == 1)
        return 1;
    if (g_OvrMode == 2)
        return 0;
    const char* base = strrchr(targetName, '\\');
    const char* b2 = strrchr(targetName, '/');
    if (b2 > base)
        base = b2;
    base = (base != NULL) ? base + 1 : targetName;
    int r = SalamanderGeneral->DialogOverwrite(g_OvrParent != NULL ? g_OvrParent : g_ProgMainWnd,
                                               BUTTONS_YESALLSKIPCANCEL,
                                               base, "target file", base, "source");
    switch (r)
    {
    case DIALOG_YES:
        return 1;
    case DIALOG_ALL:
        g_OvrMode = 1;
        return 1;
    case DIALOG_SKIP:
        return 0;
    case DIALOG_SKIPALL:
        g_OvrMode = 2;
        return 0;
    default:
        return -1; // Cancel
    }
}

// partially existing target: 1 = resume, 2 = transfer again, -1 = cancel
static int SftpAskResume(const char* targetName, unsigned __int64 have, unsigned __int64 total)
{
    const char* base = strrchr(targetName, '\\');
    const char* b2 = strrchr(targetName, '/');
    if (b2 > base) base = b2;
    base = (base != NULL) ? base + 1 : targetName;
    char msg[600];
    _snprintf_s(msg, _TRUNCATE,
                "File \"%s\" already partially exists (%I64u of %I64u bytes).\n\n"
                "Resume interrupted transfer?\n\n"
                "Yes = resume (continue)\nNo = transfer everything again\nCancel = cancel operation",
                base, have, total);
    int r = SalamanderGeneral->SalMessageBox(g_OvrParent != NULL ? g_OvrParent : g_ProgMainWnd, msg,
                                             "SFTP - resume transfer", MB_YESNOCANCEL | MB_ICONQUESTION);
    return r == IDYES ? 1 : (r == IDNO ? 2 : -1);
}

// local file size (0 if not found)
static unsigned __int64 LocalFileSize(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
        return 0;
    ULARGE_INTEGER sz;
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    return sz.QuadPart;
}

// local modification time as unix time (0 if not found)
static unsigned __int64 LocalMTime(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
        return 0;
    ULARGE_INTEGER u;
    u.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    u.HighPart = fad.ftLastWriteTime.dwHighDateTime;
// 100ns from 1601 -> seconds from 1970
    return (u.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

// synchronization: should file be skipped? (same size and local is not older)
static bool SyncSkipDownload(CSftpConnection& conn, const char* remote, const char* local)
{
    if (GetFileAttributes(local) == INVALID_FILE_ATTRIBUTES)
        return false; // missing locally -> download
    unsigned __int64 lsize = LocalFileSize(local), rsize = 0;
    unsigned long perms, uid, gid, rmtime;
    if (!conn.StatFull(remote, rsize, perms, uid, gid, rmtime))
        return false;
    if (lsize != rsize)
        return false; // different size -> download
    return LocalMTime(local) >= (unsigned __int64)rmtime; // same size and local is not older -> skip
}

static bool SyncSkipUpload(CSftpConnection& conn, const char* local, const char* remote)
{
    if (conn.PathType(remote) != 1)
        return false; // missing remotely -> upload
    unsigned __int64 lsize = LocalFileSize(local), rsize = 0;
    unsigned long perms, uid, gid, rmtime;
    if (!conn.StatFull(remote, rsize, perms, uid, gid, rmtime))
        return false;
    if (lsize != rsize)
        return false;
    return (unsigned __int64)rmtime >= LocalMTime(local); // same size and remote is not older -> skip
}

static bool SftpProgressCallback(void* ctx, const char* name, unsigned __int64 done, unsigned __int64 total)
{
    if (g_ProgDlg == NULL)
        return true;
    if (strcmp(g_ProgFile, name) != 0)
    {
        lstrcpyn(g_ProgFile, name, MAX_PATH);
        g_ProgStartTick = GetTickCount();
        g_ProgDlg->SetCurrentFile(name, total);
    }
    g_ProgDlg->UpdateFileProgress(done, total);
    if (g_ProgDlg->GetWantCancel())
    {
        g_ProgCancel = true;
        return false;
    }
    return true;
}

static CSftpConnection* g_ProgConn = NULL;

static void SftpProgressBegin(HWND parent, bool upload = false, const char* fromPath = NULL, const char* toPath = NULL,
                              int totalFiles = 1, unsigned __int64 totalExpectedBytes = 0,
                              const char* connName = NULL, CSftpConnection* conn = NULL,
                              const char* toConnName = NULL)
{
    if (connName != NULL)
        lstrcpynA(g_ProgressConnName, connName, sizeof(g_ProgressConnName));
    else
        g_ProgressConnName[0] = 0;

    g_ProgConn = conn;

    g_ProgMainWnd = parent;
    HWND pw;
    while ((pw = GetParent(g_ProgMainWnd)) != NULL && IsWindowEnabled(pw))
        g_ProgMainWnd = pw;
    EnableWindow(g_ProgMainWnd, FALSE);
    g_ProgFile[0] = 0;
    g_ProgStartTick = GetTickCount();
    g_OvrMode = 0;
    g_OvrCancel = false;
    g_ProgCancel = false;
    g_OvrParent = parent;
    g_ProgDlg = new CSftpTransferProgressDlg(g_ProgMainWnd, ooStatic);
    if (g_ProgDlg != NULL && g_ProgDlg->Create() != NULL)
    {
        SetForegroundWindow(g_ProgDlg->HWindow);
        g_ProgDlg->SetOperationInfo(upload, fromPath, toPath, totalFiles, totalExpectedBytes, connName, toConnName);
        g_OvrParent = g_ProgDlg->HWindow;
        if (g_ProgConn != NULL)
            g_ProgConn->SetProgressCallback(SftpProgressCallback, NULL);
    }
    else
    {
        if (g_ProgDlg != NULL)
            delete g_ProgDlg;
        g_ProgDlg = NULL;
        EnableWindow(g_ProgMainWnd, TRUE);
    }
}

static void SftpProgressEnd()
{
    if (g_ProgConn != NULL)
    {
        g_ProgConn->SetProgressCallback(NULL, NULL);
        g_ProgConn = NULL;
    }
    g_ProgressConnName[0] = 0;
    if (g_ProgDlg != NULL)
    {
        if (g_ProgDlg->GetWantCancel())
            g_ProgCancel = true;
        EnableWindow(g_ProgMainWnd, TRUE);
        DestroyWindow(g_ProgDlg->HWindow);
        delete g_ProgDlg;
        g_ProgDlg = NULL;
    }
}

//
// ****************************************************************************
// CPluginFSInterface
//

CPluginFSInterface::CPluginFSInterface()
{
    Path[0] = 0;
    PathError = FALSE;
    FatalError = FALSE;
    CalledFromDisconnectDialog = FALSE;
    memset(&Profile, 0, sizeof(Profile));
    Profile.Port = 22;
    Profile.Valid = false;
    ActiveTransferDlg = NULL;
}

void CPluginFSInterface::RegisterTransferDlg(CSftpTransferProgressDlg* dlg)
{
    if (dlg == NULL)
        return;
    for (size_t i = 0; i < ActiveTransferDlgs.size(); i++)
    {
        if (ActiveTransferDlgs[i] == dlg)
            return;
    }
    ActiveTransferDlgs.push_back(dlg);
    ActiveTransferDlg = dlg;
}

void CPluginFSInterface::UnregisterTransferDlg(CSftpTransferProgressDlg* dlg)
{
    for (auto it = ActiveTransferDlgs.begin(); it != ActiveTransferDlgs.end(); ++it)
    {
        if (*it == dlg)
        {
            ActiveTransferDlgs.erase(it);
            break;
        }
    }
    ActiveTransferDlg = ActiveTransferDlgs.empty() ? NULL : ActiveTransferDlgs.back();
}

CPluginFSInterface::~CPluginFSInterface()
{
    while (!ActiveTransferDlgs.empty())
    {
        CSftpTransferProgressDlg* dlg = ActiveTransferDlgs.back();
        ActiveTransferDlgs.pop_back();
        if (dlg != NULL)
        {
            dlg->SetFS(NULL);
            dlg->DetachWorker();
            if (IsWindow(dlg->HWindow))
            {
                DestroyWindow(dlg->HWindow);
            }
        }
    }
    ActiveTransferDlg = NULL;
    TransferWorker.Stop();
    Conn.Disconnect();
}

void CPluginFSInterface::ShowTransferDialog(HWND parent)
{
    BOOL shown = FALSE;
    for (size_t i = 0; i < ActiveTransferDlgs.size(); i++)
    {
        CSftpTransferProgressDlg* dlg = ActiveTransferDlgs[i];
        if (dlg != NULL && IsWindow(dlg->HWindow))
        {
            dlg->ShowFromBackground();
            shown = TRUE;
        }
    }
    if (!shown)
    {
        SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_NOTRANSFERS_ACTIVE), LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
    }
}

bool CPluginFSInterface::EnsureConnected(HWND parent)
{
    SftpTraceLog("CPluginFSInterface::EnsureConnected: calling SftpEnsureConnected");
    bool r = SftpEnsureConnected(parent, Conn, Profile);
    SftpTraceLog("CPluginFSInterface::EnsureConnected: SftpEnsureConnected returned %d", (int)r);
    return r;
}

void CPluginFSInterface::HostPrefix(char* out, int outSize) const
{
    char portpart[16] = "";
    if (Profile.Port != 0 && Profile.Port != 22)
        _snprintf_s(portpart, _TRUNCATE, ":%d", Profile.Port);
    if (Profile.User[0] != 0)
        _snprintf_s(out, outSize, _TRUNCATE, "//%s@%s%s", Profile.User, Profile.Host, portpart);
    else
        _snprintf_s(out, outSize, _TRUNCATE, "//%s%s", Profile.Host, portpart);
}

void WINAPI
CPluginFSInterface::ReleaseObject(HWND parent)
{
    if (Path[0] != 0) // if the FS is initialized, remove our disk-cache copies when closing
    {
        // build a unique name for this FS root in the disk cache (covers all files from this FS)
        char uniqueFileName[2 * MAX_PATH];
        strcpy(uniqueFileName, AssignedFSName);
        strcat(uniqueFileName, ":");
        SalamanderGeneral->GetRootPath(uniqueFileName + strlen(uniqueFileName), Path);
        // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
        // to lowercase makes the disk cache behave case-insensitively as well
        SalamanderGeneral->ToLowerCase(uniqueFileName);
        SalamanderGeneral->RemoveFilesFromCache(uniqueFileName);
    }
}

BOOL WINAPI
CPluginFSInterface::GetRootPath(char* userPart)
{
    strcpy(userPart, "/"); // SFTP root je "/"
    return TRUE;
}

// "//user@host[:port]" for current profile (path prefix displayed)
static void SftpHostPrefix(const CSftpProfile& prof, char* out, int outSize)
{
    char portpart[16] = "";
    if (prof.Port != 0 && prof.Port != 22)
        _snprintf_s(portpart, _TRUNCATE, ":%d", prof.Port);
    if (prof.User[0] != 0)
        _snprintf_s(out, outSize, _TRUNCATE, "//%s@%s%s", prof.User, prof.Host, portpart);
    else
        _snprintf_s(out, outSize, _TRUNCATE, "//%s%s", prof.Host, portpart);
}

// from "//user@host/path" returns pointer to start of remote path (after host); otherwise returns input
static const char* SftpStripHost(const char* userPart)
{
    if (userPart == NULL)
        return "";
    if ((userPart[0] == '/' || userPart[0] == '\\') &&
        (userPart[1] == '/' || userPart[1] == '\\'))
    {
        const char* p = userPart + 2;
        while (*p != 0 && *p != '/' && *p != '\\')
            p++;
        return (*p != 0) ? p : "/";
    }
    return userPart;
}

// if "//user@host[:port]/..." contains a different host than current profile, update profile
// and force new connection (allows entering sftp://user@host/ directly in address bar)
static void SftpParseHostInto(CSftpProfile& prof, CSftpConnection& conn, const char* userPart)
{
    if (userPart == NULL || (userPart[0] != '/' && userPart[0] != '\\') ||
        (userPart[1] != '/' && userPart[1] != '\\'))
        return;
    const char* p = userPart + 2;
    const char* slash = p;
    while (*slash != 0 && *slash != '/' && *slash != '\\')
        slash++;
    int len = (int)(slash - p);
    char buf[320];
    if (len <= 0 || len >= (int)sizeof(buf))
        return;
    memcpy(buf, p, len);
    buf[len] = 0;
    char user[128] = "", host[256] = "";
    char* hostpart = buf;
    char* at = strchr(buf, '@');
    if (at != NULL)
    {
        *at = 0;
        lstrcpyn(user, buf, sizeof(user));
        hostpart = at + 1;
    }
    int port = 22;
    char* colon = strchr(hostpart, ':');
    if (colon != NULL)
    {
        *colon = 0;
        port = atoi(colon + 1);
    }
    lstrcpyn(host, hostpart, sizeof(host));
    if (host[0] == 0)
        return;
    if (_stricmp(host, prof.Host) != 0 || (user[0] != 0 && _stricmp(user, prof.User) != 0))
    {
        lstrcpyn(prof.Host, host, sizeof(prof.Host));
        if (user[0] != 0)
            lstrcpyn(prof.User, user, sizeof(prof.User));
        prof.Port = (port > 0) ? port : 22;
        prof.Valid = true;
        prof.Name[0] = 0;
        for (int i = 0; i < SftpProfileCount; i++)
        {
            if (_stricmp(prof.Host, SftpProfiles[i].Host) == 0 &&
                (prof.User[0] == 0 || _stricmp(prof.User, SftpProfiles[i].User) == 0))
            {
                lstrcpyn(prof.Name, SftpProfiles[i].Name, sizeof(prof.Name));
                break;
            }
        }
        conn.Disconnect(); // different server -> new connection
    }
}

BOOL WINAPI
CPluginFSInterface::GetCurrentPath(char* userPart)
{
    char prefix[320];
    SftpHostPrefix(Profile, prefix, sizeof(prefix));
    _snprintf_s(userPart, MAX_PATH, _TRUNCATE, "%s%s", prefix, Path[0] != 0 ? Path : "/");
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::GetFullName(CFileData& file, int isDir, char* buf, int bufSize)
{
    char remote[MAX_PATH], prefix[320];
    SftpHostPrefix(Profile, prefix, sizeof(prefix));
    if (isDir == 2) // up-dir
        SftpParent(Path, remote, MAX_PATH);
    else
        SftpJoin(Path, file.Name, remote, MAX_PATH);
    _snprintf_s(buf, bufSize, _TRUNCATE, "%s%s", prefix, remote);
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize, BOOL& success)
{
    // 'path' is relative or absolute user-part; join with current path and prepend "fsName://host"
    const char* up = SftpStripHost(path);
    char full[MAX_PATH];
    if (up[0] == '/')
        lstrcpyn(full, up, MAX_PATH);
    else
        SftpJoin(Path[0] != 0 ? Path : "/", up, full, MAX_PATH);
    SftpNormalize(full);
    char prefix[320];
    SftpHostPrefix(Profile, prefix, sizeof(prefix));
    success = (int)(strlen(full) + strlen(prefix) + strlen(fsName) + 1) < pathSize;
    if (success)
        sprintf(path, "%s:%s%s", fsName, prefix, full);
    else
        SalamanderGeneral->SalMessageBox(parent, "Path is too long.", LoadStr(IDS_PLUGINNAME),
                                         MB_OK | MB_ICONEXCLAMATION);
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    if (userPart == NULL)
        userPart = "";
    return currentFSNameIndex == fsNameIndex && SftpIsSamePath(Path, SftpStripHost(userPart));
}

BOOL WINAPI
CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart)
{
    if (userPart == NULL)
        userPart = "";
    if (ConnectData.UseConnectData)
        return FALSE; // new connection from Connect dialog
    // one connection serves the entire server tree
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                               const char* userPart, char* cutFileName, BOOL* pathWasCut,
                               BOOL forceRefresh, int mode)
{
    SftpTraceLog("CPluginFSInterface::ChangePath: start currentFSNameIndex=%d fsName=%s userPart=%s mode=%d UseConnectData=%d",
                 currentFSNameIndex, fsName ? fsName : "", userPart ? userPart : "(null)", mode, ConnectData.UseConnectData);

    if (userPart == NULL)
        userPart = "";

    if (mode != 3 && (pathWasCut != NULL || cutFileName != NULL))
    {
        TRACE_E("Incorrect value of 'mode' in CPluginFSInterface::ChangePath().");
        mode = 3;
    }
    if (pathWasCut != NULL)
        *pathWasCut = FALSE;
    if (cutFileName != NULL)
        *cutFileName = 0;
    if (FatalError)
    {
        FatalError = FALSE;
        return FALSE;
    }

    HWND parent = SalamanderGeneral->GetMsgBoxParent();

    // from address bar "sftp://user@host/" optionally switch to different server
    if (ConnectData.UseConnectData)
    {
        Profile = ConnectData.Profile;
        Profile.Valid = true;
    }
    else
        SftpParseHostInto(Profile, Conn, userPart);

    if (!EnsureConnected(parent))
        return FALSE;

    // determine input path (without //host prefix)
    char path[MAX_PATH];
    if (*userPart == 0 && ConnectData.UseConnectData) // data z Connect dialogu
        lstrcpyn(path, ConnectData.UserPart, MAX_PATH);
    else
        lstrcpyn(path, SftpStripHost(userPart), MAX_PATH);

    if (path[0] == 0)
    {
        std::string home;
        if (Conn.GetHomeDir(home) && !home.empty())
            lstrcpyn(path, home.c_str(), MAX_PATH);
        else
            strcpy(path, "/");
    }

    // join relative path to current
    if (path[0] != '/')
    {
        char joined[MAX_PATH];
        SftpJoin(Path[0] != 0 ? Path : "/", path, joined, MAX_PATH);
        lstrcpyn(path, joined, MAX_PATH);
    }
    SftpNormalize(path);

    if (PathError)
    {
        PathError = FALSE;
        char parentPath[MAX_PATH];
        SftpParent(path, parentPath, MAX_PATH);
        if (SftpIsSamePath(path, parentPath) || SftpIsRoot(path))
        {
            char msg[2 * MAX_PATH];
            _snprintf_s(msg, _TRUNCATE, "Cannot list directory:\n%s:%s\n%s", fsName, path, Conn.LastError());
            SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        lstrcpyn(path, parentPath, MAX_PATH);
        if (pathWasCut != NULL)
            *pathWasCut = TRUE;
    }

    BOOL fileNameAlreadyCut = FALSE;
    while (1)
    {
        int type = Conn.PathType(path); // 0=not found,1=file,2=directory
        if (type == 2)
        {
            lstrcpyn(Path, path, MAX_PATH);
            SalamanderGeneral->AddPluginFSTimer(8000, this, SFTP_TIMER_KEEPALIVE);
            return TRUE;
        }
        // file or not found -> trim last component
        const char* slash = strrchr(path, '/');
        if (slash == NULL || slash == path || SftpIsRoot(path)) // already at root and it's not a directory -> fatal
        {
            char msg[2 * MAX_PATH];
            _snprintf_s(msg, _TRUNCATE, "Path does not exist or is not a directory:\n%s:%s", fsName, userPart);
            SalamanderGeneral->SalMessageBox(parent, msg, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        char lastComp[MAX_PATH];
        lstrcpyn(lastComp, slash + 1, MAX_PATH);
        char parentPath[MAX_PATH];
        SftpParent(path, parentPath, MAX_PATH);

        if (pathWasCut != NULL)
            *pathWasCut = TRUE;
        if (!fileNameAlreadyCut) // only first trim can be file name to focus
        {
            fileNameAlreadyCut = TRUE;
            if (cutFileName != NULL && type == 1)
                lstrcpyn(cutFileName, lastComp, MAX_PATH);
        }
        else if (cutFileName != NULL)
            *cutFileName = 0;

        lstrcpyn(path, parentPath, MAX_PATH);
    }
}

BOOL WINAPI
CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData,
                                    int& iconsType, BOOL forceRefresh)
{
    HWND parent = SalamanderGeneral->GetMsgBoxParent();
    if (!EnsureConnected(parent))
    {
        PathError = TRUE;
        return FALSE;
    }

    std::vector<CSftpEntry> entries;
    if (!Conn.ListDir(Path[0] != 0 ? Path : "/", entries))
    {
        // Try transparent reconnect once in case connection was dropped during idle
        if (EnsureConnected(parent) && Conn.ListDir(Path[0] != 0 ? Path : "/", entries))
        {
            // Successfully recovered
        }
        else
        {
            PathError = TRUE; // list error -> ChangePath will shorten path
            return FALSE;
        }
    }

    pluginData = new CPluginFSDataInterface(Path);
    if (pluginData == NULL)
    {
        TRACE_E("Low memory");
        FatalError = TRUE;
        return FALSE;
    }
    iconsType = pitFromRegistry; // icons by extension from registry (.pdf, .txt, ...)

    dir->SetFlags(SALDIRFLAG_IGNOREDUPDIRS);
    dir->SetValidData(VALID_DATA_EXTENSION | VALID_DATA_SIZE | VALID_DATA_TYPE |
                      VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_ATTRIBUTES |
                      VALID_DATA_HIDDEN | VALID_DATA_ISLINK);

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    // up-dir ".." (if not at root)
    if (!SftpIsRoot(Path))
    {
        CFileData up;
        memset(&up, 0, sizeof(up));
        up.Name = SalamanderGeneral->DupStr("..");
        up.NameLen = 2;
        up.Ext = up.Name + up.NameLen;
        up.Size = CQuadWord(0, 0);
        up.Attr = FILE_ATTRIBUTE_DIRECTORY;
        up.DosName = NULL;
        up.PluginData = (DWORD_PTR) new CFSData("", "", "");
        up.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        if (up.Name == NULL || !dir->AddDir(NULL, up, pluginData))
        {
            if (up.Name != NULL)
                SalamanderGeneral->Free(up.Name);
            dir->Clear(pluginData);
            delete pluginData;
            FatalError = TRUE;
            return FALSE;
        }
    }

    for (size_t i = 0; i < entries.size(); i++)
    {
        CSftpEntry& e = entries[i];
        CFileData file;
        memset(&file, 0, sizeof(file));
        file.Name = SalamanderGeneral->DupStr(e.Name.c_str());
        if (file.Name == NULL)
        {
            TRACE_E("Low memory");
            dir->Clear(pluginData);
            delete pluginData;
            FatalError = TRUE;
            return FALSE;
        }
        file.NameLen = e.Name.length();
        if (!sortByExtDirsAsFiles && e.IsDir)
            file.Ext = file.Name + file.NameLen; // directories have no extension
        else
        {
            char* s = strrchr(file.Name, '.');
            file.Ext = (s != NULL) ? s + 1 : file.Name + file.NameLen;
        }
        file.Size = CQuadWord((DWORD)(e.Size & 0xFFFFFFFF), (DWORD)(e.Size >> 32));
        file.Attr = e.IsDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
        ULONGLONG ll = (ULONGLONG)e.MTime * 10000000ULL + 116444736000000000ULL; // unix -> FILETIME
        file.LastWrite.dwLowDateTime = (DWORD)ll;
        file.LastWrite.dwHighDateTime = (DWORD)(ll >> 32);
        file.DosName = NULL;
        file.Hidden = (!e.Name.empty() && e.Name[0] == '.') ? 1 : 0; // unix hidden
        file.IsLink = e.IsLink ? 1 : 0;
        file.IsOffline = 0;
        file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

        // build permissions string (rwxr-xr-x)
        char rights[12];
        const char* pp = "rwxrwxrwx";
        rights[0] = e.IsLink ? 'l' : (e.IsDir ? 'd' : '-');
        for (int k = 0; k < 9; k++)
            rights[k + 1] = (e.Permissions & (1 << (8 - k))) ? pp[k] : '-';
        rights[10] = 0;
        CFSData* extData = new CFSData(rights, e.Owner.c_str(), e.Group.c_str());
        if (extData == NULL || !extData->IsGood())
        {
            if (extData != NULL)
                delete extData;
            SalamanderGeneral->Free(file.Name);
            dir->Clear(pluginData);
            delete pluginData;
            FatalError = TRUE;
            return FALSE;
        }
        file.PluginData = (DWORD_PTR)extData;

        BOOL ok = e.IsDir ? dir->AddDir(NULL, file, pluginData) : dir->AddFile(NULL, file, pluginData);
        if (!ok)
        {
            delete extData;
            SalamanderGeneral->Free(file.Name);
            dir->Clear(pluginData);
            delete pluginData;
            FatalError = TRUE;
            return FALSE;
        }
    }

    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    detach = FALSE;

    if (CalledFromDisconnectDialog || forceClose ||
        reason == FSTRYCLOSE_UNLOADCLOSEFS ||
        reason == FSTRYCLOSE_UNLOADCLOSEDETACHEDFS ||
        reason == FSTRYCLOSE_PLUGINCLOSEDETACHEDFS ||
        (SalamanderGeneral != NULL && SalamanderGeneral->IsCriticalShutdown()))
    {
        return TRUE;
    }

    if (reason == FSTRYCLOSE_CHANGEPATH)
    {
        if (SftpLeavePanelAction == 1) // Always disconnect
        {
            detach = FALSE;
            return TRUE;
        }
        if (SftpLeavePanelAction == 2 && canDetach) // Always keep
        {
            detach = TRUE;
            return TRUE;
        }

        // SftpLeavePanelAction == 0: Ask user
        if (canDetach)
        {
            MSGBOXEX_PARAMS params;
            memset(&params, 0, sizeof(params));
            params.HParent = SalamanderGeneral->GetMsgBoxParent();
            params.Flags = MSGBOXEX_YESNOCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
            params.Caption = LoadStr(IDS_PLUGINNAME);
            params.Text = LoadStr(IDS_CLOSECONINPANEL);

            int rememberChoice = 0;
            params.CheckBoxText = LoadStr(IDS_ALWAYSREMEMBER);
            params.CheckBoxValue = &rememberChoice;

            char buffer[128];
            sprintf(buffer, "%d\t%s\t%d\t%s", DIALOG_YES, LoadStr(IDS_DISCONNECTBUTTON),
                    DIALOG_NO, LoadStr(IDS_KEEPCONBUTTON));
            params.AliasBtnNames = buffer;

            int res = SalamanderGeneral->SalMessageBoxEx(&params);
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND());

            if (res == IDCANCEL)
            {
                return FALSE; // abort changing path, stay in panel
            }

            if (res == DIALOG_NO) // Keep
            {
                detach = TRUE;
                if (rememberChoice)
                {
                    SftpLeavePanelAction = 2; // Always keep
                    SaveSftpConfigurationImmediately(SalamanderGeneral->GetMsgBoxParent());
                }
                return TRUE;
            }

            // Otherwise DIALOG_YES -> Disconnect
            detach = FALSE;
            if (rememberChoice)
            {
                SftpLeavePanelAction = 1; // Always disconnect
                SaveSftpConfigurationImmediately(SalamanderGeneral->GetMsgBoxParent());
            }
            return TRUE;
        }
        else
        {
            // cannot detach: prompt if user wants to disconnect
            int res = SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(),
                                                       LoadStr(IDS_WANTDISCONNECT),
                                                       LoadStr(IDS_PLUGINNAME),
                                                       MB_YESNO | MB_ICONQUESTION);
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
            if (res == IDYES)
            {
                detach = FALSE;
                return TRUE;
            }
            return FALSE;
        }
    }

    return TRUE;
}

void WINAPI
CPluginFSInterface::Event(int event, DWORD param)
{
    char buf[MAX_PATH + 100];
    if (event == FSE_CLOSEORDETACHCANCELED)
    {
        sprintf(buf, "Close or detach of path \"%s\" was canceled (%s).", Path, (param == PANEL_LEFT ? "left" : "right"));
#ifdef SFTP_QUIET
        TRACE_I("Sftp: " << buf);
#else  // SFTP_QUIET
        SalamanderGeneral->ShowMessageBox(buf, "FS Event", MSGBOX_INFO);
#endif // SFTP_QUIET
    }

    if (event == FSE_OPENED)
    {
        sprintf(buf, "Path \"%s\" was opened in %s panel.", Path, (param == PANEL_LEFT ? "left" : "right"));
#ifdef SFTP_QUIET
        TRACE_I("Sftp: " << buf);
#else  // SFTP_QUIET
        SalamanderGeneral->ShowMessageBox(buf, "FS Event", MSGBOX_INFO);
#endif // SFTP_QUIET
    }

    if (event == FSE_DETACHED)
    {
        LastDetachedFS = this;

        sprintf(buf, "Path \"%s\" was detached (%s).", Path, (param == PANEL_LEFT ? "left" : "right"));
#ifdef SFTP_QUIET
        TRACE_I("Sftp: " << buf);
#else  // SFTP_QUIET
        SalamanderGeneral->ShowMessageBox(buf, "FS Event", MSGBOX_INFO);
#endif // SFTP_QUIET
    }

    if (event == FSE_ATTACHED)
    {
        if (this == LastDetachedFS)
            LastDetachedFS = NULL;

        sprintf(buf, "Path \"%s\" was attached (%s).", Path, (param == PANEL_LEFT ? "left" : "right"));
#ifdef SFTP_QUIET
        TRACE_I("Sftp: " << buf);
#else  // SFTP_QUIET
        SalamanderGeneral->ShowMessageBox(buf, "FS Event", MSGBOX_INFO);
#endif // SFTP_QUIET
    }

    if (event == FSE_ACTIVATEREFRESH) // the user activated Salamander (switched from another application)
    {
        // refresh the path;
        // we are inside CPluginFSInterface, so RefreshPanelPath cannot be used
        //    SalamanderGeneral->PostRefreshPanelPath((int)param);
        SalamanderGeneral->PostRefreshPanelFS(this);

        sprintf(buf, "Activate refresh on path \"%s\" (%s).", Path, (param == PANEL_LEFT ? "left" : "right"));
#ifdef SFTP_QUIET
        TRACE_I("Sftp: " << buf);
#else  // SFTP_QUIET
        SalamanderGeneral->ShowMessageBox(buf, "FS Event", MSGBOX_INFO);
#endif // SFTP_QUIET
    }

    if (event == FSE_TIMER && param == SFTP_TIMER_KEEPALIVE)
    {
        if (Conn.IsConnected())
        {
            Conn.SendKeepalive();
        }
        SalamanderGeneral->AddPluginFSTimer(8000, this, SFTP_TIMER_KEEPALIVE);
    }
}

DWORD WINAPI
CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_SHOWPROPERTIES |
           FS_SERVICE_CHANGEATTRS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMFS |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_QUICKRENAME |
           FS_SERVICE_COMMANDLINE |
           FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_SHOWSECURITYINFO |
           FS_SERVICE_CALCULATEOCCUPIEDSPACE |
           FS_SERVICE_GETPATHFORMAINWNDTITLE;
}

void WINAPI
CPluginFSInterface::ShowSecurityInfo(HWND parent)
{
    if (!Conn.IsConnected())
    {
        SalamanderGeneral->SalMessageBox(parent, "No active SFTP connection.", LoadStr(IDS_PLUGINNAME),
                                         MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string info;
    Conn.GetSecurityInfo(info);
    SalamanderGeneral->SalMessageBox(parent, info.empty() ? "(no information)" : info.c_str(),
                                     "SFTP Security Information", MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI
CPluginFSInterface::GetChangeDriveOrDisconnectItem(const char* fsName, char*& title, HICON& icon, BOOL& destroyIcon)
{
    char txt[2 * MAX_PATH + 102];
    // the text will be the FS path (in Salamander format)
    txt[0] = '\t';
    strcpy(txt + 1, fsName);
    const char* connName = Profile.Name[0] != 0 ? Profile.Name : (Profile.Host[0] != 0 ? Profile.Host : NULL);
    if (connName != NULL)
        sprintf(txt + strlen(txt), ":[%s] %s\t", connName, Path);
    else
        sprintf(txt + strlen(txt), ":%s\t", Path);
    // double any '&' characters so the path prints correctly
    SalamanderGeneral->DuplicateAmpersands(txt, 2 * MAX_PATH + 102);
    // append information about free space
    CQuadWord space;
    SalamanderGeneral->GetDiskFreeSpace(&space, Path, NULL);
    if (space != CQuadWord(-1, -1))
        SalamanderGeneral->PrintDiskSize(txt + strlen(txt), space, 0);
    title = SalamanderGeneral->DupStr(txt);
    if (title == NULL)
        return FALSE; // low-memory, no item will be shown

    SalamanderGeneral->GetRootPath(txt, Path);

    if (!SalamanderGeneral->GetFileIcon(txt, FALSE, &icon, SALICONSIZE_16, TRUE, TRUE))
        icon = NULL;
    // switched to our own implementation (lower memory use, working XOR icons)
    //SHFILEINFO shi;
    //if (SHGetFileInfo(txt, 0, &shi, sizeof(shi),
    //                  SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE))
    //{
    //  icon = shi.hIcon;  // icon successfully retrieved
    //}
    //else icon = NULL;  // no icon available
    destroyIcon = TRUE;
    return TRUE;
}

HICON WINAPI
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    char root[MAX_PATH];
    SalamanderGeneral->GetRootPath(root, Path);

    HICON icon;
    if (!SalamanderGeneral->GetFileIcon(root, FALSE, &icon, SALICONSIZE_16, TRUE, TRUE))
        icon = NULL;
    // switched to our own implementation (lower memory use, working XOR icons)
    //SHFILEINFO shi;
    //if (SHGetFileInfo(root, 0, &shi, sizeof(shi),
    //                  SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE))
    //{
    //  icon = shi.hIcon;  // icon successfully retrieved
    //}
    //else icon = NULL;  // no icon available (the standard one will be used)
    destroyIcon = TRUE;
    return icon;
}

void WINAPI
CPluginFSInterface::GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                  DWORD allowedEffects, DWORD keyState, DWORD* dropEffect)
{                                                                                       // if Copy and Move are both available, choose Move when both FS instances share the same root
    if ((*dropEffect & DROPEFFECT_MOVE) && *dropEffect != DROPEFFECT_MOVE &&            // otherwise there is no point in checking
        SalamanderGeneral->StrNICmp(srcFSPath, AssignedFSName, AssignedFSNameLen) == 0) // only paths on our FS are relevant
    {
        const char* src = srcFSPath + AssignedFSNameLen + 1;
        const char* tgt = tgtFSPath + AssignedFSNameLen + 1;
        if (SalamanderGeneral->HasTheSameRootPath(src, tgt))
            *dropEffect = DROPEFFECT_MOVE;
    }
}

void WINAPI
CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    if (Path[0] == 0)
        *retValue = CQuadWord(-1, -1);
    else
        SalamanderGeneral->GetDiskFreeSpace(retValue, Path, NULL);
}

BOOL WINAPI
CPluginFSInterface::GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset)
{
    const char* end = text + pathLen;
    const char* root = text; // pointer past the root portion of the path
    while (*root != 0 && *root != ':')
        root++;
    if (*root == ':')
    {
        root++;
        if ((root[0] == '/' || root[0] == '\\') && (root[1] == '/' || root[1] == '\\'))
            root += 2; // skip "//"
        while (root < end && *root != '/' && *root != '\\')
            root++; // skip "user@host[:port]"
        if (root < end && (*root == '/' || *root == '\\'))
            root++; // skip the leading slash of the remote path
    }

    const char* s = text + offset;
    if (s >= end)
        return FALSE;
    if (s < root)
        offset = (int)(root - text);
    else
    {
        if (*s == '/' || *s == '\\')
            s++;
        while (s < end && *s != '/' && *s != '\\')
            s++;
        offset = (int)(s - text);
    }
    return s < end;
}

BOOL WINAPI
CPluginFSInterface::GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return FALSE;

    const char* connName = Profile.Name[0] != 0 ? Profile.Name : Profile.Host;

    if (mode == 1) // "Directory Name Only"
    {
        char dirName[MAX_PATH] = "/";
        if (Path[0] != 0 && !(Path[0] == '/' && Path[1] == 0) && !(Path[0] == '\\' && Path[1] == 0))
        {
            const char* p = Path + strlen(Path);
            while (p > Path && (*(p - 1) == '/' || *(p - 1) == '\\'))
                p--;
            const char* end = p;
            while (p > Path && *(p - 1) != '/' && *(p - 1) != '\\')
                p--;
            int len = (int)(end - p);
            if (len > 0)
            {
                if (len >= MAX_PATH)
                    len = MAX_PATH - 1;
                memcpy(dirName, p, len);
                dirName[len] = 0;
            }
        }

        if (connName[0] != 0)
            _snprintf_s(buf, bufSize, _TRUNCATE, "[%s] %s", connName, dirName);
        else
            lstrcpyn(buf, dirName, bufSize);
        return TRUE;
    }
    else if (mode == 2) // "Shortened Path"
    {
        char prefix[320];
        SftpHostPrefix(Profile, prefix, sizeof(prefix));
        if (Path[0] == 0 || (Path[0] == '/' && Path[1] == 0) || (Path[0] == '\\' && Path[1] == 0))
        {
            if (connName[0] != 0)
                _snprintf_s(buf, bufSize, _TRUNCATE, "[%s] %s:%s/", connName, fsName, prefix);
            else
                _snprintf_s(buf, bufSize, _TRUNCATE, "%s:%s/", fsName, prefix);
            return TRUE;
        }
        const char* p = Path + strlen(Path);
        while (p > Path && (*(p - 1) == '/' || *(p - 1) == '\\'))
            p--;
        const char* end = p;
        while (p > Path && *(p - 1) != '/' && *(p - 1) != '\\')
            p--;
        // if root or only one level deep, return full path
        if (p <= Path + 1)
        {
            if (connName[0] != 0)
                _snprintf_s(buf, bufSize, _TRUNCATE, "[%s] %s:%s%s", connName, fsName, prefix, Path);
            else
                _snprintf_s(buf, bufSize, _TRUNCATE, "%s:%s%s", fsName, prefix, Path);
            return TRUE;
        }
        int len = (int)(end - p);
        if (connName[0] != 0)
            _snprintf_s(buf, bufSize, _TRUNCATE, "[%s] %s:%s/.../%.*s", connName, fsName, prefix, len, p);
        else
            _snprintf_s(buf, bufSize, _TRUNCATE, "%s:%s/.../%.*s", fsName, prefix, len, p);
        return TRUE;
    }

    return FALSE;
}

void WINAPI
CPluginFSInterface::ShowInfoDialog(const char* fsName, HWND parent)
{
    CQuadWord f;
    GetFSFreeSpace(&f);
    char num[100];
    if (f != CQuadWord(-1, -1))
        SalamanderGeneral->PrintDiskSize(num, f, 1);
    else
        strcpy(num, "(unknown)");

    char buf[1000];
    _snprintf_s(buf, _TRUNCATE, "SFTP connection\n\nPath: %s:%s", fsName, Path);
    SalamanderGeneral->SalMessageBox(parent, buf, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI
CPluginFSInterface::ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo)
{
    if (command[0] == 0)
        return FALSE;
    if (!EnsureConnected(parent))
        return TRUE;
    // execute command in current server directory
    char raw[2 * MAX_PATH];
    _snprintf_s(raw, _TRUNCATE, "cd \"%s\" && %s", Path[0] != 0 ? Path : "/", command);
    char full[3 * MAX_PATH];
    WrapCommandWithSftpServerPrefix(Profile.SftpServer, raw, full, sizeof(full));

    ShowCommandExecDialog(parent, command, full, Profile, this);
    command[0] = 0; // vyčisti command line
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::QuickRename(const char* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                char* newName, BOOL& cancel)
{
    // if the plugin opens its own dialog, it should use CSalamanderGeneralAbstract::AlterFileName
    // ('format' according to SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT))
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request standard dialog

    char buf[2 * MAX_PATH];
    // syntax check of name (no backslashes)
    if (newName[0] == 0 || strchr(newName, '/') != NULL || strchr(newName, '\\') != NULL)
    {
        SalamanderGeneral->SalMessageBox(parent, "Invalid name.", LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    // apply mask
    SalamanderGeneral->MaskName(buf, 2 * MAX_PATH, file.Name, newName);
    lstrcpyn(newName, buf, MAX_PATH);

    char remoteFrom[MAX_PATH], remoteTo[MAX_PATH];
    SftpJoin(Path, file.Name, remoteFrom, MAX_PATH);
    SftpJoin(Path, newName, remoteTo, MAX_PATH);

    if (!EnsureConnected(parent))
        return FALSE;
    if (!Conn.Rename(remoteFrom, remoteTo))
    {
        _snprintf_s(buf, _TRUNCATE, "Rename failed:\n%s", Conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, buf, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    SalamanderGeneral->PostChangeOnPathNotification(Path, isDir);
    return TRUE;
}

void WINAPI
CPluginFSInterface::AcceptChangeOnPathNotification(const char* fsName, const char* path, BOOL includingSubdirs)
{
    if (Path[0] == 0 || path == NULL || path[0] == 0)
        return;
    // 'path' can be either user-part ("/dir") or full FS path ("fsName:/dir") -> trim prefix
    const char* userPart = path;
    int fsNameLen = (int)strlen(fsName);
    if (SalamanderGeneral->StrNICmp(path, fsName, fsNameLen) == 0 && path[fsNameLen] == ':')
        userPart = path + fsNameLen + 1; // our FS path
    else if (path[0] != '/')
        return; // disk or foreign path -> not relevant to us

    userPart = SftpStripHost(userPart); // trim optional //host prefix

    // refresh panel when change affects our current path (or its subtree)
    if (SftpIsSamePath(userPart, Path) ||
        (includingSubdirs && strncmp(Path, userPart, strlen(userPart)) == 0))
        SalamanderGeneral->PostRefreshPanelFS(this);
}

BOOL WINAPI
CPluginFSInterface::CreateDir(const char* fsName, int mode, HWND parent, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request standard dialog

    if (newName[0] == 0)
    {
        cancel = TRUE;
        return TRUE;
    }

    char remote[MAX_PATH];
    if (newName[0] == '/')
        lstrcpyn(remote, newName, MAX_PATH);
    else
        SftpJoin(Path, newName, remote, MAX_PATH);
    SftpNormalize(remote);

    if (!EnsureConnected(parent))
        return FALSE;
    if (!Conn.MakeDir(remote))
    {
        char eb[600];
        _snprintf_s(eb, _TRUNCATE, "Cannot create directory:\n%s", Conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    SalamanderGeneral->PostChangeOnPathNotification(Path, FALSE);
    // focus newly created directory (just its name)
    const char* slash = strrchr(remote, '/');
    lstrcpyn(newName, slash != NULL ? slash + 1 : remote, MAX_PATH);
    return TRUE;
}

void WINAPI
CPluginFSInterface::ViewFile(const char* fsName, HWND parent,
                             CSalamanderForViewFileOnFSAbstract* salamander,
                             CFileData& file)
{
    // build a unique file name for the disk cache (standard Salamander path format: fsName://user@host:port/path/file:size:timestamp)
    char uniqueFileName[3 * MAX_PATH + 64];
    strcpy(uniqueFileName, fsName);
    strcat(uniqueFileName, ":");
    int len = (int)strlen(uniqueFileName);
    GetFullName(file, 0 /* isDir = 0 */, uniqueFileName + len, 2 * MAX_PATH);
    // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
    // to lowercase makes the disk cache behave case-insensitively as well
    SalamanderGeneral->ToLowerCase(uniqueFileName);
    // append size and modification time so changing file on server or switching servers invalidates cache
    len = (int)strlen(uniqueFileName);
    _snprintf_s(uniqueFileName + len, sizeof(uniqueFileName) - len, _TRUNCATE,
                ":%I64u:%08lx%08lx", file.Size.Value,
                file.LastWrite.dwHighDateTime, file.LastWrite.dwLowDateTime);

    // obtain the cache copy name
    BOOL fileExists;
    const char* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName, file.Name, NULL, fileExists);
    if (tmpFileName == NULL)
        return; // fatal error

    // if needed, download file to disk cache via SFTP
    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);
    if (!fileExists)
    {
        char remote[MAX_PATH];
        SftpJoin(Path, file.Name, remote, MAX_PATH);
        if (EnsureConnected(parent) && Conn.Download(remote, tmpFileName))
        {
            newFileOK = TRUE;
            HANDLE hFile = HANDLES_Q(CreateFile(tmpFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                NULL, OPEN_EXISTING, 0, NULL));
            if (hFile != INVALID_HANDLE_VALUE)
            {
                DWORD err;
                SalamanderGeneral->SalGetFileSize(hFile, newFileSize, err);
                HANDLES(CloseHandle(hFile));
            }
        }
        else
        {
            char errorText[3 * MAX_PATH + 100];
            _snprintf_s(errorText, _TRUNCATE, "Cannot download file %s.\n%s", file.Name, Conn.LastError());
            SalamanderGeneral->SalMessageBox(parent, errorText, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        }
    }

    // open the viewer
    HANDLE fileLock;
    BOOL fileLockOwner;
    if (!fileExists && !newFileOK || // open the viewer only if the file copy is OK
        !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    { // on failure reset the "lock"
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    // call FreeFileNameInCache to pair with AllocFileNameInCache (connects
    // the viewer and the disk cache)
    salamander->FreeFileNameInCache(uniqueFileName, fileExists, newFileOK,
                                    newFileSize, fileLock, fileLockOwner, FALSE /* do not delete immediately after closing the viewer */);
}

// recursive deletion of file/directory on SFTP
static bool SftpDeleteRecursive(CSftpConnection& conn, const char* remote, bool isDir)
{
    if (!isDir)
        return conn.RemoveFile(remote);
    std::vector<CSftpEntry> entries;
    if (conn.ListDir(remote, entries))
    {
        for (size_t i = 0; i < entries.size(); i++)
        {
            char child[MAX_PATH];
            SftpJoin(remote, entries[i].Name.c_str(), child, MAX_PATH);
            if (!SftpDeleteRecursive(conn, child, entries[i].IsDir))
                return false;
        }
    }
    return conn.RemoveDir(remote);
}

// number of files skipped (overwrite dialog "Skip" or synchronization) by the recursive
// transfer functions below; Move must not delete the source when this is non-zero
static int g_SkippedCount = 0;

// recursive download of file/directory from SFTP to disk
static bool SftpDownloadRecursive(CSftpConnection& conn, const char* remote, const char* local, bool isDir)
{
    if (SftpIsCancelled())
        return false;
    if (!isDir)
    {
        if (g_SyncMode) // synchronization: without asking, only missing/changed
        {
            if (SyncSkipDownload(conn, remote, local))
            {
                g_SkippedCount++;
                return true;
            }
            if (SftpIsCancelled())
                return false;
            return conn.Download(remote, local);
        }
        if (GetFileAttributes(local) != INVALID_FILE_ATTRIBUTES) // local file already exists
        {
            unsigned __int64 localSize = LocalFileSize(local);
            unsigned __int64 remoteSize = 0;
            // offer resume when local is smaller than remote (SFTP only)
            if (!conn.IsScpMode() && localSize > 0 &&
                conn.RemoteFileSize(remote, remoteSize) && remoteSize > localSize)
            {
                int r = SftpAskResume(local, localSize, remoteSize);
                if (r == -1) { g_OvrCancel = true; return false; }
                if (SftpIsCancelled()) return false;
                if (r == 1) return conn.Download(remote, local, localSize); // resume
                // r == 2 -> overwrite (continue with full download)
            }
            else
            {
                int a = SftpAskOverwrite(local);
                if (a == 0)
                {
                    g_SkippedCount++; // skipped - source must not be deleted by Move
                    return true;      // skip (= success)
                }
                if (a < 0)
                {
                    g_OvrCancel = true;
                    return false;
                }
            }
        }
        if (SftpIsCancelled())
            return false;
        return conn.Download(remote, local);
    }
    if (SftpIsCancelled())
        return false;
    CreateDirectory(local, NULL);
    std::vector<CSftpEntry> entries;
    if (!conn.ListDir(remote, entries))
        return false;
    for (size_t i = 0; i < entries.size(); i++)
    {
        if (SftpIsCancelled())
            return false;
        char r[MAX_PATH], l[2 * MAX_PATH];
        SftpJoin(remote, entries[i].Name.c_str(), r, MAX_PATH);
        lstrcpyn(l, local, 2 * MAX_PATH);
        SalamanderGeneral->SalPathAppend(l, entries[i].Name.c_str(), 2 * MAX_PATH);
        if (!SftpDownloadRecursive(conn, r, l, entries[i].IsDir))
            return false;
    }
    return true;
}

// recursive upload of file/directory from disk to SFTP
// True only for junctions and symlinks (reparse tags MOUNT_POINT/SYMLINK); other reparse points
// (cloud placeholders, dedup) are ordinary content and are traversed normally.
static bool IsLinkReparsePoint(const char* path)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    FindClose(h);
    return (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           (fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK);
}

static bool SftpUploadRecursive(CSftpConnection& conn, const char* local, const char* remote, bool isDir)
{
    if (SftpIsCancelled())
        return false;
    if (!isDir)
    {
        if (g_SyncMode) // synchronization: without asking, only missing/changed
        {
            if (SyncSkipUpload(conn, local, remote))
            {
                g_SkippedCount++;
                return true;
            }
            if (SftpIsCancelled())
                return false;
            return conn.Upload(local, remote);
        }
        if (conn.PathType(remote) == 1) // remote file already exists
        {
            unsigned __int64 localSize = LocalFileSize(local);
            unsigned __int64 remoteSize = 0;
            // offer resume when remote is smaller than local (SFTP only)
            if (!conn.IsScpMode() &&
                conn.RemoteFileSize(remote, remoteSize) && remoteSize > 0 && remoteSize < localSize)
            {
                int r = SftpAskResume(remote, remoteSize, localSize);
                if (r == -1) { g_OvrCancel = true; return false; }
                if (SftpIsCancelled()) return false;
                if (r == 1) return conn.Upload(local, remote, remoteSize); // resume
                // r == 2 -> overwrite
            }
            else
            {
                int a = SftpAskOverwrite(remote);
                if (a == 0)
                {
                    g_SkippedCount++; // skipped - source must not be deleted by Move
                    return true;
                }
                if (a < 0)
                {
                    g_OvrCancel = true;
                    return false;
                }
            }
        }
        if (SftpIsCancelled())
            return false;
        return conn.Upload(local, remote);
    }
    if (SftpIsCancelled())
        return false;
    // never follow junctions/symlinks (data outside the selection, possible loops)
    {
        if (IsLinkReparsePoint(local))
        {
            g_SkippedCount++; // not transferred - source must not be deleted by Move
            return true;
        }
    }
    conn.MakeDir(remote); // ignore error (may already exist)
    char mask[2 * MAX_PATH];
    lstrcpyn(mask, local, 2 * MAX_PATH);
    SalamanderGeneral->SalPathAppend(mask, "*", 2 * MAX_PATH);
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(mask, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return true;
    bool ok = true;
    do
    {
        if (SftpIsCancelled())
        {
            ok = false;
            break;
        }
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        char l[2 * MAX_PATH], r[MAX_PATH];
        lstrcpyn(l, local, 2 * MAX_PATH);
        SalamanderGeneral->SalPathAppend(l, fd.cFileName, 2 * MAX_PATH);
        SftpJoin(remote, fd.cFileName, r, MAX_PATH);
        bool childDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!SftpUploadRecursive(conn, l, r, childDir))
        {
            ok = false;
            break;
        }
    } while (FindNextFile(h, &fd));
    FindClose(h);
    return ok;
}

// recursive deletion of local file/directory (for Move operation from disk)
static void LocalDeleteRecursive(const char* path, bool isDir)
{
    if (!isDir)
    {
        DeleteFile(path);
        return;
    }
    // junction/symlink to a directory: remove only the link, never its target content
    DWORD pa = GetFileAttributes(path);
    if (pa != INVALID_FILE_ATTRIBUTES && (pa & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        RemoveDirectory(path);
        return;
    }
    char mask[2 * MAX_PATH];
    lstrcpyn(mask, path, 2 * MAX_PATH);
    SalamanderGeneral->SalPathAppend(mask, "*", 2 * MAX_PATH);
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(mask, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                continue;
            char c[2 * MAX_PATH];
            lstrcpyn(c, path, 2 * MAX_PATH);
            SalamanderGeneral->SalPathAppend(c, fd.cFileName, 2 * MAX_PATH);
            LocalDeleteRecursive(c, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        } while (FindNextFile(h, &fd));
        FindClose(h);
    }
    RemoveDirectory(path);
}

// recursive sum of remote directory size with cancellation check and symlink cycle protection
static unsigned __int64 SftpDirSize(CSftpConnection& conn, const char* remote, int& files, int& dirs, bool& cancelled, CCalcSizeProgressDlg* progDlg, int depth = 0)
{
    if (cancelled || depth > 50)
        return 0;

    if (progDlg != NULL && progDlg->GetWantCancel())
    {
        cancelled = true;
        return 0;
    }

    if (progDlg != NULL)
    {
        progDlg->Set(remote, 0, TRUE);
    }

    unsigned __int64 total = 0;
    std::vector<CSftpEntry> entries;
    if (!conn.ListDir(remote, entries))
        return 0;

    for (size_t i = 0; i < entries.size(); i++)
    {
        if (cancelled || (progDlg != NULL && progDlg->GetWantCancel()))
        {
            cancelled = true;
            return total;
        }

        char child[MAX_PATH];
        SftpJoin(remote, entries[i].Name.c_str(), child, MAX_PATH);
        if (entries[i].IsDir && !entries[i].IsLink) // do NOT recurse into symlinked directories to prevent cycles
        {
            dirs++;
            total += SftpDirSize(conn, child, files, dirs, cancelled, progDlg, depth + 1);
        }
        else
        {
            files++;
            total += entries[i].Size;
        }
    }
    return total;
}

// Edit file: download to temp, open in editor, after confirmation upload back.
void SftpEditFile(HWND parent, CPluginFSInterface* fs, const char* remoteDir, const char* fileName)
{
    if (fs == NULL || !fs->EnsureConnected(parent))
        return;
    char remote[MAX_PATH];
    SftpJoin(remoteDir, fileName, remote, MAX_PATH);
    char tmpDir[MAX_PATH], tmpFile[2 * MAX_PATH];
    GetTempPath(MAX_PATH, tmpDir);
    SalamanderGeneral->SalPathAppend(tmpDir, "SFTP_edit", MAX_PATH);
    CreateDirectory(tmpDir, NULL);
    lstrcpyn(tmpFile, tmpDir, 2 * MAX_PATH);
    SalamanderGeneral->SalPathAppend(tmpFile, fileName, 2 * MAX_PATH);

    if (!fs->Conn.Download(remote, tmpFile))
    {
        char eb[600];
        _snprintf_s(eb, _TRUNCATE, "Cannot download file:\n%s", fs->Conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    ShellExecute(parent, "open", tmpFile, NULL, tmpDir, SW_SHOWNORMAL);
    if (SalamanderGeneral->SalMessageBox(parent,
                                         "File has been opened in editor.\n\nWhen you save your changes, click Yes to upload back to server.\n(No = discard changes)",
                                         "Edit file", MB_YESNO | MB_ICONQUESTION) == IDYES)
    {
        if (fs->Conn.Upload(tmpFile, remote))
        {
            char fsfull[MAX_PATH + 32];
            _snprintf_s(fsfull, _TRUNCATE, "%s:%s", AssignedFSName, remoteDir);
            SalamanderGeneral->PostChangeOnPathNotification(fsfull, FALSE);
        }
        else
        {
            char eb[600];
            _snprintf_s(eb, _TRUNCATE, "Upload failed:\n%s", fs->Conn.LastError());
            SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        }
    }
    DeleteFile(tmpFile);
}

// Calculate size of selected items on server.
void SftpCalcSize(HWND parent, CPluginFSInterface* fs, const char* remoteDir, int panel)
{
    if (fs == NULL && panel >= 0)
        fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(panel);
    if (fs == NULL || !fs->EnsureConnected(parent))
        return;
    int index = 0;
    BOOL isDir = FALSE;
    const CFileData* f;
    BOOL focused = (SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir) == NULL); // nothing selected -> focused item
    index = 0;
    unsigned __int64 total = 0;
    int files = 0, dirs = 0;
    bool cancelled = false;

    HWND mainWnd = parent;
    HWND pw;
    while ((pw = GetParent(mainWnd)) != NULL && IsWindowEnabled(pw))
        mainWnd = pw;
    EnableWindow(mainWnd, FALSE);

    CCalcSizeProgressDlg* progDlg = new CCalcSizeProgressDlg(mainWnd, ooStatic);
    if (progDlg != NULL && progDlg->Create() != NULL)
    {
        SetForegroundWindow(progDlg->HWindow);
        progDlg->Set("...", 0, FALSE);
    }
    else
    {
        if (progDlg != NULL)
            delete progDlg;
        progDlg = NULL;
    }

    while (!cancelled)
    {
        if (progDlg != NULL && progDlg->GetWantCancel())
        {
            cancelled = true;
            break;
        }

        f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                    : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;
        if (strcmp(f->Name, "..") != 0)
        {
            char remote[MAX_PATH];
            SftpJoin(remoteDir, f->Name, remote, MAX_PATH);
            if (isDir)
            {
                dirs++;
                int subFiles = 0, subDirs = 0;
                unsigned __int64 dirSize = 0;

                if (progDlg != NULL)
                {
                    progDlg->Set(f->Name, 0, FALSE);
                }

                // Try fast server-side calculation (du / find) first with privilege elevation
                if (!fs->Conn.FastDirSize(remote, dirSize, subFiles, subDirs, true))
                {
                    // Fall back to recursive SFTP scan with cancel check
                    dirSize = SftpDirSize(fs->Conn, remote, subFiles, subDirs, cancelled, progDlg);
                }

                files += subFiles;
                dirs += subDirs;
                total += dirSize;

                if (!cancelled)
                {
                    CFileData* nonConstF = const_cast<CFileData*>(f);
                    nonConstF->Size.SetUI64(dirSize);
                    nonConstF->SizeValid = 1;
                    nonConstF->Dirty = 1;
                }
            }
            else
            {
                files++;
                total += f->Size.Value;
            }
        }
        if (focused)
            break;
    }

    if (progDlg != NULL)
    {
        EnableWindow(mainWnd, TRUE);
        DestroyWindow(progDlg->HWindow);
        delete progDlg;
        progDlg = NULL;
    }
    else
    {
        EnableWindow(mainWnd, TRUE);
    }

    SalamanderGeneral->RepaintChangedItems(panel);

    HWND hFocus = GetFocus();
    if (hFocus != NULL)
    {
        InvalidateRect(hFocus, NULL, TRUE);
        UpdateWindow(hFocus);
    }

    if (cancelled)
    {
        SalamanderGeneral->SalMessageBox(parent, "Calculation was cancelled by user.", LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        char info[400];
        _snprintf_s(info, _TRUNCATE,
                    "Size: %I64u bytes (%.2f MB)\nFiles: %d\nDirectories: %d",
                    total, total / 1048576.0, files, dirs);
        SalamanderGeneral->SalMessageBox(parent, info, "Size on server", MB_OK | MB_ICONINFORMATION);
    }
}

void SftpOnSpacePressedOnFolder(int panel, const CFileData* f)
{
    if (!f || !f->Name || strcmp(f->Name, "..") == 0)
        return;

    CPluginFSInterface* fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(panel);
    if (fs == NULL)
        return;

    HWND hMain = SalamanderGeneral->GetMainWindowHWND();
    if (!fs->EnsureConnected(hMain))
        return;

    char folderName[MAX_PATH];
    lstrcpyn(folderName, f->Name, MAX_PATH);

    // Locate the focused item and the subsequent item in the panel
    int itIdx = 0;
    BOOL itemIsDir = FALSE;
    const CFileData* item = NULL;
    const CFileData* targetF = NULL;
    const CFileData* nextF = NULL;
    bool foundCurrent = false;

    while ((item = SalamanderGeneral->GetPanelItem(panel, &itIdx, &itemIsDir)) != NULL)
    {
        if (!foundCurrent)
        {
            if (item == f || (item->Name && strcmp(item->Name, folderName) == 0))
            {
                targetF = item;
                foundCurrent = true;
            }
        }
        else
        {
            nextF = item;
            break;
        }
    }

    if (!targetF)
        targetF = f;

    // 1. Toggle selection on the folder item (standard Salamander spacebar behavior)
    BOOL newSelected = !targetF->Selected;
    SalamanderGeneral->SelectPanelItem(panel, targetF, newSelected);

    // 2. Calculate folder size on the server
    char remote[MAX_PATH];
    SftpJoin(fs->Path, folderName, remote, MAX_PATH);

    unsigned __int64 dirSize = 0;
    int subFiles = 0, subDirs = 0;

    // Try fast server-side calculation (du only, no find) first with privilege elevation
    if (!fs->Conn.FastDirSize(remote, dirSize, subFiles, subDirs, false))
    {
        bool cancelled = false;
        dirSize = SftpDirSize(fs->Conn, remote, subFiles, subDirs, cancelled, NULL);
    }

    CFileData* nonConstF = const_cast<CFileData*>(targetF);
    nonConstF->Size.SetUI64(dirSize);
    nonConstF->SizeValid = 1;
    nonConstF->Dirty = 1;

    // 3. Move focus / caret to the next item
    if (nextF != NULL)
    {
        SalamanderGeneral->SetPanelFocusedItem(panel, nextF, FALSE);
    }

    SalamanderGeneral->RepaintChangedItems(panel);

    HWND hFocus = GetFocus();
    if (hFocus != NULL)
    {
        InvalidateRect(hFocus, NULL, TRUE);
        UpdateWindow(hFocus);
    }
}

// directory synchronization: direction 0 = download (server->PC), 1 = upload (PC->server).
// Only transfers missing and changed files (size + time comparison).
void SftpSyncDir(HWND parent, CPluginFSInterface* fs, const char* remoteDir, const char* localDir, int direction)
{
    if (fs == NULL || !fs->EnsureConnected(parent))
        return;
    const char* connName = fs->Profile.Name[0] != 0 ? fs->Profile.Name : fs->Profile.Host;
    bool isUpload = (direction == 1);
    SftpProgressBegin(parent, isUpload, isUpload ? localDir : remoteDir, isUpload ? remoteDir : localDir, 1, 0, connName, &fs->Conn);
    g_SyncMode = 1;
    bool ok;
    if (direction == 0)
        ok = SftpDownloadRecursive(fs->Conn, remoteDir, localDir, true);
    else
        ok = SftpUploadRecursive(fs->Conn, localDir, remoteDir, true);
    g_SyncMode = 0;
    SftpProgressEnd();
    if (direction == 1)
        SalamanderGeneral->PostChangeOnPathNotification(remoteDir, TRUE); // refresh remote panel
    if (!ok && !SftpIsCancelled())
    {
        char eb[600];
        _snprintf_s(eb, _TRUNCATE, "Synchronization failed:\n%s", fs->Conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
    }
    else if (!SftpIsCancelled())
        SalamanderGeneral->SalMessageBox(parent, "Synchronization completed.", LoadStr(IDS_PLUGINNAME),
                                         MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI
CPluginFSInterface::Delete(const char* fsName, int mode, HWND parent, int panel,
                           int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // request standard delete confirmation

    if (!EnsureConnected(parent))
    {
        cancelOrError = TRUE;
        return FALSE;
    }

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    BOOL success = TRUE;
    const CFileData* f;
    while (1)
    {
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        char remote[MAX_PATH];
        SftpJoin(Path, f->Name, remote, MAX_PATH);
        if (!SftpDeleteRecursive(Conn, remote, isDir != 0))
        {
            char eb[700];
            _snprintf_s(eb, _TRUNCATE, "Cannot delete \"%s\":\n%s\n\nContinue with other items?",
                        f->Name, Conn.LastError());
            if (SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME),
                                                 MB_YESNO | MB_ICONEXCLAMATION) == IDNO)
            {
                success = FALSE;
                break;
            }
        }
        if (focused)
            break;
    }
    SalamanderGeneral->PostChangeOnPathNotification(Path, TRUE);
    cancelOrError = !success;
    return success;

    /*
  // fetch the "Confirm on" configuration values
  BOOL ConfirmOnNotEmptyDirDelete, ConfirmOnSystemHiddenFileDelete, ConfirmOnSystemHiddenDirDelete;
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMNEDIRDEL, &ConfirmOnNotEmptyDirDelete, 4, NULL);
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEDEL, &ConfirmOnSystemHiddenFileDelete, 4, NULL);
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHDIRDEL, &ConfirmOnSystemHiddenDirDelete, 4, NULL);

  char buf[2 * MAX_PATH];  // buffer for error texts

  char fileName[MAX_PATH];   // buffer for the full name
  strcpy(fileName, Path);
  char *end = fileName + strlen(fileName);  // space reserved for names from the panel
  if (end > fileName && *(end - 1) != '\\')
  {
    *end++ = '\\';
    *end = 0;
  }
  int endSize = MAX_PATH - (end - fileName);  // maximum number of characters available for a panel name

  char dfsFileName[2 * MAX_PATH];   // buffer for the full DFS name
  sprintf(dfsFileName, "%s:%s", fsName, fileName);
  char *endDFSName = dfsFileName + strlen(dfsFileName);  // space reserved for names from the panel
  int endDFSNameSize = 2 * MAX_PATH - (endDFSName - dfsFileName); // maximum number of characters available for a panel name

  const CFileData *f = NULL;  // pointer to the file/directory in the panel to process
  BOOL isDir = FALSE;         // TRUE if 'f' is a directory
  BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
  int index = 0;
  BOOL success = TRUE;        // FALSE if an error occurs or the user cancels
  BOOL skipAllSHFD = FALSE;   // skip all deletes of system or hidden files
  BOOL yesAllSHFD = FALSE;    // delete all system or hidden files
  BOOL skipAllSHDD = FALSE;   // skip all deletes of system or hidden dirs
  BOOL yesAllSHDD = FALSE;    // delete all system or hidden dirs
  BOOL skipAllErrors = FALSE; // skip all errors
  BOOL changeInSubdirs = FALSE;
  while (1)
  {
    // fetch data for the file being processed
    if (focused) f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    else f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

    // delete the file/directory
    if (f != NULL)
    {
      // assemble the full names; trimming to MAX_PATH (2 * MAX_PATH) is theoretically unnecessary
      // but unfortunately required in practice
      lstrcpyn(end, f->Name, endSize);
      lstrcpyn(endDFSName, f->Name, endDFSNameSize);

      if (isDir)
      {
        BOOL skip = FALSE;
        if (ConfirmOnSystemHiddenDirDelete &&
            (f->Attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
        {
          if (!skipAllSHDD && !yesAllSHDD)
          {
            int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, dfsFileName,
                                                        "Do you want to delete the directory with "
                                                        "SYSTEM or HIDDEN attribute?",
                                                        "Confirm Directory Delete");
            switch (res)
            {
              case DIALOG_ALL: yesAllSHDD = TRUE;
              case DIALOG_YES: break;

              case DIALOG_SKIPALL: skipAllSHDD = TRUE;
              case DIALOG_SKIP: skip = TRUE; break;

              default: success = FALSE; break; // DIALOG_CANCEL
            }
          }
          else  // skip all or delete all
          {
            if (skipAllSHDD) skip = TRUE;
          }
        }

        if (success && !skip)   // not canceled and not skipped
        {

          // handle ConfirmOnNotEmptyDirDelete plus recursive delete here,
          // also update the progress (after deleting/skipping files/directories)
          // deleted files should call SalamanderGeneral->RemoveOneFileFromCache();

          changeInSubdirs = TRUE;   // changes may also occur in subdirectories
        }
      }
      else
      {
        BOOL skip = FALSE;
        if (ConfirmOnSystemHiddenFileDelete &&
            (f->Attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
        {
          if (!skipAllSHFD && !yesAllSHFD)
          {
            int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, dfsFileName,
                                                        "Do you want to delete the file with "
                                                        "SYSTEM or HIDDEN attribute?",
                                                        "Confirm File Delete");
            switch (res)
            {
              case DIALOG_ALL: yesAllSHFD = TRUE;
              case DIALOG_YES: break;

              case DIALOG_SKIPALL: skipAllSHFD = TRUE;
              case DIALOG_SKIP: skip = TRUE; break;

              default: success = FALSE; break; // DIALOG_CANCEL
            }
          }
          else  // skip all or delete all
          {
            if (skipAllSHFD) skip = TRUE;
          }
        }

        if (success && !skip)   // not canceled and not skipped
        {
          BOOL skip = FALSE;
          while (1)
          {
            SalamanderGeneral->ClearReadOnlyAttr(fileName, f->Attr);  // allow deletion of read-only items
            if (!DeleteFile(fileName))
            {
              if (!skipAllErrors)
              {
                SalamanderGeneral->GetErrorText(GetLastError(), buf, 2 * MAX_PATH);
                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, dfsFileName, buf, "DFS Delete Error");
                switch (res)
                {
                  case DIALOG_RETRY: break;

                  case DIALOG_SKIPALL: skipAllErrors = TRUE;
                  case DIALOG_SKIP: skip = TRUE; break;

                  default: success = FALSE; break; // DIALOG_CANCEL
                }
              }
              else skip = TRUE;
            }
            else
            {
              // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
              // to lowercase makes the disk cache behave case-insensitively as well
              SalamanderGeneral->ToLowerCase(dfsFileName);
              // remove the deleted file's copy from the disk cache (if it is cached)
              SalamanderGeneral->RemoveOneFileFromCache(dfsFileName);
              break;   // delete succeeded
            }
            if (!success || skip) break;
          }

          if (success)
          {

            // update the progress here (after deleting/skipping a single file)

          }
        }
      }
    }

    // check whether it makes sense to continue (if there is no error and another selected item exists)
    if (!success || focused || f == NULL) break;
  }

  // change on the Path path (without subdirectories if only files were deleted)
  // NOTE: a typical plugin should send the full FS path here
  SalamanderGeneral->PostChangeOnPathNotification(Path, changeInSubdirs);
  return success;
*/
}

BOOL WINAPI DFS_IsTheSamePath(const char* path1, const char* path2)
{
    while (*path1 != 0 && LowerCase[*path1] == LowerCase[*path2])
    {
        path1++;
        path2++;
    }
    if (*path1 == '\\')
        path1++;
    if (*path2 == '\\')
        path2++;
    return *path1 == 0 && *path2 == 0;
}

enum CDFSPathError
{
    dfspeNone,
    dfspeServerNameMissing,
    dfspeShareNameMissing,
    dfspeRelativePath, // relative paths are not supported ("PATH", "\PATH", or "C:PATH")
};

BOOL DFS_IsValidPath(const char* path, CDFSPathError* err)
{
    const char* s = path;
    if (err != NULL)
        *err = dfspeNone;
    if (*s == '\\' && *(s + 1) == '\\') // UNC (\\server\share\...)
    {
        s += 2;
        if (*s == 0 || *s == '\\')
        {
            if (err != NULL)
                *err = dfspeServerNameMissing;
        }
        else
        {
            while (*s != 0 && *s != '\\')
                s++; // skip the server name
            if (*s == '\\')
                s++;
            if (*s == 0 || *s == '\\')
            {
                if (err != NULL)
                    *err = dfspeShareNameMissing;
            }
            else
                return TRUE; // path OK
        }
    }
    else // path specified via a drive (c:\...)
    {
        if (LowerCase[*s] >= 'a' && LowerCase[*s] <= 'z' && *(s + 1) == ':' && *(s + 2) == '\\') // "c:\..."
        {
            return TRUE; // path OK
        }
        else
        {
            if (err != NULL)
                *err = dfspeRelativePath;
        }
    }
    return FALSE;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                     int panel, int selectedFiles, int selectedDirs,
                                     char* targetPath, BOOL& operationMask,
                                     BOOL& cancelOrHandlePath, HWND dropTarget)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;

    if (mode == 1) // first call: let Salamander offer target and show standard dialog
        return FALSE;
    if (mode == 4) // path processing error -> let user fix it
        return FALSE;

    // trim mask (*.* etc.) from target path, keep directory
    char target[2 * MAX_PATH];
    lstrcpyn(target, targetPath, 2 * MAX_PATH);
    {
        char* lastBs = strrchr(target, '\\');
        char* comp = (lastBs != NULL) ? lastBs + 1 : target;
        if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
        {
            if (lastBs != NULL)
                *lastBs = 0;
            else
                target[0] = 0;
        }
    }

    // target must be a disk path (X:\... or \\server\...)
    BOOL diskPath = (target[0] != 0 && target[1] == ':') ||
                    (target[0] == '\\' && target[1] == '\\');

    if (!EnsureConnected(parent))
    {
        cancelOrHandlePath = TRUE;
        return TRUE;
    }

    if (!diskPath)
    {
        // target is on SFTP (sftp://host/... or //host/... or /path)
        const char* p = target;
        while (_strnicmp(p, "sftp:", 5) == 0)
            p += 5;

        char targetHost[256] = "";
        char targetUser[128] = "";
        int targetPort = 0;
        char remoteTargetDir[MAX_PATH] = "/";

        if ((p[0] == '/' || p[0] == '\\') && (p[1] == '/' || p[1] == '\\'))
        {
            // Host specified: //user@host:port/path
            const char* hostStart = p + 2;
            const char* slash = hostStart;
            while (*slash != 0 && *slash != '/' && *slash != '\\')
                slash++;
            int hLen = (int)(slash - hostStart);
            if (hLen > 0 && hLen < (int)sizeof(targetHost))
            {
                char hostBuf[320];
                memcpy(hostBuf, hostStart, hLen);
                hostBuf[hLen] = 0;
                char* at = strchr(hostBuf, '@');
                char* hpart = hostBuf;
                if (at != NULL)
                {
                    *at = 0;
                    lstrcpynA(targetUser, hostBuf, sizeof(targetUser));
                    hpart = at + 1;
                }
                char* colon = strchr(hpart, ':');
                if (colon != NULL)
                {
                    *colon = 0;
                    targetPort = atoi(colon + 1);
                }
                lstrcpynA(targetHost, hpart, sizeof(targetHost));
            }
            if (*slash != 0)
                lstrcpynA(remoteTargetDir, slash, sizeof(remoteTargetDir));
        }
        else
        {
            // Just a path (/path)
            lstrcpynA(remoteTargetDir, p, sizeof(remoteTargetDir));
        }

        for (char* s = remoteTargetDir; *s; s++)
            if (*s == '\\')
                *s = '/';
        SftpNormalize(remoteTargetDir);

        bool isSameServer = true;
        if (targetHost[0] != 0)
        {
            if (_stricmp(targetHost, Profile.Host) != 0)
                isSameServer = false;
            else if (targetPort > 0 && targetPort != Profile.Port)
                isSameServer = false;
            else if (targetUser[0] != 0 && Profile.User[0] != 0 && _stricmp(targetUser, Profile.User) != 0)
                isSameServer = false;
        }

        CSftpProfile targetProfile;
        CSftpConnection* pTargetConn = NULL;
        CSftpConnection localTargetConn;
        char targetConnName[128] = "";
        const char* sourceConnName = Profile.Name[0] != 0 ? Profile.Name : Profile.Host;

        if (isSameServer)
        {
            pTargetConn = &Conn;
            lstrcpynA(targetConnName, sourceConnName, sizeof(targetConnName));
        }
        else
        {
            // Find active FS instance matching targetHost
            const std::vector<CPluginFSInterfaceAbstract*>& fsList = InterfaceForFS.GetActiveFSList();
            for (size_t i = 0; i < fsList.size(); i++)
            {
                CPluginFSInterface* pFS = static_cast<CPluginFSInterface*>(fsList[i]);
                if (_stricmp(pFS->Profile.Host, targetHost) == 0 &&
                    (targetUser[0] == 0 || _stricmp(pFS->Profile.User, targetUser) == 0) &&
                    (targetPort == 0 || pFS->Profile.Port == targetPort))
                {
                    targetProfile = pFS->Profile;
                    if (pFS->Conn.IsConnected())
                        pTargetConn = &pFS->Conn;
                    break;
                }
            }

            // If profile not resolved from active FS, look in saved profiles
            if (targetProfile.Host[0] == 0)
            {
                for (int i = 0; i < SftpProfileCount; i++)
                {
                    if (_stricmp(SftpProfiles[i].Host, targetHost) == 0 &&
                        (targetUser[0] == 0 || _stricmp(SftpProfiles[i].User, targetUser) == 0) &&
                        (targetPort == 0 || SftpProfiles[i].Port == targetPort))
                    {
                        SftpProfileFromSaved(targetProfile, SftpProfiles[i]);
                        break;
                    }
                }
            }

            if (targetProfile.Host[0] == 0)
            {
                lstrcpynA(targetProfile.Host, targetHost, sizeof(targetProfile.Host));
                if (targetUser[0] != 0)
                    lstrcpynA(targetProfile.User, targetUser, sizeof(targetProfile.User));
                targetProfile.Port = targetPort > 0 ? targetPort : 22;
                targetProfile.Valid = true;
            }

            const char* tcn = targetProfile.Name[0] != 0 ? targetProfile.Name : targetProfile.Host;
            lstrcpynA(targetConnName, tcn, sizeof(targetConnName));

            if (pTargetConn == NULL || !pTargetConn->IsConnected())
            {
                if (!SftpEnsureConnected(parent, localTargetConn, targetProfile))
                {
                    cancelOrHandlePath = TRUE;
                    return TRUE;
                }
                pTargetConn = &localTargetConn;
            }
        }

        char tmpDir[MAX_PATH];
        GetTempPath(MAX_PATH, tmpDir);

        BOOL focusedF = (selectedFiles == 0 && selectedDirs == 0);
        int totalFilesF = focusedF ? 1 : (selectedFiles + selectedDirs);
        int indexF = 0;
        BOOL isDirF = FALSE;
        BOOL okF = TRUE;
        const CFileData* ff;

        SftpProgressBegin(parent, false, Path, remoteTargetDir, totalFilesF, 0, sourceConnName, pTargetConn, targetConnName);
        while (1)
        {
            if (SftpIsCancelled())
            {
                okF = FALSE;
                break;
            }
            ff = focusedF ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDirF)
                          : SalamanderGeneral->GetPanelSelectedItem(panel, &indexF, &isDirF);
            if (ff == NULL)
                break;
            char src[MAX_PATH], dst[MAX_PATH], tmpItem[2 * MAX_PATH];
            SftpJoin(Path, ff->Name, src, MAX_PATH);
            SftpJoin(remoteTargetDir, ff->Name, dst, MAX_PATH);
            lstrcpyn(tmpItem, tmpDir, 2 * MAX_PATH);
            SalamanderGeneral->SalPathAppend(tmpItem, ff->Name, 2 * MAX_PATH);
            g_SkippedCount = 0;
            BOOL step = SftpDownloadRecursive(Conn, src, tmpItem, isDirF != 0) &&
                        SftpUploadRecursive(*pTargetConn, tmpItem, dst, isDirF != 0);
            LocalDeleteRecursive(tmpItem, isDirF != 0); // cleanup temp
            if (!step)
            {
                if (SftpIsCancelled())
                {
                    okF = FALSE;
                    break;
                }
                char eb[700];
                _snprintf_s(eb, _TRUNCATE, "Error copying \"%s\":\n%s\n\nContinue?", ff->Name, pTargetConn->LastError());
                if (SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONEXCLAMATION) == IDNO)
                {
                    okF = FALSE;
                    break;
                }
            }
            else if (!copy && g_SkippedCount == 0)
                SftpDeleteRecursive(Conn, src, isDirF != 0); // Move -> delete source (only if nothing was skipped)
            if (focusedF)
                break;
        }
        SftpProgressEnd();
        if (pTargetConn == &localTargetConn)
        {
            localTargetConn.Disconnect();
        }

        char fsfull2[MAX_PATH + 64];
        if (!isSameServer)
        {
            char prefix[320];
            SftpHostPrefix(targetProfile, prefix, sizeof(prefix));
            _snprintf_s(fsfull2, sizeof(fsfull2), _TRUNCATE, "%s:%s%s", fsName, prefix, remoteTargetDir);
        }
        else
        {
            _snprintf_s(fsfull2, sizeof(fsfull2), _TRUNCATE, "%s:%s", fsName, remoteTargetDir);
        }
        SalamanderGeneral->PostChangeOnPathNotification(fsfull2, FALSE);
        if (!copy)
            SalamanderGeneral->PostChangeOnPathNotification(Path, TRUE);
        if (okF)
            targetPath[0] = 0;
        else
            cancelOrHandlePath = TRUE;
        return TRUE;
    }

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int totalFiles = focused ? 1 : (selectedFiles + selectedDirs);
    int index = 0;
    BOOL isDir = FALSE;
    const CFileData* f;
    const char* connName = Profile.Name[0] != 0 ? Profile.Name : Profile.Host;

    CSftpTransferWorker* worker = new CSftpTransferWorker();
    if (worker == NULL)
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }

    CSftpTransferProgressDlg* dlg = new CSftpTransferProgressDlg(parent, ooStandard);
    if (dlg == NULL)
    {
        delete worker;
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
    dlg->SetConnName(connName);
    CSftpProgressDlgThread* dlgThread = new CSftpProgressDlgThread(dlg, parent);
    if (dlgThread == NULL || !dlgThread->Start())
    {
        if (dlgThread != NULL)
            delete dlgThread;
        delete dlg;
        delete worker;
        cancelOrHandlePath = TRUE;
        return FALSE;
    }

    SetForegroundWindow(dlg->HWindow);
    dlg->SetOperationInfo(false, Path, target, totalFiles, 0, connName);
    dlg->SetNotifyPaths(target, Path, !copy);

    while (1)
    {
        f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                    : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;

        char remote[MAX_PATH], local[2 * MAX_PATH];
        SftpJoin(Path, f->Name, remote, MAX_PATH);
        lstrcpyn(local, target, 2 * MAX_PATH);
        SalamanderGeneral->SalPathAppend(local, f->Name, 2 * MAX_PATH);

        CSftpTransferTask task;
        task.TaskType = CSftpTransferTask::TaskDownload;
        task.RemotePath = remote;
        task.LocalPath = local;
        task.FileSize = f->Size.Value;
        task.ResumeOffset = 0;
        task.IsDirectory = (isDir != 0);
        task.DeleteSourceOnSuccess = (!copy);

        worker->EnqueueTask(task);

        if (focused)
            break;
    }

    dlg->SetFS(this);
    dlg->AttachWorker(worker, true);
    RegisterTransferDlg(dlg);
    worker->Start(Profile, dlg->HWindow);

    targetPath[0] = 0;
    cancelOrHandlePath = FALSE;
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                           const char* sourcePath, SalEnumSelection2 next,
                                           void* nextParam, int sourceFiles, int sourceDirs,
                                           char* targetPath, BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = FALSE;

    if (mode == 1)
    {
        // add mask *.* to target path (Salamander will show standard dialog)
        SalamanderGeneral->SalPathAppend(targetPath, "*.*", 2 * MAX_PATH);
        return TRUE;
    }

    // get user-part of target path (after "fsName://host") and trim mask -> remote directory
    char remoteDir[MAX_PATH];
    char* up = strchr(targetPath, ':');
    lstrcpyn(remoteDir, SftpStripHost((up != NULL) ? up + 1 : targetPath), MAX_PATH);
    for (char* p = remoteDir; *p; p++)
        if (*p == '\\')
            *p = '/';
    {
        char* lastSlash = strrchr(remoteDir, '/');
        char* comp = (lastSlash != NULL) ? lastSlash + 1 : remoteDir;
        if (strchr(comp, '*') != NULL || strchr(comp, '?') != NULL)
        {
            if (lastSlash != NULL)
                *lastSlash = 0;
            else
                remoteDir[0] = 0;
        }
    }
    SftpNormalize(remoteDir);

    if (!EnsureConnected(parent))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    const char* name;
    const char* dosName;
    BOOL isDir;
    CQuadWord size;
    DWORD attr;
    FILETIME lastWrite;
    int totalFiles = (sourceFiles == 0 && sourceDirs == 0) ? 1 : (sourceFiles + sourceDirs);
    const char* connName = Profile.Name[0] != 0 ? Profile.Name : Profile.Host;

    CSftpTransferWorker* worker = new CSftpTransferWorker();
    if (worker == NULL)
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    CSftpTransferProgressDlg* dlg = new CSftpTransferProgressDlg(parent, ooStandard);
    if (dlg == NULL)
    {
        delete worker;
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    dlg->SetConnName(connName);
    CSftpProgressDlgThread* dlgThread = new CSftpProgressDlgThread(dlg, parent);
    if (dlgThread == NULL || !dlgThread->Start())
    {
        if (dlgThread != NULL)
            delete dlgThread;
        delete dlg;
        delete worker;
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    SetForegroundWindow(dlg->HWindow);
    dlg->SetOperationInfo(true, sourcePath, remoteDir, totalFiles, 0, connName);

    char fsfull[MAX_PATH + 32];
    _snprintf_s(fsfull, _TRUNCATE, "%s:%s", fsName, remoteDir);
    dlg->SetNotifyPaths(fsfull, sourcePath, !copy);

    while ((name = next(NULL, 0, &dosName, &isDir, &size, &attr, &lastWrite, nextParam, NULL)) != NULL)
    {
        char local[2 * MAX_PATH];
        lstrcpyn(local, sourcePath, 2 * MAX_PATH);
        SalamanderGeneral->SalPathAppend(local, name, 2 * MAX_PATH);

        const char* base = strrchr(name, '\\');
        base = (base != NULL) ? base + 1 : name;
        char remote[MAX_PATH];
        SftpJoin(remoteDir, base, remote, MAX_PATH);

        CSftpTransferTask task;
        task.TaskType = CSftpTransferTask::TaskUpload;
        task.LocalPath = local;
        task.RemotePath = remote;
        task.FileSize = size.Value;
        task.ResumeOffset = 0;
        task.IsDirectory = (isDir != 0);
        task.DeleteSourceOnSuccess = (!copy);

        worker->EnqueueTask(task);
    }

    dlg->SetFS(this);
    dlg->AttachWorker(worker, true);
    RegisterTransferDlg(dlg);
    worker->Start(Profile, dlg->HWindow);

    return TRUE;
}

// dialog for entering octal permissions (chmod)
static int g_ChmodOctal = 0644;
static INT_PTR CALLBACK ChmodDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND par = GetParent(hwnd);
        if (par != NULL)
            SalamanderGeneral->MultiMonCenterWindow(hwnd, par, TRUE);
        char b[16];
        _snprintf_s(b, _TRUNCATE, "%o", g_ChmodOctal & 0777);
        SetDlgItemText(hwnd, IDC_CHMODVAL, b);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK)
        {
            char b[16];
            GetDlgItemText(hwnd, IDC_CHMODVAL, b, sizeof(b));
            g_ChmodOctal = (int)strtol(b, NULL, 8);
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL)
        {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_WINDOWPOSCHANGED:
        SftpFlushDWMForInteractiveMove(reinterpret_cast<const WINDOWPOS*>(lp));
        break;
    }
    return FALSE;
}

BOOL WINAPI
CPluginFSInterface::ChangeAttributes(const char* fsName, HWND parent, int panel,
                                     int selectedFiles, int selectedDirs)
{
    if (!EnsureConnected(parent))
        return FALSE;

    // pre-fill permissions of first selected/focused item
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
    if (f != NULL)
    {
        char remote[MAX_PATH];
        SftpJoin(Path, f->Name, remote, MAX_PATH);
        unsigned long m;
        if (Conn.GetPermissions(remote, m))
            g_ChmodOctal = (int)m;
    }

    if (SftpDialogBox(HLanguage, IDD_CHMOD, parent, ChmodDlgProc, 0) != IDOK)
        return FALSE;
    unsigned long mode = (unsigned long)(g_ChmodOctal & 0777);

    // apply to all selected (or focused)
    focused = (selectedFiles == 0 && selectedDirs == 0);
    index = 0;
    BOOL success = TRUE;
    while (1)
    {
        f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                    : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (f == NULL)
            break;
        char remote[MAX_PATH];
        SftpJoin(Path, f->Name, remote, MAX_PATH);
        if (!Conn.Chmod(remote, mode))
        {
            char eb[700];
            _snprintf_s(eb, _TRUNCATE, "Cannot change permissions \"%s\":\n%s\n\nContinue?",
                        f->Name, Conn.LastError());
            if (SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME),
                                                 MB_YESNO | MB_ICONEXCLAMATION) == IDNO)
            {
                success = FALSE;
                break;
            }
        }
        if (focused)
            break;
    }
    SalamanderGeneral->PostChangeOnPathNotification(Path, FALSE);
    return success;
}

void WINAPI
CPluginFSInterface::ShowProperties(const char* fsName, HWND parent, int panel,
                                   int selectedFiles, int selectedDirs)
{
    if (!EnsureConnected(parent))
        return;
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL isDir = FALSE;
    const CFileData* f = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                 : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
    if (f == NULL)
        return;
    char remote[MAX_PATH];
    SftpJoin(Path, f->Name, remote, MAX_PATH);
    unsigned __int64 size;
    unsigned long perms, uid, gid, mtime;
    if (!Conn.StatFull(remote, size, perms, uid, gid, mtime))
    {
        char eb[600];
        _snprintf_s(eb, _TRUNCATE, "Cannot read properties:\n%s", Conn.LastError());
        SalamanderGeneral->SalMessageBox(parent, eb, LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    char timeStr[64] = "?";
    if (mtime != 0)
    {
        ULONGLONG ll = (ULONGLONG)mtime * 10000000ULL + 116444736000000000ULL;
        FILETIME ft;
        ft.dwLowDateTime = (DWORD)ll;
        ft.dwHighDateTime = (DWORD)(ll >> 32);
        SYSTEMTIME st, lt;
        FileTimeToSystemTime(&ft, &st);
        SystemTimeToTzSpecificLocalTime(NULL, &st, &lt);
        _snprintf_s(timeStr, _TRUNCATE, "%02d.%02d.%04d %02d:%02d:%02d",
                    lt.wDay, lt.wMonth, lt.wYear, lt.wHour, lt.wMinute, lt.wSecond);
    }
    char rwx[12];
    const char* perm = "rwxrwxrwx";
    rwx[0] = isDir ? 'd' : '-';
    for (int i = 0; i < 9; i++)
        rwx[i + 1] = (perms & (1 << (8 - i))) ? perm[i] : '-';
    rwx[10] = 0;

    char info[1100];
    if (focused || (selectedFiles + selectedDirs) <= 1)
    {
        _snprintf_s(info, _TRUNCATE,
                    "Name:\t%s\nType:\t%s\nSize:\t%I64u B\nModified:\t%s\nPermissions:\t%s  (%03o)\nOwner (UID):\t%lu\nGroup (GID):\t%lu\nPath:\t%s",
                    f->Name, isDir ? "directory" : "file", size, timeStr, rwx,
                    (unsigned)(perms & 0777), uid, gid, remote);
    }
    else
    {
        _snprintf_s(info, _TRUNCATE, "Selected: %d files, %d directories", selectedFiles, selectedDirs);
    }
    SalamanderGeneral->SalMessageBox(parent, info, "Properties", MB_OK | MB_ICONINFORMATION);
}

void WINAPI
CPluginFSInterface::ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                int panel, int selectedFiles, int selectedDirs)
{
#ifndef SFTP_QUIET
    char bufText[100];
    sprintf(bufText, "Show context menu (type %d).", (int)type);
    SalamanderGeneral->SalMessageBox(parent, bufText, "DFS Context Menu", MB_OK | MB_ICONINFORMATION);
#endif // SFTP_QUIET

    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        TRACE_E("CPluginFSInterface::ContextMenu: Unable to create menu.");
        return;
    }
    MENUITEMINFO mi;
    char nameBuf[200];

    switch (type)
    {
    case fscmItemsInPanel: // context menu for panel items (selected/focused files and directories)
    {
        int i = 0;

        BOOL isDir = FALSE;
        const CFileData* focusedFile = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        if (focusedFile == NULL || isDir)
        {
            int idx = 0;
            focusedFile = SalamanderGeneral->GetPanelSelectedItem(panel, &idx, &isDir);
        }

        bool insertedExecute = false;
        int index = 0;
        int salCmd;
        BOOL enabled;
        int type2, lastType = sctyUnknown;
        while (SalamanderGeneral->EnumSalamanderCommands(&index, &salCmd, nameBuf, 200, &enabled, &type2))
        {
            if (type2 != lastType && lastType != sctyUnknown) // insert a separator between command groups
            {
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE;
                mi.fType = MFT_SEPARATOR;
                InsertMenuItem(menu, i++, TRUE, &mi);
            }
            lastType = type2;

            // insert Salamander commands
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
            mi.fType = MFT_STRING;
            if (salCmd == SALCMD_CALCDIRSIZES || salCmd == SALCMD_OCCUPIEDSPACE)
            {
                mi.wID = MENUCMD_CALCSIZE;
                mi.fState = MFS_ENABLED;
            }
            else
            {
                mi.wID = salCmd + 1000; // shift Salamander commands by 1000 so they differ from ours
                mi.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
            }
            mi.dwTypeData = nameBuf;
            mi.cch = (UINT)strlen(nameBuf);
            InsertMenuItem(menu, i++, TRUE, &mi);

            // Insert Execute directly after Open (SALCMD_OPEN) without any separator
            if (!insertedExecute && salCmd == SALCMD_OPEN && focusedFile != NULL && !isDir)
            {
                insertedExecute = true;
                char execBuf[100];
                lstrcpyn(execBuf, LoadStr(IDS_MENU_EXECUTE), sizeof(execBuf));
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
                mi.fType = MFT_STRING;
                mi.wID = MENUCMD_EXECUTEFILE;
                mi.dwTypeData = execBuf;
                mi.cch = (UINT)strlen(execBuf);
                mi.fState = MFS_ENABLED;
                InsertMenuItem(menu, i++, TRUE, &mi);
            }
        }
        if (!insertedExecute && focusedFile != NULL && !isDir)
        {
            insertedExecute = true;
            char execBuf[100];
            lstrcpyn(execBuf, LoadStr(IDS_MENU_EXECUTE), sizeof(execBuf));
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
            mi.fType = MFT_STRING;
            mi.wID = MENUCMD_EXECUTEFILE;
            mi.dwTypeData = execBuf;
            mi.cch = (UINT)strlen(execBuf);
            mi.fState = MFS_ENABLED;
            InsertMenuItem(menu, i++, TRUE, &mi);
        }
        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd == MENUCMD_EXECUTEFILE)
        {
            SalamanderGeneral->PostMenuExtCommand(MENUCMD_EXECUTEFILE, TRUE);
        }
        else if (cmd == MENUCMD_CALCSIZE)
        {
            SalamanderGeneral->PostMenuExtCommand(MENUCMD_CALCSIZE, TRUE);
        }
        else if (cmd >= 1000) // the user selected a Salamander command
        {
            if (SalamanderGeneral->GetSalamanderCommand(cmd - 1000, nameBuf, 200, &enabled, &type2))
                TRACE_I("Starting command: " << nameBuf);
            SalamanderGeneral->PostSalamanderCommand(cmd - 1000);
        }
        break;
    }

    case fscmPathInPanel: // context menu for the current path in the panel
    {
        int i = 0;

        strcpy(nameBuf, "&Disconnect");
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = panel == PANEL_LEFT ? MENUCMD_DISCONNECT_LEFT : MENUCMD_DISCONNECT_RIGHT;
        mi.dwTypeData = nameBuf;
        mi.cch = (UINT)strlen(nameBuf);
        mi.fState = MFS_ENABLED;
        InsertMenuItem(menu, i++, TRUE, &mi);

        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd != 0)                                         // the user selected a command from the menu
            SalamanderGeneral->PostMenuExtCommand(cmd, TRUE); // execute later in "sal-idle"
        break;
    }

    case fscmPanel: // context menu for the panel
    {
        int i = 0;

        strcpy(nameBuf, "&Disconnect");
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = panel == PANEL_LEFT ? MENUCMD_DISCONNECT_LEFT : MENUCMD_DISCONNECT_RIGHT;
        mi.dwTypeData = nameBuf;
        mi.cch = (UINT)strlen(nameBuf);
        mi.fState = MFS_ENABLED;
        InsertMenuItem(menu, i++, TRUE, &mi);

        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd != 0)                                         // the user selected a command from the menu
            SalamanderGeneral->PostMenuExtCommand(cmd, TRUE); // execute later in "sal-idle"
        break;
    }
    }
    DestroyMenu(menu);
}
