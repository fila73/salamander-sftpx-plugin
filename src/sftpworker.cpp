// Copyright © 2026 Dupl3xx
//
// Background worker thread for asynchronous SFTP transfers.
#include "precomp.h"
#include "sftpworker.h"
#include "sftpconflictdlg.h"
#include <process.h>
#include <shlwapi.h>
#include <stdio.h>
#ifndef NO_OPENSSL
#include <openssl/crypto.h>
#endif

static void SftpSplitPathAndName(const char* fullPath, char separator, std::string& outDir, std::string& outName)
{
    if (fullPath == NULL || fullPath[0] == 0)
    {
        outDir = "";
        outName = "";
        return;
    }
    const char* p = strrchr(fullPath, separator);
    if (separator == '\\')
    {
        const char* p2 = strrchr(fullPath, '/');
        if (p2 > p)
            p = p2;
    }
    if (p != NULL)
    {
        outDir.assign(fullPath, p - fullPath);
        outName.assign(p + 1);
    }
    else
    {
        outDir = "";
        outName = fullPath;
    }
}

CSftpTransferWorker::CSftpTransferWorker()
    : ThreadHandle(NULL), ThreadId(0), WakeEvent(NULL), StopEvent(NULL), RunEvent(NULL),
      DlgHwnd(NULL), CurrentTaskIndex(0), AttachedObserver(NULL), Initialized(true), OverwriteAllDecision(-1)
{
    InitializeCriticalSection(&QueueLock);
    State.Init();
    WakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    RunEvent = CreateEvent(NULL, TRUE, TRUE, NULL); // manual reset, initially signaled (running)
    memset(&Profile, 0, sizeof(Profile));
}

CSftpTransferWorker::~CSftpTransferWorker()
{
    Stop();
    Initialized = false;
    if (AttachedObserver != NULL)
    {
        AttachedObserver->DetachWorker();
        AttachedObserver = NULL;
    }
    DlgHwnd = NULL;
    if (WakeEvent)
    {
        CloseHandle(WakeEvent);
        WakeEvent = NULL;
    }
    if (StopEvent)
    {
        CloseHandle(StopEvent);
        StopEvent = NULL;
    }
    if (RunEvent)
    {
        CloseHandle(RunEvent);
        RunEvent = NULL;
    }
    DeleteCriticalSection(&QueueLock);
    State.Destroy();
}

void CSftpTransferWorker::Reset()
{
    if (ThreadHandle != NULL)
    {
        DWORD exitCode = 0;
        if (GetExitCodeThread(ThreadHandle, &exitCode) && exitCode != STILL_ACTIVE)
        {
            CloseHandle(ThreadHandle);
            ThreadHandle = NULL;
            ThreadId = 0;
        }
    }

    EnterCriticalSection(&QueueLock);
    TaskQueue.clear();
    AllTasks.clear();
    CurrentTaskIndex = 0;
    LeaveCriticalSection(&QueueLock);

    EnterCriticalSection(&State.Lock);
    State.Reset();
    LeaveCriticalSection(&State.Lock);

    if (RunEvent)
        SetEvent(RunEvent);

    OverwriteAllDecision = -1;
}

