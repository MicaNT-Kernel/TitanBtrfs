#include "titanbtrfs/btrfs.hpp"
#include "titanbtrfs/device.hpp"
#include "titanbtrfs/ipc.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <filesystem>

#ifdef _WIN32
#include "titanbtrfs/winfsp_bridge.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#endif

namespace titanbtrfs {

struct MountedVolumeInfo {
    std::string devicePath;
    std::string driveLetter;
    std::string subvolumeName;
    uint64_t    subvolumeId{BTRFS_FS_TREE_OBJECTID};
    std::shared_ptr<FileBlockDevice> device;
    std::shared_ptr<BtrfsFileSystem> fs;
#ifdef _WIN32
    std::shared_ptr<WinFspMountSession> session;
#endif
};

class TitanBtrfsDaemon {
public:
    static TitanBtrfsDaemon& Get() {
        static TitanBtrfsDaemon instance;
        return instance;
    }

    bool start() {
        if (running_) return true;
        running_ = true;

        std::cout << "========================================================================\n";
        std::cout << "        TitanBtrfs Windows NT Subsystem Service Daemon                  \n";
        std::cout << "========================================================================\n";
        std::cout << "[TitanBtrfs Daemon] Initializing filesystem and mount subsystem...\n";

#ifdef _WIN32
        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            std::cout << "[TitanBtrfs Daemon] WinFsp dynamic engine loaded successfully.\n";
            loadConfigAndMount();
        } else {
            std::cout << "[TitanBtrfs Daemon] WinFsp not found; running in headless storage engine mode.\n";
        }
#endif

        // Start Named Pipe IPC Server
        ipcServer_.start([this](uint32_t cmd, const std::string& req, std::string& resp) {
            return this->handleIpcCommand(cmd, req, resp);
        });
        std::cout << "[TitanBtrfs Daemon] Named pipe IPC endpoint active: \\\\.\\pipe\\titanbtrfs_ipc\n";

        // Start 24-hour Autonomous B-Tree Integrity Watchdog & Scrubber
        watchdogThread_ = std::thread(&TitanBtrfsDaemon::watchdogWorker, this);
        std::cout << "[TitanBtrfs Daemon] Autonomous B-Tree integrity watchdog & scrubber started.\n";
        std::cout << "[TitanBtrfs Daemon] Service is ready and accepting requests.\n\n";

        return true;
    }

    void stop() {
        if (!running_) return;
        std::cout << "\n[TitanBtrfs Daemon] Initiating graceful subsystem shutdown...\n";
        running_ = false;

        ipcServer_.stop();

        if (watchdogThread_.joinable()) {
            watchdogThread_.join();
        }

        std::lock_guard<std::mutex> lock(mountMutex_);
#ifdef _WIN32
        for (auto& [drive, vol] : mounts_) {
            std::cout << "[TitanBtrfs Daemon] Unmounting " << drive << " (" << vol.devicePath << ")...\n";
            if (vol.session) {
                vol.session->unmount();
            }
            if (vol.device) {
                vol.device->flush();
            }
        }
#endif
        mounts_.clear();
        std::cout << "[TitanBtrfs Daemon] Subsystem stopped. All Btrfs volumes unmounted.\n";
    }

    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    int mountVolume(const std::string& devPath, const std::string& driveLetter, const std::string& subvolName, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        std::string drive = normalizeDriveLetter(driveLetter);

        if (mounts_.find(drive) != mounts_.end()) {
            err = "Drive letter " + drive + " is already in use by TitanBtrfs.";
            return -1;
        }

#ifdef _WIN32
        auto& fsp = WinFspApi::Get();
        if (!fsp.isAvailable()) {
            err = "WinFsp dynamic driver library is not available.";
            return -2;
        }

        try {
            auto dev = std::make_shared<FileBlockDevice>(devPath, 0);
            auto fs = std::make_shared<BtrfsFileSystem>(dev);

            if (!fs->mount()) {
                err = "Failed to mount Btrfs volume on " + devPath + " (invalid magic or checksum).";
                return -3;
            }

            uint64_t targetSubvolId = BTRFS_FS_TREE_OBJECTID;
            std::vector<BtrfsSubvolumeInfo> subvols;
            fs->listSubvolumes(subvols);
            for (const auto& s : subvols) {
                if (s.name == subvolName || std::to_string(s.id) == subvolName) {
                    targetSubvolId = s.id;
                    break;
                }
            }

            auto session = std::make_shared<WinFspMountSession>();
            std::wstring wDrive(drive.begin(), drive.end());
            std::wstring wLabel(fs->getLabel().begin(), fs->getLabel().end());

            if (!session->mount(fs, wDrive, wLabel, targetSubvolId)) {
                err = "WinFsp mount failed to attach drive letter " + drive;
                return -4;
            }

            MountedVolumeInfo info;
            info.devicePath = devPath;
            info.driveLetter = drive;
            info.subvolumeName = subvolName;
            info.subvolumeId = targetSubvolId;
            info.device = dev;
            info.fs = fs;
            info.session = session;

            mounts_[drive] = info;
            saveConfig();
            return 0;

        } catch (const std::exception& ex) {
            err = std::string("Exception: ") + ex.what();
            return -5;
        }
#else
        (void)devPath; (void)subvolName;
        err = "WinFsp driver mounting only supported on Windows.";
        return -99;
#endif
    }

