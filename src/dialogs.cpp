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
#include "sftpconn.h"
#include "sftpglue.h"
#include <string>
#include <vector>

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))


namespace
{
void FlushDWM()
{
    typedef HRESULT(WINAPI * FDwmFlush)();
    static FDwmFlush dwmFlush = NULL;
    static BOOL loaded = FALSE;
    if (!loaded)
    {
        HMODULE dwmApi = GetModuleHandleW(L"dwmapi.dll");
        if (dwmApi != NULL)
            dwmFlush = reinterpret_cast<FDwmFlush>(GetProcAddress(dwmApi, "DwmFlush"));
        loaded = TRUE;
    }
    if (dwmFlush != NULL)
        dwmFlush();
}

}

void SftpFlushDWMForInteractiveMove(const WINDOWPOS* windowPos)
{
    if (windowPos != NULL && (windowPos->flags & SWP_NOSIZE) != 0)
        FlushDWM();
}

BOOL SftpHandleDarkDlgMessage(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam, INT_PTR* result)
{
    switch (msg)
    {
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    {
        RefreshWinLibDarkModeFromHost();
        if (msg == WM_THEMECHANGED || DarkModeHandleSettingChange(msg, lParam))
        {
            WinLibApplyDarkMode(hDlg);
            RedrawWindow(hDlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
            *result = TRUE;
            return TRUE;
        }
        return FALSE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSCROLLBAR:
    {
        LRESULT brush = 0;
        if (DarkModeHandleCtlColor(msg, wParam, lParam, brush))
        {
            *result = (INT_PTR)brush;
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

INT_PTR SftpDialogBox(HINSTANCE module, int resID, HWND parent, DLGPROC proc, LPARAM param)
{
    return DialogBoxParamW(module, MAKEINTRESOURCEW(resID), parent, proc, param);
}

//****************************************************************************
//
// CCommonDialog
//

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, hParent, origin)
{
}

CCommonDialog::CCommonDialog(HINSTANCE hInstance, int resID, int helpID, HWND hParent, CObjectOrigin origin)
    : CDialog(hInstance, resID, helpID, hParent, origin)
{
}

INT_PTR
CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // horizontally and vertically center the dialog relative to the parent
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break; // let DefDlgProc handle focus
    }
    }
    INT_PTR result = CDialog::DialogProc(uMsg, wParam, lParam);
    if (uMsg == WM_WINDOWPOSCHANGED)
    {
        SftpFlushDWMForInteractiveMove(reinterpret_cast<const WINDOWPOS*>(lParam));
    }
    return result;
}

void CCommonDialog::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

//
// ****************************************************************************
// CCommonPropSheetPage
//

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    SalamanderGUI->ArrangeHorizontalLines(HWindow);
}

//
// ****************************************************************************
// CConfigPageFirst
//

CConfigPageFirst::CConfigPageFirst()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGEFIRST, IDD_CFGPAGEFIRST, PSP_HASHELP, NULL)
{
}

void CConfigPageFirst::Validate(CTransferInfo& ti)
{
    int dummy;                          // test value only (Validate is called when leaving the window via OK)
    ti.EditLine(IDC_TESTNUMBER, dummy); // check whether it is a number
    if (ti.IsGood() && dummy >= 10)     // ensure the number is not greater than or equal to 10
    {
        SalamanderGeneral->SalMessageBox(HWindow, "Number must be less then 10.", "Error",
                                         MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDC_TESTNUMBER);
        // PostMessage(GetDlgItem(HWindow, IDC_TESTNUMBER), EM_SETSEL, errorPos1, errorPos2);  // highlight the error position
    }
}

void CConfigPageFirst::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDC_TESTSTRING, Str, MAX_PATH);
    ti.EditLine(IDC_TESTNUMBER, Number);

    HWND hWnd;
    if (ti.GetControl(hWnd, IDC_COMBO))
    {
        if (ti.Type == ttDataToWindow) // Transfer() called when opening the window (data -> window)
        {
            SendMessage(hWnd, CB_RESETCONTENT, 0, 0);
            SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM) "first");
            SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM) "second");
            SendMessage(hWnd, CB_ADDSTRING, 0, (LPARAM) "third");
            SendMessage(hWnd, CB_SETCURSEL, Selection, 0);
        }
        else // ttDataFromWindow; Transfer() called when OK is pressed (window -> data)
        {
            Selection = (int)SendMessage(hWnd, CB_GETCURSEL, 0, 0);
        }
    }
}

//
// ****************************************************************************
// CConfigPageSecond
//

CConfigPageSecond::CConfigPageSecond()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGESECOND, 0, NULL) // second config page has no help
{
}