bool CSftpTransferWorker::Start(const CSftpProfile& profile, HWND dlgHwnd)
{
    if (ThreadHandle != NULL)
    {
        DWORD exitCode = 0;
        if (GetExitCodeThread(ThreadHandle, &exitCode) && exitCode != STILL_ACTIVE)
        {
            CloseHandle(ThreadHandle);
            ThreadHandle = NULL;
            ThreadId = 0;
        }
        else
        {
            return false; // already running
        }
    }

    Profile = profile;
    DlgHwnd = dlgHwnd;
    OverwriteAllDecision = -1;

    ResetEvent(StopEvent);
    ResetEvent(WakeEvent);
    if (RunEvent)
        SetEvent(RunEvent);

    EnterCriticalSection(&State.Lock);
    // Note: Do not wipe TotalItemsCount or TotalBytesExpected; they were set by EnqueueTask!
    State.CurrentLocalFile[0] = 0;
    State.CurrentRemoteFile[0] = 0;
    State.CurrentFileDone = 0;
    State.CurrentFileTotal = 0;
    State.CurrentItemIndex = 0;
    State.TotalBytesDone = 0;
    State.BytesPerSec = 0.0;
    State.Cancelled = false;
    State.HasError = false;
    State.ErrorMsg[0] = 0;
    State.IsRunning = true;
    State.StartTick = GetTickCount();
    State.LastUpdateTick = State.StartTick;
    LeaveCriticalSection(&State.Lock);

    ThreadHandle = (HANDLE)_beginthreadex(NULL, 0, ThreadEntryPoint, this, 0, (unsigned*)&ThreadId);
    if (!ThreadHandle)
    {
        EnterCriticalSection(&State.Lock);
        State.IsRunning = false;
        State.HasError = true;
        strcpy_s(State.ErrorMsg, "Failed to create worker thread");
        LeaveCriticalSection(&State.Lock);
        return false;
    }
    return true;
}

void CSftpTransferWorker::Stop()
{
    if (AttachedObserver != NULL)
    {
        AttachedObserver->DetachWorker();
        AttachedObserver = NULL;
    }
    DlgHwnd = NULL;

    if (!ThreadHandle)
        return;

    if (Initialized)
    {
        EnterCriticalSection(&State.Lock);
        State.Cancelled = true;
        LeaveCriticalSection(&State.Lock);
    }

    SetEvent(StopEvent);
    SetEvent(WakeEvent);
    if (RunEvent)
        SetEvent(RunEvent);

    if (WaitForSingleObject(ThreadHandle, 5000) == WAIT_TIMEOUT)
    {
        // Thread did not finish in time; terminate as last resort
        TerminateThread(ThreadHandle, 1);
    }
    CloseHandle(ThreadHandle);
    ThreadHandle = NULL;
    ThreadId = 0;

    WorkerConn.Disconnect();

    if (Initialized)
    {
        EnterCriticalSection(&State.Lock);
        State.IsRunning = false;
        LeaveCriticalSection(&State.Lock);
    }
}

void CSftpTransferWorker::EnqueueTask(const CSftpTransferTask& task)
{
    if (!Initialized)
        return;
    EnterCriticalSection(&QueueLock);
    CSftpTransferTask t = task;
    t.Status = CSftpTransferTask::StatusWaiting;
    TaskQueue.push_back(t);
    AllTasks.push_back(t);
    EnterCriticalSection(&State.Lock);
    State.TotalItemsCount++;
    State.TotalBytesExpected += task.FileSize;
    LeaveCriticalSection(&State.Lock);
    LeaveCriticalSection(&QueueLock);

    SetEvent(WakeEvent);
}

void CSftpTransferWorker::Cancel()
{
    if (!Initialized)
        return;
    EnterCriticalSection(&State.Lock);
    State.Cancelled = true;
    LeaveCriticalSection(&State.Lock);
    if (RunEvent)
        SetEvent(RunEvent);
    SetEvent(WakeEvent);
}

void CSftpTransferWorker::SetPaused(bool paused)
{
    if (!Initialized)
        return;
    EnterCriticalSection(&State.Lock);
    State.IsPaused = paused;
    LeaveCriticalSection(&State.Lock);

    if (RunEvent)
    {
        if (paused)
            ResetEvent(RunEvent);
        else
            SetEvent(RunEvent);
    }

    if (DlgHwnd)
        PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_UPDATE, 0, 0);
}

bool CSftpTransferWorker::IsPaused() const
{
    if (!Initialized)
        return false;
    EnterCriticalSection(const_cast<LPCRITICAL_SECTION>(&State.Lock));
    bool paused = State.IsPaused;
    LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&State.Lock));
    return paused;
}

bool CSftpTransferWorker::IsRunning() const
{
    if (!Initialized)
        return false;
    EnterCriticalSection(const_cast<LPCRITICAL_SECTION>(&State.Lock));
    bool running = State.IsRunning;
    LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&State.Lock));
    return running;
}

