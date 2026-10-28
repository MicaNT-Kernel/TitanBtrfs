#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdint>
#include "titanbtrfs/btrfs.hpp"

namespace titanbtrfs {

// ============================================================================
// NTSTATUS Codes
// ============================================================================

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_OBJECT_NAME_NOT_FOUND     ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION     ((NTSTATUS)0xC0000035L)
#define STATUS_ACCESS_DENIED             ((NTSTATUS)0xC0000022L)
#define STATUS_END_OF_FILE               ((NTSTATUS)0xC0000011L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_MEDIA_WRITE_PROTECTED     ((NTSTATUS)0xC00000A2L)
#define STATUS_FILE_IS_A_DIRECTORY       ((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_A_DIRECTORY           ((NTSTATUS)0xC0000103L)
#define STATUS_DIRECTORY_NOT_EMPTY       ((NTSTATUS)0xC0000101L)
#endif

#pragma pack(push, 1)

struct FSP_FSCTL_VOLUME_INFO {
    UINT64 TotalSize;
    UINT64 FreeSize;
    UINT16 VolumeLabelLength;
    WCHAR  VolumeLabel[32];
};

struct FSP_FSCTL_FILE_INFO {
    UINT32 FileAttributes;
    UINT32 ReparseTag;
    UINT64 AllocationSize;
    UINT64 FileSize;
    UINT64 CreationTime;
    UINT64 LastAccessTime;
    UINT64 LastWriteTime;
    UINT64 ChangeTime;
    UINT64 IndexNumber;
    UINT32 HardLinks;
    UINT32 EaSize;
};

struct FSP_FSCTL_DIR_INFO {
    UINT16 Size;
    FSP_FSCTL_FILE_INFO FileInfo;
    union {
        UINT64 NextOffset;
        UINT8  Padding[24];
    };
    WCHAR  FileNameBuf[1];
};

struct FSP_FSCTL_VOLUME_PARAMS {
    UINT16 Version;
    UINT16 SectorSize;
    UINT16 SectorsPerAllocationUnit;
    UINT16 MaxComponentLength;
    UINT64 VolumeCreationTime;
    UINT32 VolumeSerialNumber;
    UINT32 TransactTimeout;
    UINT32 IrpTimeout;
    UINT32 IrpCapacity;
    UINT32 FileInfoTimeout;
    UINT32 Flags;
    WCHAR  FileSystemName[16];
    UINT8  Reserved[432];
};
static_assert(sizeof(FSP_FSCTL_VOLUME_PARAMS) == 504, "FSP_FSCTL_VOLUME_PARAMS must be exactly 504 bytes");

#pragma pack(pop)

struct _FSP_FILE_SYSTEM {
    PVOID UserContext;
};
typedef struct _FSP_FILE_SYSTEM FSP_FILE_SYSTEM;

struct FSP_FILE_SYSTEM_INTERFACE {
    NTSTATUS (*GetVolumeInfo)(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*SetVolumeLabel)(FSP_FILE_SYSTEM *FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*GetSecurityByName)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    NTSTATUS (*Create)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Open)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Overwrite)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    VOID (*Cleanup)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags);
    VOID (*Close)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext);
    NTSTATUS (*Read)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*Write)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Flush)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*GetFileInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetBasicInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetFileSize)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*CanReplace)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID TargetContext);
    NTSTATUS (*Rename)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
    NTSTATUS (*GetSecurity)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    NTSTATUS (*SetSecurity)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor);
    NTSTATUS (*ReadDirectory)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*ResolveReparsePoints)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
    NTSTATUS (*GetReparsePointByName)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize);
    NTSTATUS (*GetDirInfoByName)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, FSP_FSCTL_DIR_INFO *DirInfo);
    NTSTATUS (*Control)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 ControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, PULONG PBytesTransferred);
    NTSTATUS (*SetDelete)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile);
};