    int unmountVolume(const std::string& driveLetter, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        std::string drive = normalizeDriveLetter(driveLetter);

        auto it = mounts_.find(drive);
        if (it == mounts_.end()) {
            err = "Volume " + drive + " is not currently mounted.";
            return -1;
        }

#ifdef _WIN32
        if (it->second.session) {
            it->second.session->unmount();
        }
        if (it->second.device) {
            it->second.device->flush();
        }
#endif
        mounts_.erase(it);
        saveConfig();
        return 0;
    }

private:
    std::atomic<bool> running_{false};
    std::mutex mountMutex_;
    std::map<std::string, MountedVolumeInfo> mounts_;
    IpcServer ipcServer_;
    std::thread watchdogThread_;

    TitanBtrfsDaemon() = default;

    static std::string normalizeDriveLetter(std::string d) {
        if (d.empty()) return "B:";
        if (d.size() == 1) return d + ":";
        if (d.size() >= 2 && d[1] == ':') return d.substr(0, 2);
        return d;
    }

    void loadConfigAndMount() {
        std::filesystem::path cfgPath = getConfigPath();
        if (!std::filesystem::exists(cfgPath)) return;

        std::ifstream in(cfgPath);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string dev, drive, subvol;
            if (std::getline(ss, dev, ',') && std::getline(ss, drive, ',')) {
                if (!std::getline(ss, subvol, ',')) {
                    subvol = "@";
                }
                std::string err;
                std::cout << "[TitanBtrfs Daemon] Auto-mounting " << dev << " to " << drive << " (subvol: " << subvol << ")...\n";
                mountVolume(dev, drive, subvol, err);
            }
        }
    }

    void saveConfig() {
        std::filesystem::path cfgPath = getConfigPath();
        std::error_code ec;
        std::filesystem::create_directories(cfgPath.parent_path(), ec);

        std::ofstream out(cfgPath, std::ios::trunc);
        if (!out.is_open()) return;

        out << "# TitanBtrfs Persistent Mount Configuration\n";
        for (const auto& [drive, vol] : mounts_) {
            out << vol.devicePath << "," << drive << "," << vol.subvolumeName << "\n";
        }
    }

    static std::filesystem::path getConfigPath() {
#ifdef _WIN32
        const char* appData = std::getenv("PROGRAMDATA");
        if (appData) {
            return std::filesystem::path(appData) / "TitanBtrfs" / "mounts.conf";
        }
#endif
        return "mounts.conf";
    }

    void watchdogWorker() {
        while (running_) {
            for (int i = 0; i < 24 * 3600 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running_) break;

            std::cout << "\n[TitanBtrfs Daemon] Initiating scheduled 24-hour B-tree integrity scrub...\n";
            std::lock_guard<std::mutex> lock(mountMutex_);
            for (auto& [drive, vol] : mounts_) {
                if (vol.fs) {
                    BtrfsScrubReport report{};
                    bool ok = vol.fs->scrub(report);
                    if (!ok) {
                        std::cerr << "[TitanBtrfs Scrubber] Warning: Errors on " << drive << " (" << vol.devicePath << "). Auto-repairing...\n";
                        vol.fs->repair(report);
                    } else {
                        std::cout << "[TitanBtrfs Scrubber] Volume " << drive << " (" << vol.devicePath << ") is clean.\n";
                    }
                }
            }
        }
    }

    int handleIpcCommand(uint32_t cmd, const std::string& request, std::string& response) {
        std::stringstream ss;
        switch (cmd) {
            case IpcCmdPing:
                response = "{\"status\":\"ok\",\"service\":\"TitanBtrfs\",\"version\":\"1.0.0\"}";
                return 0;

            case IpcCmdServiceStatus: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                ss << "{\"status\":\"running\",\"mountCount\":" << mounts_.size() << "}";
                response = ss.str();
                return 0;
            }

            case IpcCmdListMounts: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                ss << "{\"mounts\":[";
                bool first = true;
                for (const auto& [drive, vol] : mounts_) {
                    if (!first) ss << ",";
                    first = false;
                    ss << "{\"drive\":\"" << drive << "\",\"device\":\"" << vol.devicePath
                       << "\",\"subvol\":\"" << vol.subvolumeName << "\",\"totalBytes\":"
                       << (vol.fs ? vol.fs->getTotalBytes() : 0) << "}";
                }
                ss << "]}";
                response = ss.str();
                return 0;
            }

            case IpcCmdVolumeMount: {
                std::stringstream reqSs(request);
                std::string dev, drive, subvol;
                reqSs >> dev >> drive >> subvol;
                if (subvol.empty()) subvol = "@";

                std::string err;
                int res = mountVolume(dev, drive, subvol, err);
                if (res == 0) {
                    response = "{\"status\":\"mounted\",\"drive\":\"" + drive + "\"}";
                } else {
                    response = "{\"status\":\"error\",\"message\":\"" + err + "\"}";
                }
                return res;
            }

            case IpcCmdVolumeUnmount: {
                std::string drive = request;
                std::string err;
                int res = unmountVolume(drive, err);
                if (res == 0) {
                    response = "{\"status\":\"unmounted\",\"drive\":\"" + drive + "\"}";
                } else {
                    response = "{\"status\":\"error\",\"message\":\"" + err + "\"}";
                }
                return res;
            }

            case IpcCmdScrubStart: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                uint64_t totalBlocks = 0;
                uint64_t totalErrors = 0;
                for (auto& [drive, vol] : mounts_) {
                    if (vol.fs) {
                        BtrfsScrubReport report{};
                        vol.fs->scrub(report);
                        totalBlocks += report.totalBlocksScanned;
                        totalErrors += report.checksumErrors;
                    }
                }
                ss << "{\"status\":\"scrub_complete\",\"blocksScanned\":" << totalBlocks
                   << ",\"errors\":" << totalErrors << "}";
                response = ss.str();
                return 0;
            }

            case IpcCmdSubvolumeList: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                ss << "{\"subvolumes\":[";
                bool first = true;
                for (const auto& [drive, vol] : mounts_) {
                    if (vol.fs) {
                        std::vector<BtrfsSubvolumeInfo> subvols;
                        vol.fs->listSubvolumes(subvols);
                        for (const auto& s : subvols) {
                            if (!first) ss << ",";
                            first = false;
                            ss << "{\"drive\":\"" << drive << "\",\"id\":" << s.id
                               << ",\"name\":\"" << s.name << "\",\"isSnapshot\":"
                               << (s.is_snapshot ? "true" : "false") << "}";
                        }
                    }
                }
                ss << "]}";
                response = ss.str();
                return 0;
            }

            default:
                response = "{\"status\":\"error\",\"message\":\"Unknown command\"}";
                return -1;
        }
    }
};