bool CSftpTransferWorker::HasTasks() const
{
    if (!Initialized)
        return false;
    EnterCriticalSection(const_cast<LPCRITICAL_SECTION>(&QueueLock));
    bool empty = TaskQueue.empty();
    LeaveCriticalSection(const_cast<LPCRITICAL_SECTION>(&QueueLock));
    return !empty;
}

void CSftpTransferWorker::GetStateSnapshot(CSftpTransferState& outState)
{
    if (!Initialized)
        return;
    EnterCriticalSection(&State.Lock);
    memcpy(&outState.CurrentLocalFile, State.CurrentLocalFile, sizeof(State.CurrentLocalFile));
    memcpy(&outState.CurrentRemoteFile, State.CurrentRemoteFile, sizeof(State.CurrentRemoteFile));
    outState.CurrentFileDone = State.CurrentFileDone;
    outState.CurrentFileTotal = State.CurrentFileTotal;
    outState.CurrentItemIndex = State.CurrentItemIndex;
    outState.TotalItemsCount = State.TotalItemsCount;
    outState.TotalBytesDone = State.TotalBytesDone;
    outState.TotalBytesExpected = State.TotalBytesExpected;
    outState.StartTick = State.StartTick;
    outState.LastUpdateTick = State.LastUpdateTick;
    outState.BytesPerSec = State.BytesPerSec;
    outState.IsRunning = State.IsRunning;
    outState.IsPaused = State.IsPaused;
    outState.Cancelled = State.Cancelled;
    outState.HasError = State.HasError;
    memcpy(&outState.ErrorMsg, State.ErrorMsg, sizeof(State.ErrorMsg));
    LeaveCriticalSection(&State.Lock);
}

void CSftpTransferWorker::GetTasksSnapshot(std::vector<CSftpTransferTask>& outTasks)
{
    if (!Initialized)
        return;
    EnterCriticalSection(&QueueLock);
    outTasks = AllTasks;
    LeaveCriticalSection(&QueueLock);
}

unsigned __stdcall CSftpTransferWorker::ThreadEntryPoint(LPVOID param)
{
    CSftpTransferWorker* worker = static_cast<CSftpTransferWorker*>(param);
    worker->ThreadLoop();
    _endthreadex(0);
    return 0;
}