void CConfigPageSecond::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_CHECK, CheckBox);

    ti.RadioButton(IDC_RADIO1, 10, RadioBox);
    ti.RadioButton(IDC_RADIO2, 13, RadioBox);
    ti.RadioButton(IDC_RADIO3, 20, RadioBox);
}

//
// ****************************************************************************
// CConfigPageViewer
//

CConfigPageViewer::CConfigPageViewer()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGEVIEWER, IDD_CFGPAGEVIEWER, PSP_HASHELP, NULL)
{
}

void CConfigPageViewer::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDC_CFG_SAVEPOSONCLOSE, 1, CfgSavePosition);
    ti.RadioButton(IDC_CFG_SETBYMAINWINDOW, 0, CfgSavePosition);
}

//
// ****************************************************************************
// CConfigDialog
//

// helper object for centering the configuration dialog relative to the parent
class CCenteredPropertyWindow : public CWindow
{
protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pos = (WINDOWPOS*)lParam;
            if (pos->flags & SWP_SHOWWINDOW)
            {
                HWND hParent = GetParent(HWindow);
                if (hParent != NULL)
                    SalamanderGeneral->MultiMonCenterWindow(HWindow, hParent, TRUE);
            }
            break;
        }

        case WM_APP + 1000: // detach from the dialog (it is already centered)
        {
            DetachWindow();
            delete this; // a bit ugly, but nothing will touch 'this' anymore, so it is fine
            return 0;
        }
        }
        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