// ============================================================================
// Dynamic WinFsp API Loader
// ============================================================================

class WinFspApi {
public:
    static WinFspApi& Get() {
        static WinFspApi instance;
        return instance;
    }

    [[nodiscard]] bool isAvailable() const noexcept { return hModule_ != nullptr; }

    NTSTATUS Create(PWSTR DevicePath, const FSP_FSCTL_VOLUME_PARAMS* VolumeParams, const FSP_FILE_SYSTEM_INTERFACE* Interface, FSP_FILE_SYSTEM** PFileSystem) {
        if (!fnCreate_) return ((NTSTATUS)0xC0000001L);
        return fnCreate_(DevicePath, VolumeParams, Interface, PFileSystem);
    }

    VOID Delete(FSP_FILE_SYSTEM* FileSystem) {
        if (fnDelete_) fnDelete_(FileSystem);
    }

    NTSTATUS StartDispatcher(FSP_FILE_SYSTEM* FileSystem, ULONG ThreadCount) {
        if (!fnStartDispatcher_) return ((NTSTATUS)0xC0000001L);
        return fnStartDispatcher_(FileSystem, ThreadCount);
    }

    VOID StopDispatcher(FSP_FILE_SYSTEM* FileSystem) {
        if (fnStopDispatcher_) fnStopDispatcher_(FileSystem);
    }

    NTSTATUS SetMountPoint(FSP_FILE_SYSTEM* FileSystem, PWSTR MountPoint) {
        if (!fnSetMountPoint_) return ((NTSTATUS)0xC0000001L);
        return fnSetMountPoint_(FileSystem, MountPoint);
    }

    VOID DeleteMountPoint(FSP_FILE_SYSTEM* FileSystem, PWSTR MountPoint) {
        if (fnDeleteMountPoint_) fnDeleteMountPoint_(FileSystem, MountPoint);
    }

    BOOLEAN AddDirInfo(FSP_FSCTL_DIR_INFO* DirInfo, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        if (!fnAddDirInfo_) return FALSE;
        return fnAddDirInfo_(DirInfo, Buffer, Length, PBytesTransferred);
    }

private:
    HMODULE hModule_{nullptr};
    NTSTATUS (*fnCreate_)(PWSTR, const FSP_FSCTL_VOLUME_PARAMS*, const FSP_FILE_SYSTEM_INTERFACE*, FSP_FILE_SYSTEM**){nullptr};
    VOID (*fnDelete_)(FSP_FILE_SYSTEM*){nullptr};
    NTSTATUS (*fnStartDispatcher_)(FSP_FILE_SYSTEM*, ULONG){nullptr};
    VOID (*fnStopDispatcher_)(FSP_FILE_SYSTEM*){nullptr};
    NTSTATUS (*fnSetMountPoint_)(FSP_FILE_SYSTEM*, PWSTR){nullptr};
    VOID (*fnDeleteMountPoint_)(FSP_FILE_SYSTEM*, PWSTR){nullptr};
    BOOLEAN (*fnAddDirInfo_)(FSP_FSCTL_DIR_INFO*, PVOID, ULONG, PULONG){nullptr};

    WinFspApi() {
        loadDll();
    }

    ~WinFspApi() {
        if (hModule_) {
            FreeLibrary(hModule_);
            hModule_ = nullptr;
        }
    }

    void loadDll() {
        hModule_ = LoadLibraryW(L"winfsp-x64.dll");
        if (!hModule_) {
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
                RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                WCHAR installDir[MAX_PATH];
                DWORD dirSize = sizeof(installDir);
                if (RegQueryValueExW(hKey, L"InstallDir", nullptr, nullptr, reinterpret_cast<LPBYTE>(installDir), &dirSize) == ERROR_SUCCESS) {
                    std::wstring fullPath = std::wstring(installDir) + L"bin\\winfsp-x64.dll";
                    hModule_ = LoadLibraryW(fullPath.c_str());
                }
                RegCloseKey(hKey);
            }
        }