void CSftpTransferWorker::ThreadLoop()
{
    // 1. Connect dedicated session
    const char* keyFile = (Profile.KeyFile[0] != 0) ? Profile.KeyFile : nullptr;
    const char* sftpServer = (Profile.SftpServer[0] != 0) ? Profile.SftpServer : nullptr;

    bool connected = WorkerConn.Connect(
        Profile.Host, Profile.Port, Profile.User, Profile.Password,
        keyFile, Profile.UseCompression, Profile.Protocol,
        Profile.ScpFallback, sftpServer
    );

    if (!connected)
    {
        EnterCriticalSection(&State.Lock);
        State.HasError = true;
        State.IsRunning = false;
        strncpy_s(State.ErrorMsg, WorkerConn.LastError(), _TRUNCATE);
        LeaveCriticalSection(&State.Lock);

        if (DlgHwnd)
            PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_FINISHED, 0, 0);
        return;
    }

    WorkerConn.SetProgressCallback(StaticProgressCallback, this);

    HANDLE waitHandles[2] = { StopEvent, WakeEvent };

    while (1)
    {
        if (WaitForSingleObject(StopEvent, 0) == WAIT_OBJECT_0)
            break;

        CSftpTransferTask currentTask;
        bool haveTask = false;

        EnterCriticalSection(&QueueLock);
        if (!TaskQueue.empty())
        {
            currentTask = TaskQueue.front();
            TaskQueue.pop_front();
            haveTask = true;
        }
        LeaveCriticalSection(&QueueLock);

        if (!haveTask)
        {
            // Wait for new task or stop signal
            DWORD wr = WaitForMultipleObjects(2, waitHandles, FALSE, 200);
            if (wr == WAIT_OBJECT_0) // StopEvent
                break;
            if (wr == WAIT_TIMEOUT)
            {
                // Check if we are done (queue empty and no more items being added)
                EnterCriticalSection(&QueueLock);
                bool stillEmpty = TaskQueue.empty();
                LeaveCriticalSection(&QueueLock);
                if (stillEmpty)
                {
                    // If no tasks left and running, we can finish or wait
                    // For now, if queue is empty and was previously processed, we break
                    break;
                }
            }
            continue;
        }

        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            break;
        }
        State.CurrentItemIndex++;
        LeaveCriticalSection(&State.Lock);

        size_t taskIdx = (size_t)-1;
        EnterCriticalSection(&QueueLock);
        if (CurrentTaskIndex < AllTasks.size())
        {
            taskIdx = CurrentTaskIndex++;
            AllTasks[taskIdx].Status = CSftpTransferTask::StatusRunning;
        }
        LeaveCriticalSection(&QueueLock);

        if (DlgHwnd)
            PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_UPDATE, 0, 0);

        DeleteFailedPath.clear();
        bool success = ExecuteTask(currentTask);

        // Move: transfer succeeded but a source file could not be removed - report it
        std::string deleteErr;
        if (success && !DeleteFailedPath.empty())
            deleteErr = "Could not delete source: " + DeleteFailedPath;

        EnterCriticalSection(&QueueLock);
        if (taskIdx != (size_t)-1 && taskIdx < AllTasks.size())
        {
            if (success && !deleteErr.empty())
            {
                AllTasks[taskIdx].Status = CSftpTransferTask::StatusError;
                AllTasks[taskIdx].ErrorMsg = deleteErr;
            }
            else if (success)
                AllTasks[taskIdx].Status = CSftpTransferTask::StatusDone;
            else if (State.Cancelled)
                AllTasks[taskIdx].Status = CSftpTransferTask::StatusSkipped;
            else
            {
                AllTasks[taskIdx].Status = CSftpTransferTask::StatusError;
                AllTasks[taskIdx].ErrorMsg = WorkerConn.LastError();
            }
        }
        LeaveCriticalSection(&QueueLock);

        if (DlgHwnd)
            PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_UPDATE, 0, 0);

        if (!deleteErr.empty())
        {
            // not fatal for the queue: remember the message and continue
            EnterCriticalSection(&State.Lock);
            State.HasError = true;
            strncpy_s(State.ErrorMsg, deleteErr.c_str(), _TRUNCATE);
            LeaveCriticalSection(&State.Lock);
        }

        if (!success)
        {
            EnterCriticalSection(&State.Lock);
            if (State.Cancelled)
            {
                LeaveCriticalSection(&State.Lock);
                break;
            }
            State.HasError = true;
            strncpy_s(State.ErrorMsg, WorkerConn.LastError(), _TRUNCATE);
            LeaveCriticalSection(&State.Lock);
            break;
        }
    }

    WorkerConn.Disconnect();

    EnterCriticalSection(&State.Lock);
    State.IsRunning = false;
    LeaveCriticalSection(&State.Lock);

    if (DlgHwnd)
        PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_FINISHED, 0, 0);

#ifndef NO_OPENSSL
    OPENSSL_thread_stop();
#endif
}

bool CSftpTransferWorker::ExecuteTask(CSftpTransferTask& task)
{
    bool ok = false;
    if (task.TaskType == CSftpTransferTask::TaskDownload)
    {
        // Move: source items are deleted one by one inside the recursive function,
        // only after they were actually transferred (never skipped/failed ones)
        ok = DoDownloadRecursive(task.RemotePath, task.LocalPath, task.IsDirectory, task.DeleteSourceOnSuccess);
    }
    else if (task.TaskType == CSftpTransferTask::TaskUpload)
    {
        ok = DoUploadRecursive(task.LocalPath, task.RemotePath, task.IsDirectory, task.DeleteSourceOnSuccess);
    }
    return ok;
}

