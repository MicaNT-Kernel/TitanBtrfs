#include "titanbtrfs/btrfs.hpp"
#include "titanbtrfs/device.hpp"

#ifdef _WIN32
#include "titanbtrfs/winfsp_bridge.hpp"
#include <csignal>
#include <atomic>
#include <iostream>
#include <filesystem>
#include <thread>

using namespace titanbtrfs;

namespace {
std::atomic<bool> g_Running{true};
WinFspMountSession g_Session;

void SigHandler(int) {
    std::cout << "\n[TitanBtrfs Mount] Stop signal received. Unmounting volume cleanly...\n";
    g_Running = false;
    g_Session.unmount();
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "========================================================================\n";
        std::cout << "    TitanBtrfs Filesystem Mount Driver (WinFsp Userland Bridge)         \n";
        std::cout << "========================================================================\n\n";
        std::cout << "Usage: titanbtrfs_mount <image_path_or_device> <drive_letter:> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --subvol <name_or_id>  Mount specific subvolume (default: @ / ID 5)\n";
        std::cout << "  --label <name>         Set volume label (default: Btrfs)\n";
        std::cout << "  --format               Format image as clean Btrfs volume before mounting\n";
        std::cout << "  --size <MB>            Backing file size in megabytes if created (default: 256)\n";
        std::cout << "  --sectorsize <B>       Sector size in bytes (default: 4096)\n";
        std::cout << "  --nodesize <B>         Node size in bytes (default: 16384)\n\n";
        std::cout << "Examples:\n";
        std::cout << "  titanbtrfs_mount btrfs_disk.img B:\n";
        std::cout << "  titanbtrfs_mount btrfs_disk.img B: --subvol @home\n";
        std::cout << "  titanbtrfs_mount \\\\.\\PhysicalDrive2 B: --label SteamDeck\n\n";
        return 0;
    }

    std::string devicePath;
    std::string driveLetter;
    std::string subvolTarget = "@";
    std::wstring volumeLabel = L"Btrfs";
    bool doFormat = false;
    uint64_t diskSizeMB = 256;
    uint32_t sectorSize = BTRFS_DEFAULT_SECTOR_SIZE;
    uint32_t nodeSize = BTRFS_DEFAULT_NODE_SIZE;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--format") {
            doFormat = true;
        } else if (arg == "--label" && i + 1 < argc) {
            std::string lStr = argv[++i];
            volumeLabel = std::wstring(lStr.begin(), lStr.end());
        } else if (arg == "--subvol" && i + 1 < argc) {
            subvolTarget = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            diskSizeMB = std::stoull(argv[++i]);
            if (diskSizeMB < 16) diskSizeMB = 16;
        } else if (arg == "--sectorsize" && i + 1 < argc) {
            sectorSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--nodesize" && i + 1 < argc) {
            nodeSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (devicePath.empty()) {
            devicePath = arg;
        } else if (driveLetter.empty()) {
            driveLetter = arg;
        }
    }

    if (devicePath.empty() || driveLetter.empty()) {
        std::cerr << "[-] Error: Both device/image path and drive letter must be specified.\n";
        return 1;
    }

    if (driveLetter.size() == 1) {
        driveLetter += ":";
    } else if (driveLetter.size() > 2 && driveLetter[1] == ':') {
        driveLetter = driveLetter.substr(0, 2);
    }

    auto& fsp = WinFspApi::Get();
    if (!fsp.isAvailable()) {
        std::cerr << "[-] Error: WinFsp dynamic library (winfsp-x64.dll) was not found.\n";
        std::cerr << "Please install WinFsp on this machine (e.g. run titanbtrfs_setup.exe or 'winget install WinFsp.WinFsp').\n";
        return 1;
    }

    uint64_t totalBytes = diskSizeMB * 1024ULL * 1024ULL;

    try {
        auto dev = std::make_shared<FileBlockDevice>(devicePath, totalBytes, sectorSize);
        auto fs = std::make_shared<BtrfsFileSystem>(dev);

        if (doFormat) {
            std::cout << "[*] Formatting " << devicePath << " with clean-room Btrfs architecture...\n";
            std::string labelStr(volumeLabel.begin(), volumeLabel.end());
            if (!fs->format(totalBytes, sectorSize, nodeSize, labelStr)) {
                std::cerr << "[-] Error: Formatting failed on " << devicePath << "\n";
                return 1;
            }
        } else {
            if (!fs->mount()) {
                std::cerr << "[-] Error: Failed to mount Btrfs volume on " << devicePath << "\n";
                return 1;
            }
        }

        // Resolve Subvolume
        uint64_t subvolId = BTRFS_FS_TREE_OBJECTID;
        std::vector<BtrfsSubvolumeInfo> subvols;
        fs->listSubvolumes(subvols);
        for (const auto& s : subvols) {
            if (s.name == subvolTarget || std::to_string(s.id) == subvolTarget) {
                subvolId = s.id;
                break;
            }
        }

        std::wstring wDrive(driveLetter.begin(), driveLetter.end());
        std::cout << "[*] Mounting Btrfs subvolume (ID " << subvolId << ") on drive " << driveLetter << "...\n";

        if (!g_Session.mount(fs, wDrive, volumeLabel, subvolId)) {
            std::cerr << "[-] Error: WinFsp mount session failed to start.\n";
            return 1;
        }

        std::signal(SIGINT, SigHandler);
        std::signal(SIGTERM, SigHandler);

        std::cout << "[+] Volume is mounted and active in Windows Explorer!\n";
        std::cout << "    Drive Letter:  " << driveLetter << "\\\n";
        std::cout << "    Press Ctrl+C to unmount cleanly and exit.\n";

        while (g_Running && g_Session.isMounted()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (g_Session.isMounted()) {
            g_Session.unmount();
        }
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[-] Exception: " << ex.what() << "\n";
        return 1;
    }
}

#else

int main() {
    std::cout << "TitanBtrfs WinFsp mount utility is only supported on Windows.\n";
    return 0;
}

#endif
