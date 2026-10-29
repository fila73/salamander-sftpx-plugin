// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sftpconflictdlg.h"
#include "sftpglue.h"

CSftpConflictDlg::CSftpConflictDlg(HWND parent,
                                   const char* srcPath, const char* srcName,
                                   const char* tgtPath, const char* tgtName,
                                   unsigned __int64 srcSize, unsigned __int64 tgtSize)
    : CCommonDialog(HLanguage, IDD_CONFLICTDLG, parent, ooStatic)
{
    CenterWnd = parent != NULL ? parent : (SalamanderGeneral != NULL ? SalamanderGeneral->GetMainWindowHWND() : NULL);
    lstrcpynA(SrcPath, srcPath != NULL ? srcPath : "", sizeof(SrcPath));
    lstrcpynA(SrcName, srcName != NULL ? srcName : "", sizeof(SrcName));
    lstrcpynA(TgtPath, tgtPath != NULL ? tgtPath : "", sizeof(TgtPath));
    lstrcpynA(TgtName, tgtName != NULL ? tgtName : "", sizeof(TgtName));
    ResultName[0] = 0;
    SrcSize = srcSize;
    TgtSize = tgtSize;
    Action = SFTP_CONFLICT_CANCEL;
    ApplyToAll = FALSE;
}

INT_PTR CSftpConflictDlg::ExecuteDlg(ESftpConflictAction& outAction, BOOL& outApplyToAll, char* outNewName, int maxNewName)
{
    INT_PTR res = CCommonDialog::Execute();
    outAction = Action;
    outApplyToAll = ApplyToAll;
    if (outNewName != NULL && maxNewName > 0)
    {
        if (ResultName[0] != 0 && strcmp(ResultName, TgtName) != 0)
            lstrcpynA(outNewName, ResultName, maxNewName);
        else
            outNewName[0] = 0;
    }
    return res;
}

void CSftpConflictDlg::GenerateAlternateName(char* outBuf, int maxLen)
{
    if (outBuf == NULL || maxLen <= 0)
        return;

    char base[MAX_PATH];
    char ext[MAX_PATH];
    base[0] = 0;
    ext[0] = 0;

    const char* dot = strrchr(TgtName, '.');
    if (dot != NULL && dot != TgtName)
    {
        int baseLen = (int)(dot - TgtName);
        if (baseLen >= (int)sizeof(base))
            baseLen = sizeof(base) - 1;
        memcpy(base, TgtName, baseLen);
        base[baseLen] = 0;
        lstrcpynA(ext, dot, sizeof(ext));
    }
    else
    {
        lstrcpynA(base, TgtName, sizeof(base));
        ext[0] = 0;
    }

    // Check if base ends with " (N)"
    int counter = 1;
    char* openParen = strrchr(base, '(');
    if (openParen != NULL && openParen > base && *(openParen - 1) == ' ')
    {
        char* closeParen = strchr(openParen, ')');
        if (closeParen != NULL && *(closeParen + 1) == 0)
        {
            int num = atoi(openParen + 1);
            if (num > 0)
            {
                counter = num + 1;
                *(openParen - 1) = 0; // strip " (N)"
            }
        }
    }

    _snprintf_s(outBuf, maxLen, _TRUNCATE, "%s (%d)%s", base, counter, ext);
}