bool CSftpTransferWorker::DoDownloadRecursive(const std::string& remote, const std::string& local, bool isDir, bool deleteSource)
{
    {
        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            return false;
        }
        strncpy_s(State.CurrentRemoteFile, remote.c_str(), _TRUNCATE);
        strncpy_s(State.CurrentLocalFile, local.c_str(), _TRUNCATE);
        State.CurrentFileDone = 0;
        State.CurrentFileTotal = 0;
        LeaveCriticalSection(&State.Lock);
    }

    if (!isDir)
    {
        std::string actRemote = remote;
        std::string actLocal = local;
        unsigned __int64 resumeOffset = 0;
        if (GetFileAttributesA(actLocal.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            // Local file exists - determine whether to resume, overwrite, or skip
            WIN32_FILE_ATTRIBUTE_DATA fad;
            unsigned __int64 localSize = 0;
            if (GetFileAttributesExA(actLocal.c_str(), GetFileExInfoStandard, &fad))
            {
                ULARGE_INTEGER uli;
                uli.LowPart = fad.nFileSizeLow;
                uli.HighPart = fad.nFileSizeHigh;
                localSize = uli.QuadPart;
            }

            unsigned __int64 remoteSize = 0;
            WorkerConn.RemoteFileSize(actRemote.c_str(), remoteSize);

            std::string srcDir, srcName, tgtDir, tgtName, newTgtName;
            SftpSplitPathAndName(actRemote.c_str(), '/', srcDir, srcName);
            SftpSplitPathAndName(actLocal.c_str(), '\\', tgtDir, tgtName);

            int decision = AskOverwriteWorker(srcDir.c_str(), srcName.c_str(),
                                             tgtDir.c_str(), tgtName.c_str(),
                                             true, localSize, remoteSize, resumeOffset, newTgtName);
            if (decision == 0) // Skip
                return true;
            if (decision < 0)  // Cancel
            {
                Cancel();
                return false;
            }
            if (!newTgtName.empty() && newTgtName != tgtName)
            {
                if (!tgtDir.empty())
                    actLocal = tgtDir + "\\" + newTgtName;
                else
                    actLocal = newTgtName;
            }
        }

        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            return false;
        }
        LeaveCriticalSection(&State.Lock);

        if (!WorkerConn.Download(actRemote.c_str(), actLocal.c_str(), resumeOffset))
            return false;
        // Transferred successfully - delete the source file (move)
        if (deleteSource && !WorkerConn.RemoveFile(actRemote.c_str()) && DeleteFailedPath.empty())
            DeleteFailedPath = actRemote;
        return true;
    }

    // Recursive directory download
    CreateDirectoryA(local.c_str(), NULL);
    std::vector<CSftpEntry> entries;
    if (!WorkerConn.ListDir(remote.c_str(), entries))
        return false;

    for (size_t i = 0; i < entries.size(); i++)
    {
        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            return false;
        }
        LeaveCriticalSection(&State.Lock);

        char r[MAX_PATH], l[MAX_PATH];
        SftpJoin(remote.c_str(), entries[i].Name.c_str(), r, MAX_PATH);
        lstrcpynA(l, local.c_str(), MAX_PATH);
        PathAppendA(l, entries[i].Name.c_str());

        if (!DoDownloadRecursive(r, l, entries[i].IsDir, deleteSource))
            return false;
    }
    // Move: non-recursive rmdir succeeds only if all children were moved
    // (a directory with skipped files is kept)
    if (deleteSource)
        WorkerConn.RemoveDir(remote.c_str());
    return true;
}