#ifndef LPDLGTEMPLATEEX
#include <pshpack1.h>
typedef struct DLGTEMPLATEEX
{
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATEEX, *LPDLGTEMPLATEEX;
#include <poppack.h>
#endif // LPDLGTEMPLATEEX

// helper callback for centering the configuration dialog relative to the parent and removing the '?' button from the caption
int CALLBACK CenterCallback(HWND HWindow, UINT uMsg, LPARAM lParam)
{
    if (uMsg == PSCB_INITIALIZED) // attach to the dialog
    {
        CCenteredPropertyWindow* wnd = new CCenteredPropertyWindow;
        if (wnd != NULL)
        {
            wnd->AttachToWindow(HWindow);
            if (wnd->HWindow == NULL)
                delete wnd; // window is not attached, dispose of it right away
            else
            {
                PostMessage(wnd->HWindow, WM_APP + 1000, 0, 0); // to detach CCenteredPropertyWindow from the dialog
            }
        }
    }
    if (uMsg == PSCB_PRECREATE) // remove the '?' button from the property sheet header
    {
        // Remove the DS_CONTEXTHELP style from the dialog box template
        if (((LPDLGTEMPLATEEX)lParam)->signature == 0xFFFF)
            ((LPDLGTEMPLATEEX)lParam)->style &= ~DS_CONTEXTHELP;
        else
            ((LPDLGTEMPLATE)lParam)->style &= ~DS_CONTEXTHELP;
    }
    return 0;
}

CConfigDialog::CConfigDialog(HWND parent)
    : CPropertyDialog(parent, HLanguage, LoadStr(IDS_CFG_TITLE),
                      LastCfgPage, PSH_USECALLBACK | PSH_NOAPPLYNOW | PSH_HASHELP,
                      NULL, &LastCfgPage, CenterCallback)
{
    Add(&PageFirst);
    Add(&PageSecond);
    Add(&PageViewer);
}

//
// ****************************************************************************
// CPathDialog
//

CPathDialog::CPathDialog(HWND parent, char* path, BOOL* filePath)
    : CCommonDialog(HLanguage, IDD_PATHDLG, IDD_PATHDLG, parent)
{
    Path = path;
    FilePath = filePath;
}

void CPathDialog::Transfer(CTransferInfo& ti)
{
    ti.EditLine(IDC_PATHSTRING, Path, MAX_PATH);
    ti.CheckBox(IDC_FILECHECK, *FilePath);
}

INT_PTR
CPathDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CPathDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        /*
      SetFocus(GetDlgItem(HWindow, IDOK));  // we want our own focus handling
      CDialog::DialogProc(uMsg, wParam, lParam);
      return FALSE;
*/
        break; // let DefDlgProc handle focus
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

#define WM_USER_APPEND_TEXT (WM_USER + 410)
#define WM_USER_EXEC_DONE   (WM_USER + 411)

struct CCmdExecContext
{
    HWND hDlg = NULL;
    std::string displayCmd;
    std::string fullCmd;
    CSftpProfile profile;
    CPluginFSInterfaceAbstract* fs = nullptr;
    volatile bool cancelRequested = false;
    volatile bool isRunning = false;
    HANDLE hThread = NULL;
    CRITICAL_SECTION cs;
    std::vector<std::string> pendingChunks;
    HFONT hFont = NULL;
    bool success = false;
    std::string errorMsg;
};

static bool SftpExecStreamCallback(void* ctx, const char* data, size_t size)
{
    CCmdExecContext* c = (CCmdExecContext*)ctx;
    if (!c || size == 0)
        return true;

    // Convert \n to \r\n for Windows EDIT control
    std::string norm;
    norm.reserve(size + size / 4);
    for (size_t i = 0; i < size; i++)
    {
        if (data[i] == '\n' && (i == 0 || data[i - 1] != '\r'))
            norm += "\r\n";
        else
            norm += data[i];
    }

    EnterCriticalSection(&c->cs);
    c->pendingChunks.push_back(norm);
    LeaveCriticalSection(&c->cs);

    PostMessage(c->hDlg, WM_USER_APPEND_TEXT, 0, 0);
    return true;
}

static DWORD WINAPI CmdExecWorkerThread(LPVOID param)
{
    CCmdExecContext* c = (CCmdExecContext*)param;
    CSftpConnection conn;
    // Dedicated SSH connection using profile credentials (protocol 1 = pure SSH/SCP mode without SFTP init)
    if (conn.Connect(c->profile.Host, c->profile.Port, c->profile.User, c->profile.Password, c->profile.KeyFile,
                     c->profile.UseCompression, 1, false, nullptr))
    {
        c->success = conn.ExecCommandStream(c->fullCmd.c_str(), SftpExecStreamCallback, c, &c->cancelRequested);
        if (!c->success)
            c->errorMsg = conn.LastError();
        conn.Disconnect();
    }
    else
    {
        c->success = false;
        c->errorMsg = conn.LastError();
    }
    PostMessage(c->hDlg, WM_USER_EXEC_DONE, 0, 0);
    return 0;
}

static INT_PTR CALLBACK CmdExecDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CCmdExecContext* ctx = (CCmdExecContext*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    INT_PTR darkResult = 0;
    if (SftpHandleDarkDlgMessage(hDlg, uMsg, wParam, lParam, &darkResult))
        return darkResult;

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        WinLibApplyDarkMode(hDlg);
        ctx = (CCmdExecContext*)lParam;
        SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);
        ctx->hDlg = hDlg;

        // Set info text
        char info[512];
        _snprintf_s(info, _TRUNCATE, "Command: %s", ctx->displayCmd.c_str());
        SetDlgItemText(hDlg, IDC_CMD_INFO, info);

        // Limit text capacity to maximum
        SendDlgItemMessage(hDlg, IDC_CMD_OUTPUT, EM_SETLIMITTEXT, 0, 0);

        // Create clean Monospace font
        ctx->hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        if (!ctx->hFont)
            ctx->hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Lucida Console");
        if (!ctx->hFont)
            ctx->hFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);

        SendDlgItemMessage(hDlg, IDC_CMD_OUTPUT, WM_SETFONT, (WPARAM)ctx->hFont, TRUE);

        // Center dialog relative to Salamander main window
        HWND parentWnd = SalamanderGeneral ? SalamanderGeneral->GetMainWindowHWND() : NULL;
        if (parentWnd)
        {
            RECT pr, dr;
            GetWindowRect(parentWnd, &pr);
            GetWindowRect(hDlg, &dr);
            int dw = dr.right - dr.left;
            int dh = dr.bottom - dr.top;
            int x = pr.left + ((pr.right - pr.left) - dw) / 2;
            int y = pr.top + ((pr.bottom - pr.top) - dh) / 2;
            SetWindowPos(hDlg, NULL, x > 0 ? x : 0, y > 0 ? y : 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        ctx->isRunning = true;
        ctx->hThread = CreateThread(NULL, 0, CmdExecWorkerThread, ctx, 0, NULL);
        return TRUE;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        HWND hInfo = GetDlgItem(hDlg, IDC_CMD_INFO);
        HWND hEdit = GetDlgItem(hDlg, IDC_CMD_OUTPUT);
        HWND hBtn = GetDlgItem(hDlg, IDC_CMD_BTN);

        if (hInfo && hEdit && hBtn)
        {
            SetWindowPos(hInfo, NULL, 10, 8, w - 20, 18, SWP_NOZORDER);
            SetWindowPos(hEdit, NULL, 10, 28, w - 20, h - 70, SWP_NOZORDER);
            SetWindowPos(hBtn, NULL, w - 100, h - 34, 90, 26, SWP_NOZORDER);
        }
        return TRUE;
    }

    case WM_USER_APPEND_TEXT:
    {
        if (!ctx) return TRUE;
        std::vector<std::string> chunks;
        EnterCriticalSection(&ctx->cs);
        chunks.swap(ctx->pendingChunks);
        LeaveCriticalSection(&ctx->cs);

        if (!chunks.empty())
        {
            HWND hEdit = GetDlgItem(hDlg, IDC_CMD_OUTPUT);
            for (size_t i = 0; i < chunks.size(); i++)
            {
                int len = GetWindowTextLength(hEdit);
                SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
                SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)chunks[i].c_str());
            }
            SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
        }
        return TRUE;
    }

    case WM_USER_EXEC_DONE:
    {
        if (!ctx) return TRUE;
        // Flush remaining chunks if any
        SendMessage(hDlg, WM_USER_APPEND_TEXT, 0, 0);

        ctx->isRunning = false;
        HWND hEdit = GetDlgItem(hDlg, IDC_CMD_OUTPUT);
        if (GetWindowTextLength(hEdit) == 0)
        {
            if (!ctx->success)
            {
                char eb[512];
                _snprintf_s(eb, _TRUNCATE, "Command failed:\r\n%s\r\n", ctx->errorMsg.c_str());
                SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)eb);
            }
            else
            {
                SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)"(command completed with no output)\r\n");
            }
        }

        HWND hBtn = GetDlgItem(hDlg, IDC_CMD_BTN);
        SetWindowText(hBtn, "Close");
        EnableWindow(hBtn, TRUE);
        SetFocus(hBtn);

        // Notify Salamander panel to refresh
        if (ctx->fs && SalamanderGeneral)
            SalamanderGeneral->PostRefreshPanelFS(ctx->fs);

        return TRUE;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        if (id == IDC_CMD_BTN || id == IDCANCEL || id == IDOK)
        {
            if (ctx && ctx->isRunning)
            {
                ctx->cancelRequested = true;
                HWND hBtn = GetDlgItem(hDlg, IDC_CMD_BTN);
                SetWindowText(hBtn, "Stopping...");
                EnableWindow(hBtn, FALSE);
            }
            else
            {
                DestroyWindow(hDlg);
            }
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
    {
        if (ctx && ctx->isRunning)
        {
            ctx->cancelRequested = true;
            HWND hBtn = GetDlgItem(hDlg, IDC_CMD_BTN);
            SetWindowText(hBtn, "Stopping...");
            EnableWindow(hBtn, FALSE);
        }
        else
        {
            DestroyWindow(hDlg);
        }
        return TRUE;
    }

    case WM_DESTROY:
    {
        if (ctx && ctx->hFont)
        {
            DeleteObject(ctx->hFont);
            ctx->hFont = NULL;
        }
        return 0;
    }

    case WM_NCDESTROY:
    {
        if (ctx)
        {
            if (ctx->hThread != NULL)
            {
                ctx->cancelRequested = true;
                if (WaitForSingleObject(ctx->hThread, 500) == WAIT_TIMEOUT)
                {
                    TerminateThread(ctx->hThread, 0);
                }
                CloseHandle(ctx->hThread);
                ctx->hThread = NULL;
            }
            DeleteCriticalSection(&ctx->cs);
            delete ctx;
        }
        SetWindowLongPtr(hDlg, GWLP_USERDATA, 0);
        return 0;
    }
    }

    return FALSE;
}