        if (hModule_) {
            fnCreate_ = reinterpret_cast<decltype(fnCreate_)>(GetProcAddress(hModule_, "FspFileSystemCreate"));
            fnDelete_ = reinterpret_cast<decltype(fnDelete_)>(GetProcAddress(hModule_, "FspFileSystemDelete"));
            fnStartDispatcher_ = reinterpret_cast<decltype(fnStartDispatcher_)>(GetProcAddress(hModule_, "FspFileSystemStartDispatcher"));
            fnStopDispatcher_ = reinterpret_cast<decltype(fnStopDispatcher_)>(GetProcAddress(hModule_, "FspFileSystemStopDispatcher"));
            fnSetMountPoint_ = reinterpret_cast<decltype(fnSetMountPoint_)>(GetProcAddress(hModule_, "FspFileSystemSetMountPoint"));
            fnDeleteMountPoint_ = reinterpret_cast<decltype(fnDeleteMountPoint_)>(GetProcAddress(hModule_, "FspFileSystemDeleteMountPoint"));
            fnAddDirInfo_ = reinterpret_cast<decltype(fnAddDirInfo_)>(GetProcAddress(hModule_, "FspFileSystemAddDirInfo"));
        }
    }
};

// ============================================================================
// File Context & Mount Session
// ============================================================================

struct BtrfsFileContext {
    std::string path;
    bool isDirectory{false};
    bool deleteOnClose{false};
};

class WinFspMountSession {
private:
    std::shared_ptr<BtrfsFileSystem> fs_;
    std::wstring driveLetter_;
    std::wstring volumeLabel_;
    uint64_t subvolId_{BTRFS_FS_TREE_OBJECTID};
    FSP_FILE_SYSTEM* fspFs_{nullptr};
    FSP_FILE_SYSTEM_INTERFACE interface_{};
    bool mounted_{false};

public:
    WinFspMountSession() = default;
    ~WinFspMountSession() {
        unmount();
    }

    bool mount(std::shared_ptr<BtrfsFileSystem> fs, const std::wstring& driveLetter,
               const std::wstring& volumeLabel = L"Btrfs", uint64_t subvolId = BTRFS_FS_TREE_OBJECTID) {
        if (mounted_ || !fs) return false;

        auto& fsp = WinFspApi::Get();
        if (!fsp.isAvailable()) {
            std::cerr << "[TitanBtrfs Mount] Error: WinFsp runtime (winfsp-x64.dll) was not found.\n";
            return false;
        }

        fs_ = fs;
        driveLetter_ = driveLetter;
        volumeLabel_ = volumeLabel;
        subvolId_ = subvolId;
        fs_->setCurrentSubvolume(subvolId_);

        initInterface();

        FSP_FSCTL_VOLUME_PARAMS params{};
        params.Version = sizeof(FSP_FSCTL_VOLUME_PARAMS);
        params.SectorSize = static_cast<UINT16>(fs_->getSectorSize());
        params.SectorsPerAllocationUnit = 1;
        params.MaxComponentLength = 255;
        params.VolumeSerialNumber = 0x54495441U; // "TITA"
        params.Flags = 0;

        const wchar_t* fsName = L"BTRFS";
        std::wcsncpy(params.FileSystemName, fsName, 15);

        NTSTATUS status = fsp.Create(nullptr, &params, &interface_, &fspFs_);
        if (status != STATUS_SUCCESS || !fspFs_) {
            std::cerr << "[TitanBtrfs Mount] FspFileSystemCreate failed with NTSTATUS: 0x"
                      << std::hex << status << "\n";
            return false;
        }

        fspFs_->UserContext = this;

        status = fsp.SetMountPoint(fspFs_, const_cast<PWSTR>(driveLetter_.c_str()));
        if (status != STATUS_SUCCESS) {
            std::cerr << "[TitanBtrfs Mount] FspFileSystemSetMountPoint ("
                      << std::string(driveLetter_.begin(), driveLetter_.end())
                      << ") failed: 0x" << std::hex << status << "\n";
            fsp.Delete(fspFs_);
            fspFs_ = nullptr;
            return false;
        }

        status = fsp.StartDispatcher(fspFs_, 0);
        if (status != STATUS_SUCCESS) {
            std::cerr << "[TitanBtrfs Mount] FspFileSystemStartDispatcher failed: 0x"
                      << std::hex << status << "\n";
            fsp.DeleteMountPoint(fspFs_, const_cast<PWSTR>(driveLetter_.c_str()));
            fsp.Delete(fspFs_);
            fspFs_ = nullptr;
            return false;
        }

        mounted_ = true;
        std::wcout << L"[TitanBtrfs Mount] Successfully mounted Btrfs volume on " << driveLetter_ << L"\\\n";
        return true;
    }