bool CSftpTransferWorker::DoUploadRecursive(const std::string& local, const std::string& remote, bool isDir, bool deleteSource)
{
    {
        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            return false;
        }
        strncpy_s(State.CurrentLocalFile, local.c_str(), _TRUNCATE);
        strncpy_s(State.CurrentRemoteFile, remote.c_str(), _TRUNCATE);
        State.CurrentFileDone = 0;
        State.CurrentFileTotal = 0;
        LeaveCriticalSection(&State.Lock);
    }

    if (!isDir)
    {
        std::string actLocal = local;
        std::string actRemote = remote;
        unsigned __int64 resumeOffset = 0;
        if (WorkerConn.PathType(actRemote.c_str()) == 1) // Remote file exists
        {
            unsigned __int64 remoteSize = 0;
            WorkerConn.RemoteFileSize(actRemote.c_str(), remoteSize);

            WIN32_FILE_ATTRIBUTE_DATA fad;
            unsigned __int64 localSize = 0;
            if (GetFileAttributesExA(actLocal.c_str(), GetFileExInfoStandard, &fad))
            {
                ULARGE_INTEGER uli;
                uli.LowPart = fad.nFileSizeLow;
                uli.HighPart = fad.nFileSizeHigh;
                localSize = uli.QuadPart;
            }

            std::string srcDir, srcName, tgtDir, tgtName, newTgtName;
            SftpSplitPathAndName(actLocal.c_str(), '\\', srcDir, srcName);
            SftpSplitPathAndName(actRemote.c_str(), '/', tgtDir, tgtName);

            int decision = AskOverwriteWorker(srcDir.c_str(), srcName.c_str(),
                                             tgtDir.c_str(), tgtName.c_str(),
                                             false, remoteSize, localSize, resumeOffset, newTgtName);
            if (decision == 0) // Skip
                return true;
            if (decision < 0)  // Cancel
            {
                Cancel();
                return false;
            }
            if (!newTgtName.empty() && newTgtName != tgtName)
            {
                if (!tgtDir.empty())
                {
                    if (tgtDir.back() == '/')
                        actRemote = tgtDir + newTgtName;
                    else
                        actRemote = tgtDir + "/" + newTgtName;
                }
                else
                    actRemote = newTgtName;
            }
        }

        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            return false;
        }
        LeaveCriticalSection(&State.Lock);

        if (!WorkerConn.Upload(actLocal.c_str(), actRemote.c_str(), resumeOffset))
            return false;
        // Transferred successfully - delete the source file (move)
        if (deleteSource && !DeleteFileA(actLocal.c_str()) && DeleteFailedPath.empty())
            DeleteFailedPath = actLocal;
        return true;
    }

    // Never follow junctions/symlinks: their target lies outside the selection
    // (Move would delete foreign data, a link to an ancestor would loop forever).
    // The link stays on disk, so a Move keeps the parent directory too.
    {
        DWORD la = GetFileAttributesA(local.c_str());
        if (la != INVALID_FILE_ATTRIBUTES && (la & FILE_ATTRIBUTE_REPARSE_POINT))
            return true;
    }

    // Recursive directory upload
    WorkerConn.MakeDir(remote.c_str());

    char mask[MAX_PATH];
    lstrcpynA(mask, local.c_str(), MAX_PATH);
    PathAppendA(mask, "*");

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(mask, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return true;

    bool ok = true;
    do
    {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        EnterCriticalSection(&State.Lock);
        if (State.Cancelled)
        {
            LeaveCriticalSection(&State.Lock);
            ok = false;
            break;
        }
        LeaveCriticalSection(&State.Lock);

        char l[MAX_PATH], r[MAX_PATH];
        lstrcpynA(l, local.c_str(), MAX_PATH);
        PathAppendA(l, fd.cFileName);
        SftpJoin(remote.c_str(), fd.cFileName, r, MAX_PATH);

        bool subIsDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!DoUploadRecursive(l, r, subIsDir, deleteSource))
        {
            ok = false;
            break;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    // Move: non-recursive rmdir succeeds only if all children were moved
    // (a directory with skipped files is kept)
    if (ok && deleteSource)
        RemoveDirectoryA(local.c_str());
    return ok;
}

int CSftpTransferWorker::AskOverwriteWorker(const char* srcPath, const char* srcName,
                                           const char* tgtPath, const char* tgtName,
                                           bool isLocalTarget,
                                           unsigned __int64 existingSize, unsigned __int64 newSize,
                                           unsigned __int64& outResumeOffset,
                                           std::string& outNewTargetName)
{
    outResumeOffset = 0;
    outNewTargetName.clear();

    if (OverwriteAllDecision == 1) // Overwrite all
        return 1;
    if (OverwriteAllDecision == 0) // Skip all
        return 0;
    if (OverwriteAllDecision == 2) // Resume all
    {
        if (existingSize > 0 && existingSize < newSize)
            outResumeOffset = existingSize;
        return 1;
    }
    if (OverwriteAllDecision == 3) // Resume or overwrite all
    {
        if (existingSize > 0 && existingSize < newSize)
            outResumeOffset = existingSize;
        else
            outResumeOffset = 0;
        return 1;
    }

    if (DlgHwnd != NULL && IsWindow(DlgHwnd))
    {
        CSftpConflictDlg dlg(DlgHwnd, srcPath, srcName, tgtPath, tgtName, newSize, existingSize);
        ESftpConflictAction action = SFTP_CONFLICT_CANCEL;
        BOOL applyToAll = FALSE;
        char newNameBuf[MAX_PATH] = "";
        INT_PTR res = dlg.ExecuteDlg(action, applyToAll, newNameBuf, sizeof(newNameBuf));

        if (res == IDCANCEL || action == SFTP_CONFLICT_CANCEL)
        {
            return -1; // Cancel
        }

        if (newNameBuf[0] != 0 && strcmp(newNameBuf, tgtName) != 0)
        {
            outNewTargetName = newNameBuf;
        }

        if (applyToAll)
        {
            if (action == SFTP_CONFLICT_OVERWRITE || action == SFTP_CONFLICT_OVERWRITE_ALL)
                OverwriteAllDecision = 1;
            else if (action == SFTP_CONFLICT_SKIP)
                OverwriteAllDecision = 0;
            else if (action == SFTP_CONFLICT_RESUME)
                OverwriteAllDecision = 2;
            else if (action == SFTP_CONFLICT_RESUME_OR_OVERWRITE)
                OverwriteAllDecision = 3;
        }

        switch (action)
        {
        case SFTP_CONFLICT_RETRY:
            outResumeOffset = 0;
            return 1;
        case SFTP_CONFLICT_RESUME:
            // Resuming is only meaningful when the target is smaller than the source;
            // otherwise overwrite (offset == size would transfer 0 bytes and a Move
            // would then delete the source)
            if (existingSize > 0 && existingSize < newSize)
                outResumeOffset = existingSize;
            else
                outResumeOffset = 0;
            return 1;
        case SFTP_CONFLICT_RESUME_OR_OVERWRITE:
            if (existingSize > 0 && existingSize < newSize)
                outResumeOffset = existingSize;
            else
                outResumeOffset = 0;
            return 1;
        case SFTP_CONFLICT_OVERWRITE:
        case SFTP_CONFLICT_OVERWRITE_ALL:
            outResumeOffset = 0;
            return 1;
        case SFTP_CONFLICT_SKIP:
            return 0;
        default:
            return -1;
        }
    }

    // Default fallback for non-interactive test/batch
    outResumeOffset = 0;
    return 1;
}

bool CSftpTransferWorker::StaticProgressCallback(void* ctx, const char* name, unsigned __int64 done, unsigned __int64 total)
{
    CSftpTransferWorker* worker = static_cast<CSftpTransferWorker*>(ctx);
    return worker->ReportProgress(name, done, total);
}

bool CSftpTransferWorker::ReportProgress(const char* name, unsigned __int64 done, unsigned __int64 total)
{
    if (RunEvent && StopEvent)
    {
        HANDLE waitHandles[2] = { StopEvent, RunEvent };
        DWORD wr = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0)
            return false;
    }

    EnterCriticalSection(&State.Lock);
    if (State.Cancelled)
    {
        LeaveCriticalSection(&State.Lock);
        return false;
    }

    DWORD now = GetTickCount();
    DWORD elapsed = now - State.StartTick;
    unsigned __int64 prevDone = State.CurrentFileDone;
    State.CurrentFileDone = done;
    State.CurrentFileTotal = total;

    if (done >= prevDone)
        State.TotalBytesDone += (done - prevDone);

    if (elapsed > 200 && State.TotalBytesDone > 0)
    {
        State.BytesPerSec = (double)State.TotalBytesDone / ((double)elapsed / 1000.0);
    }

    bool notify = (now - State.LastUpdateTick >= 100);
    if (notify)
        State.LastUpdateTick = now;

    LeaveCriticalSection(&State.Lock);

    if (notify && DlgHwnd)
    {
        PostMessage(DlgHwnd, WM_APP_SFTP_WORKER_UPDATE, 0, 0);
    }

    return true;
}