#ifdef _WIN32
SERVICE_STATUS        g_ServiceStatus;
SERVICE_STATUS_HANDLE g_StatusHandle;

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            TitanBtrfsDaemon::Get().stop();
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            break;
        default:
            break;
    }
}

VOID WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_StatusHandle = RegisterServiceCtrlHandlerW(L"TitanBtrfs", ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_ServiceStatus.dwWin32ExitCode = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    auto& daemon = TitanBtrfsDaemon::Get();
    if (!daemon.start()) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    while (daemon.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

bool InstallService() {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;

    SC_HANDLE svc = CreateServiceW(
        scm,
        L"TitanBtrfs",
        L"TitanBtrfs Storage Subsystem Daemon",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        path,
        nullptr,
        nullptr,
        L"WinFsp\0",
        nullptr,
        nullptr
    );

    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = const_cast<LPWSTR>(L"Clean-room Btrfs storage engine, subvolume manager, WinFsp user-mode driver mount daemon, and 24-hour B-tree integrity scrubber.");
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    std::cout << "[+] TitanBtrfs Service registered and set to Auto-Start successfully.\n";
    return true;
}

bool UninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, L"TitanBtrfs", SERVICE_STOP | DELETE);
    if (!svc) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status;
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    bool ok = DeleteService(svc) != FALSE;

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    std::cout << "[+] TitanBtrfs Service unregistered and deleted successfully.\n";
    return ok;
}
#endif

} // namespace titanbtrfs

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string action = argv[1];
#ifdef _WIN32
        if (action == "install" || action == "--install") {
            return titanbtrfs::InstallService() ? 0 : 1;
        }
        if (action == "uninstall" || action == "--uninstall") {
            return titanbtrfs::UninstallService() ? 0 : 1;
        }
#endif
        if (action == "run" || action == "--console" || action == "-c" || action == "--debug") {
            auto& daemon = titanbtrfs::TitanBtrfsDaemon::Get();
            daemon.start();

            std::signal(SIGINT, [](int) {
                titanbtrfs::TitanBtrfsDaemon::Get().stop();
            });
            std::signal(SIGTERM, [](int) {
                titanbtrfs::TitanBtrfsDaemon::Get().stop();
            });

            while (daemon.isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            return 0;
        }

        std::cout << "TitanBtrfs Windows NT Subsystem Service Daemon\n";
        std::cout << "Usage: titanbtrfs_svc [command]\n\n";
        std::cout << "Commands:\n";
        std::cout << "  run, --console, -c     Run daemon directly in console (interactive/debug)\n";
#ifdef _WIN32
        std::cout << "  install, --install     Register as Windows NT Auto-Start Service\n";
        std::cout << "  uninstall, --uninstall Unregister and remove Windows NT Service\n";
#endif
        return 0;
    }

#ifdef _WIN32
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { const_cast<LPWSTR>(L"TitanBtrfs"), titanbtrfs::ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        auto& daemon = titanbtrfs::TitanBtrfsDaemon::Get();
        daemon.start();
        while (daemon.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
#else
    auto& daemon = titanbtrfs::TitanBtrfsDaemon::Get();
    daemon.start();
    while (daemon.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif
    return 0;
}