INT_PTR CSftpConflictDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // this handler returns FALSE below and so skips CDialog::DialogProc: apply dark mode explicitly
        WinLibApplyDarkMode(HWindow);

        if (CenterWnd != NULL)
        {
            SalamanderGeneral->MultiMonCenterWindow(HWindow, CenterWnd, TRUE);
        }

        SalamanderGUI->AttachButton(HWindow, IDOK, BTF_DROPDOWN);

        SetDlgItemTextA(HWindow, IDE_SCRD_SRCPATH, SrcPath);
        SetDlgItemTextA(HWindow, IDE_SCRD_SRCNAME, SrcName);
        SetDlgItemTextA(HWindow, IDE_SCRD_TGTPATH, TgtPath);
        SetDlgItemTextA(HWindow, IDE_SCRD_TGTNAME, TgtName);

        // Put initial focus on the Overwrite button (matching FTP plugin behavior)
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, CM_SIED_OVERWRITE), TRUE);
        return FALSE; // handled focus
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (LOWORD(wParam) == IDOK)
        {
            HMENU hMenu = LoadMenuA(HLanguage, MAKEINTRESOURCEA(IDM_FILEEXISTSERRRETRY));
            if (hMenu != NULL)
            {
                HMENU hSubMenu = GetSubMenu(hMenu, 0);
                if (hSubMenu != NULL)
                {
                    CGUIMenuPopupAbstract* salMenu = SalamanderGUI->CreateMenuPopup();
                    if (salMenu != NULL)
                    {
                        salMenu->SetTemplateMenu(hSubMenu);
                        RECT r;
                        GetWindowRect(GetDlgItem(HWindow, (int)wParam), &r);
                        DWORD cmd = salMenu->Track(MENU_TRACK_RETURNCMD, r.left, r.bottom, HWindow, &r);
                        if (cmd != 0)
                        {
                            PostMessage(HWindow, WM_COMMAND, cmd, 0);
                        }
                        SalamanderGUI->DestroyMenuPopup(salMenu);
                    }
                }
                DestroyMenu(hMenu);
            }
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case CM_SCRD_USEALTNAME:
        {
            char altName[MAX_PATH];
            GenerateAlternateName(altName, sizeof(altName));
            SetDlgItemTextA(HWindow, IDE_SCRD_TGTNAME, altName);
            SendDlgItemMessageA(HWindow, IDE_SCRD_TGTNAME, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(HWindow, IDE_SCRD_TGTNAME));
            return TRUE;
        }

        case IDOK:
        case CM_SIED_RESUME:
        case CM_SIED_RESUMEOROVR:
        case CM_SIED_OVERWRITE:
        case CM_SIED_OVERWRITEALL:
        case IDB_SCRD_SKIP:
        {
            GetDlgItemTextA(HWindow, IDE_SCRD_TGTNAME, ResultName, sizeof(ResultName));
            ApplyToAll = (IsDlgButtonChecked(HWindow, IDC_SCRD_APPLYTOALL) == BST_CHECKED);

            switch (LOWORD(wParam))
            {
            case IDOK:
                Action = SFTP_CONFLICT_RETRY;
                break;
            case CM_SIED_RESUME:
                Action = SFTP_CONFLICT_RESUME;
                break;
            case CM_SIED_RESUMEOROVR:
                Action = SFTP_CONFLICT_RESUME_OR_OVERWRITE;
                break;
            case CM_SIED_OVERWRITE:
                Action = SFTP_CONFLICT_OVERWRITE;
                break;
            case CM_SIED_OVERWRITEALL:
                Action = SFTP_CONFLICT_OVERWRITE_ALL;
                ApplyToAll = TRUE;
                break;
            case IDB_SCRD_SKIP:
                Action = SFTP_CONFLICT_SKIP;
                break;
            }

            EndDialog(HWindow, IDOK);
            return TRUE;
        }

        case IDCANCEL:
        {
            Action = SFTP_CONFLICT_CANCEL;
            EndDialog(HWindow, IDCANCEL);
            return TRUE;
        }

        case IDHELP:
        {
            if (SalamanderGeneral != NULL)
            {
                SalamanderGeneral->OpenHtmlHelp(HWindow, HHCDisplayIndex, 0, FALSE);
            }
            return TRUE;
        }
        }
        break;
    }

    // WM_THEMECHANGED / WM_SETTINGCHANGE / WM_CTLCOLOR* are handled by CDialog::DialogProc (winliblt)
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