void ShowCommandExecDialog(HWND parent, const char* displayCmd, const char* remoteFullCmd, const CSftpProfile& profile, CPluginFSInterfaceAbstract* fs)
{
    CCmdExecContext* ctx = new CCmdExecContext();
    ctx->hDlg = NULL;
    ctx->displayCmd = displayCmd ? displayCmd : "";
    ctx->fullCmd = remoteFullCmd ? remoteFullCmd : "";
    ctx->profile = profile;
    ctx->fs = fs;
    ctx->cancelRequested = false;
    ctx->isRunning = false;
    ctx->hThread = NULL;
    ctx->hFont = NULL;
    ctx->success = false;
    InitializeCriticalSection(&ctx->cs);

    // Ensure Salamander windows are enabled in case host disabled them for operation
    HWND hMain = SalamanderGeneral ? SalamanderGeneral->GetMainWindowHWND() : parent;
    if (hMain != NULL)
        EnableWindow(hMain, TRUE);
    if (parent != NULL && parent != hMain)
        EnableWindow(parent, TRUE);

    // Create as independent top-level unowned window (NULL parent prevents modal-like owner lock)
    HWND hDlg = CreateDialogParamW(HLanguage, MAKEINTRESOURCEW(IDD_CMDEXEC), NULL, CmdExecDlgProc, (LPARAM)ctx);
    if (hDlg != NULL)
    {
        SetWindowLongPtr(hDlg, GWL_EXSTYLE, GetWindowLongPtr(hDlg, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        ShowWindow(hDlg, SW_SHOW);
        SetForegroundWindow(hDlg);
    }
    else
    {
        DeleteCriticalSection(&ctx->cs);
        delete ctx;
    }
}

