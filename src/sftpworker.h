// Copyright © 2026 Dupl3xx
//
// Background worker thread for asynchronous SFTP transfers.
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include "sftpconn.h"
#include "sftpglue.h"

// Custom window messages for worker -> UI notifications
#define WM_APP_SFTP_WORKER_UPDATE        (WM_APP + 101)
#define WM_APP_SFTP_WORKER_FINISHED      (WM_APP + 102)
#define WM_APP_SFTP_WORKER_ASK_OVERWRITE (WM_APP + 103)

// Single transfer task in the worker queue
struct CSftpTransferTask
{
    enum Type { TaskDownload, TaskUpload };
    enum TaskStatus { StatusWaiting = 0, StatusRunning, StatusDone, StatusError, StatusSkipped };

    Type TaskType;
    TaskStatus Status;
    std::string RemotePath;
    std::string LocalPath;
    unsigned __int64 FileSize;      // expected file size (or 0 if unknown)
    unsigned __int64 ResumeOffset;  // 0 = start from beginning
    bool IsDirectory;               // true = recursive directory
    bool DeleteSourceOnSuccess;     // true for Move operation
    std::string ErrorMsg;

    CSftpTransferTask()
        : TaskType(TaskDownload), Status(StatusWaiting),
          FileSize(0), ResumeOffset(0), IsDirectory(false), DeleteSourceOnSuccess(false)
    {
    }
};

// Thread-safe shared state snapshot
struct CSftpTransferState
{
    CRITICAL_SECTION Lock;

    char CurrentLocalFile[MAX_PATH];
    char CurrentRemoteFile[MAX_PATH];
    unsigned __int64 CurrentFileDone;
    unsigned __int64 CurrentFileTotal;

    int CurrentItemIndex;
    int TotalItemsCount;
    unsigned __int64 TotalBytesDone;
    unsigned __int64 TotalBytesExpected;

    DWORD StartTick;
    DWORD LastUpdateTick;
    double BytesPerSec;

    volatile bool IsRunning;
    volatile bool IsPaused;
    volatile bool Cancelled;
    volatile bool HasError;
    char ErrorMsg[512];

    void Init()
    {
        InitializeCriticalSection(&Lock);
        Reset();
    }

    void Destroy()
    {
        DeleteCriticalSection(&Lock);
    }

    void Reset()
    {
        CurrentLocalFile[0] = 0;
        CurrentRemoteFile[0] = 0;
        CurrentFileDone = 0;
        CurrentFileTotal = 0;
        CurrentItemIndex = 0;
        TotalItemsCount = 0;
        TotalBytesDone = 0;
        TotalBytesExpected = 0;
        StartTick = 0;
        LastUpdateTick = 0;
        BytesPerSec = 0.0;
        IsRunning = false;
        IsPaused = false;
        Cancelled = false;
        HasError = false;
        ErrorMsg[0] = 0;
    }
};

// Observer interface for detached dialog notification
class ISftpTransferDlgObserver
{
public:
    virtual ~ISftpTransferDlgObserver() {}
    virtual void DetachWorker() = 0;
};

// Background worker thread managing dedicated CSftpConnection
class CSftpTransferWorker
{
public:
    CSftpTransferWorker();
    ~CSftpTransferWorker();

    // Start worker thread with target connection profile
    bool Start(const CSftpProfile& profile, HWND dlgHwnd = NULL);
    // Gracefully stop worker and wait for thread termination
    void Stop();
    // Reset worker state and queue for fresh transfer operation
    void Reset();
    // Add transfer task to queue
    void EnqueueTask(const CSftpTransferTask& task);
    // Cancel ongoing and queued transfers
    void Cancel();

    // Pause / Resume transfer
    void SetPaused(bool paused);
    bool IsPaused() const;

    bool IsRunning() const;
    bool HasTasks() const;
    void SetDlgHwnd(HWND hwnd) { DlgHwnd = hwnd; }
    void SetObserver(ISftpTransferDlgObserver* obs) { AttachedObserver = obs; }

    // Take a thread-safe snapshot of the current state
    void GetStateSnapshot(CSftpTransferState& outState);
    // Take a thread-safe snapshot of all transfer tasks
    void GetTasksSnapshot(std::vector<CSftpTransferTask>& outTasks);

    // Get reference to worker connection profile
    const CSftpProfile& GetProfile() const { return Profile; }

private:
    static unsigned __stdcall ThreadEntryPoint(void* param);
    void ThreadLoop();

    bool ExecuteTask(CSftpTransferTask& task);
    bool DoDownloadRecursive(const std::string& remote, const std::string& local, bool isDir, bool deleteSource = false);
    bool DoUploadRecursive(const std::string& local, const std::string& remote, bool isDir, bool deleteSource = false);

    static bool StaticProgressCallback(void* ctx, const char* name, unsigned __int64 done, unsigned __int64 total);
    bool ReportProgress(const char* name, unsigned __int64 done, unsigned __int64 total);

    HANDLE ThreadHandle;
    DWORD ThreadId;
    HANDLE WakeEvent;
    HANDLE StopEvent;
    HANDLE RunEvent;
    HWND DlgHwnd;

    CRITICAL_SECTION QueueLock;
    std::deque<CSftpTransferTask> TaskQueue;
    std::vector<CSftpTransferTask> AllTasks;
    size_t CurrentTaskIndex;

    CSftpConnection WorkerConn;
    CSftpProfile Profile;
    CSftpTransferState State;
    ISftpTransferDlgObserver* AttachedObserver;
    bool Initialized;

    // Overwrite decision handling in worker
    std::string SkippedLinkPath;  // first junction/symlink directory of the current task that was skipped
    std::string DeleteFailedPath; // first source file of the current task that Move could not delete
    int OverwriteAllDecision; // -1 = ask, 0 = skip all, 1 = overwrite all, 2 = resume all, 3 = resume or overwrite all
    int AskOverwriteWorker(const char* srcPath, const char* srcName,
                           const char* tgtPath, const char* tgtName,
                           bool isLocalTarget,
                           unsigned __int64 existingSize, unsigned __int64 newSize,
                           unsigned __int64& outResumeOffset,
                           std::string& outNewTargetName);
};