    void unmount() {
        if (!mounted_ || !fspFs_) return;

        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            fsp.StopDispatcher(fspFs_);
            fsp.DeleteMountPoint(fspFs_, const_cast<PWSTR>(driveLetter_.c_str()));
            fsp.Delete(fspFs_);
        }

        fspFs_ = nullptr;
        mounted_ = false;
        std::wcout << L"[TitanBtrfs Mount] Unmounted " << driveLetter_ << L" successfully.\n";
    }

    [[nodiscard]] bool isMounted() const noexcept { return mounted_; }

private:
    void initInterface() {
        std::memset(&interface_, 0, sizeof(interface_));
        interface_.GetVolumeInfo = CbGetVolumeInfo;
        interface_.SetVolumeLabel = CbSetVolumeLabel;
        interface_.GetSecurityByName = CbGetSecurityByName;
        interface_.Create = CbCreate;
        interface_.Open = CbOpen;
        interface_.Overwrite = CbOverwrite;
        interface_.Cleanup = CbCleanup;
        interface_.Close = CbClose;
        interface_.Read = CbRead;
        interface_.Write = CbWrite;
        interface_.Flush = CbFlush;
        interface_.GetFileInfo = CbGetFileInfo;
        interface_.SetBasicInfo = CbSetBasicInfo;
        interface_.SetFileSize = CbSetFileSize;
        interface_.Rename = CbRename;
        interface_.ReadDirectory = CbReadDirectory;
        interface_.SetDelete = CbSetDelete;
    }

    static std::string WStringToString(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        std::string str(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), len, nullptr, nullptr);
        return str;
    }

    static std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
        std::wstring wstr(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstr.data(), len);
        return wstr;
    }

    static std::string NormalizePath(PWSTR FileName) {
        std::wstring wpath(FileName ? FileName : L"\\");
        for (auto& c : wpath) {
            if (c == L'\\') c = L'/';
        }
        std::string path = WStringToString(wpath);
        if (path.empty() || path[0] != '/') {
            path = "/" + path;
        }
        return path;
    }

    void populateFileInfo(const std::string& path, FSP_FSCTL_FILE_INFO* FileInfo) {
        if (!FileInfo) return;
        std::memset(FileInfo, 0, sizeof(FSP_FSCTL_FILE_INFO));

        BtrfsFileInfo info{};
        if (path == "/" || !fs_->getFileInfo(path, info)) {
            FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            FileInfo->AllocationSize = 4096;
            FileInfo->FileSize = 0;
            FileInfo->HardLinks = 1;
            return;
        }

        if (info.isDirectory) {
            FileInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            FileInfo->AllocationSize = 4096;
            FileInfo->FileSize = 0;
        } else {
            FileInfo->FileAttributes = FILE_ATTRIBUTE_ARCHIVE;
            FileInfo->FileSize = info.size;
            FileInfo->AllocationSize = (info.size + 4095) & ~4095ULL;
        }

        uint64_t winTime = (info.mtime + 11644473600ULL) * 10000000ULL;
        FileInfo->CreationTime = winTime;
        FileInfo->LastAccessTime = winTime;
        FileInfo->LastWriteTime = winTime;
        FileInfo->ChangeTime = winTime;
        FileInfo->HardLinks = info.nlink ? info.nlink : 1;
        FileInfo->IndexNumber = info.inodeNumber;
    }

    // Callbacks
    static NTSTATUS CbGetVolumeInfo(FSP_FILE_SYSTEM* FileSystem, FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        VolumeInfo->TotalSize = session->fs_->getTotalBytes();
        VolumeInfo->FreeSize = (session->fs_->getTotalBytes() > session->fs_->getBytesUsed())
            ? (session->fs_->getTotalBytes() - session->fs_->getBytesUsed()) : 0;

        size_t cch = std::min(session->volumeLabel_.size(), size_t(31));
        std::wcsncpy(VolumeInfo->VolumeLabel, session->volumeLabel_.c_str(), cch);
        VolumeInfo->VolumeLabel[cch] = 0;
        VolumeInfo->VolumeLabelLength = static_cast<UINT16>(cch * sizeof(WCHAR));

        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetVolumeLabel(FSP_FILE_SYSTEM* FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session) return STATUS_ACCESS_DENIED;

        if (VolumeLabel) {
            session->volumeLabel_ = VolumeLabel;
        }
        return CbGetVolumeInfo(FileSystem, VolumeInfo);
    }

    static NTSTATUS CbGetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) {
        (void)SecurityDescriptor;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::string path = NormalizePath(FileName);
        if (path == "/") {
            if (PFileAttributes) *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        } else {
            BtrfsFileInfo info{};
            if (!session->fs_->getFileInfo(path, info)) {
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            if (PFileAttributes) {
                *PFileAttributes = info.isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
            }
        }

        if (PSecurityDescriptorSize) *PSecurityDescriptorSize = 0;
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbCreate(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)GrantedAccess; (void)FileAttributes; (void)SecurityDescriptor; (void)AllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::string path = NormalizePath(FileName);
        bool isDir = (CreateOptions & 0x00000001 /* FILE_DIRECTORY_FILE */) != 0;

        if (isDir) {
            if (!session->fs_->createDirectory(path)) {
                return STATUS_OBJECT_NAME_COLLISION;
            }
        } else {
            if (!session->fs_->createFile(path)) {
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }

        auto* ctx = new BtrfsFileContext();
        ctx->path = path;
        ctx->isDirectory = isDir;
        *PFileContext = ctx;

        session->populateFileInfo(path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbOpen(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)CreateOptions; (void)GrantedAccess;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::string path = NormalizePath(FileName);
        BtrfsFileInfo info{};
        bool exists = session->fs_->getFileInfo(path, info);

        if (!exists && path != "/") {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        auto* ctx = new BtrfsFileContext();
        ctx->path = path;
        ctx->isDirectory = (path == "/") || info.isDirectory;
        *PFileContext = ctx;

        session->populateFileInfo(path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbOverwrite(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)FileAttributes; (void)ReplaceFileAttributes; (void)AllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->fs_->truncateFile(ctx->path, 0);
        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static VOID CbCleanup(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags) {
        (void)FileName; (void)Flags;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return;

        if (ctx->deleteOnClose) {
            if (ctx->isDirectory) {
                session->fs_->removeDirectory(ctx->path);
            } else {
                session->fs_->deleteFile(ctx->path);
            }
        }
    }

    static VOID CbClose(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext) {
        (void)FileSystem;
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        delete ctx;
    }

    static NTSTATUS CbRead(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        std::vector<uint8_t> data;
        size_t bytesRead = 0;
        if (!session->fs_->readFile(ctx->path, Offset, Length, data, bytesRead)) {
            return STATUS_END_OF_FILE;
        }

        if (bytesRead > 0) {
            std::memcpy(Buffer, data.data(), bytesRead);
        }

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(bytesRead);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbWrite(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)ConstrainedIo;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        if (WriteToEndOfFile) {
            BtrfsFileInfo info{};
            if (session->fs_->getFileInfo(ctx->path, info)) {
                Offset = info.size;
            }
        }

        std::vector<uint8_t> data(Length);
        std::memcpy(data.data(), Buffer, Length);

        size_t written = 0;
        if (!session->fs_->writeFile(ctx->path, Offset, data, written)) {
            return STATUS_ACCESS_DENIED;
        }

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(written);
        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbFlush(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbGetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetBasicInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)FileAttributes; (void)CreationTime; (void)LastAccessTime; (void)LastWriteTime; (void)ChangeTime;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetFileSize(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)SetAllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->fs_->truncateFile(ctx->path, NewSize);
        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbRename(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) {
        (void)FileName; (void)ReplaceIfExists;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        std::string newPath = NormalizePath(NewFileName);
        if (session->fs_->renamePath(ctx->path, newPath)) {
            ctx->path = newPath;
            return STATUS_SUCCESS;
        }
        return STATUS_ACCESS_DENIED;
    }

    static NTSTATUS CbReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        (void)Pattern;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        auto& fsp = WinFspApi::Get();

        std::vector<BtrfsDirEntry> entries;
        session->fs_->listDirectory(ctx->path, entries);

        // Include "." and ".."
        std::vector<BtrfsDirEntry> allEntries;
        allEntries.push_back({".", 0, BTRFS_FT_DIR, 0});
        allEntries.push_back({"..", 0, BTRFS_FT_DIR, 0});
        for (const auto& e : entries) {
            allEntries.push_back(e);
        }

        std::wstring wMarker = Marker ? Marker : L"";
        size_t startIdx = 0;
        if (!wMarker.empty()) {
            std::string markerStr = WStringToString(wMarker);
            for (size_t i = 0; i < allEntries.size(); ++i) {
                if (allEntries[i].name == markerStr) {
                    startIdx = i + 1;
                    break;
                }
            }
        }

        ULONG totalTransferred = 0;
        for (size_t i = startIdx; i < allEntries.size(); ++i) {
            const auto& e = allEntries[i];
            std::wstring wname = StringToWString(e.name);

            UINT16 dirInfoSize = static_cast<UINT16>(sizeof(FSP_FSCTL_DIR_INFO) + (wname.size() * sizeof(WCHAR)));
            std::vector<uint8_t> dirInfoBuf(dirInfoSize, 0);
            auto* dirInfo = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(dirInfoBuf.data());

            dirInfo->Size = dirInfoSize;
            std::memcpy(dirInfo->FileNameBuf, wname.c_str(), (wname.size() + 1) * sizeof(WCHAR));

            if (e.fileType == BTRFS_FT_DIR) {
                dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
                dirInfo->FileInfo.AllocationSize = 4096;
            } else {
                dirInfo->FileInfo.FileAttributes = FILE_ATTRIBUTE_ARCHIVE;
                dirInfo->FileInfo.FileSize = e.size;
                dirInfo->FileInfo.AllocationSize = (e.size + 4095) & ~4095ULL;
            }

            ULONG bytesTransferred = 0;
            if (!fsp.AddDirInfo(dirInfo, Buffer, Length, &bytesTransferred)) {
                break;
            }
        }

        if (PBytesTransferred) *PBytesTransferred = totalTransferred;
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetDelete(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile) {
        (void)FileSystem; (void)FileName;
        auto* ctx = reinterpret_cast<BtrfsFileContext*>(FileContext);
        if (ctx) {
            ctx->deleteOnClose = (DeleteFile != FALSE);
        }
        return STATUS_SUCCESS;
    }
};

} // namespace titanbtrfs

#endif // _WIN32
