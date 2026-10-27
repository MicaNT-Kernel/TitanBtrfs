#include "titanbtrfs/btrfs.hpp"
#include "titanbtrfs/device.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

using namespace titanbtrfs;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "========================================================================\n";
        std::cout << "             TitanBtrfs Volume Formatting Utility                       \n";
        std::cout << "========================================================================\n\n";
        std::cout << "Usage: titanbtrfs_format <image_path_or_device> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --size <MB>       Volume size in megabytes (default: 256, minimum: 16)\n";
        std::cout << "  --label <name>    Volume label (default: TITAN_BTRFS)\n";
        std::cout << "  --sectorsize <B>  Sector size in bytes (default: 4096)\n";
        std::cout << "  --nodesize <B>    B-tree node size in bytes (default: 16384)\n\n";
        std::cout << "Examples:\n";
        std::cout << "  titanbtrfs_format btrfs_disk.img --size 512\n";
        std::cout << "  titanbtrfs_format btrfs_disk.img --size 1024 --label SteamDeck\n";
        std::cout << "  titanbtrfs_format \\\\.\\PhysicalDrive2 --label Backup\n\n";
        return 0;
    }

    std::string devicePath = argv[1];
    uint64_t sizeMB = 256;
    std::string label = "TITAN_BTRFS";
    uint32_t sectorSize = BTRFS_DEFAULT_SECTOR_SIZE;
    uint32_t nodeSize = BTRFS_DEFAULT_NODE_SIZE;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            sizeMB = std::stoull(argv[++i]);
        } else if (arg == "--label" && i + 1 < argc) {
            label = argv[++i];
        } else if (arg == "--sectorsize" && i + 1 < argc) {
            sectorSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--nodesize" && i + 1 < argc) {
            nodeSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }

    if (sizeMB < 16) {
        std::cerr << "Error: Minimum volume size is 16 MB.\n";
        return 1;
    }

    uint64_t totalBytes = sizeMB * 1024ULL * 1024ULL;

    std::cout << "[*] Initializing block device: " << devicePath << " (" << sizeMB << " MB)\n";
    try {
        auto dev = std::make_shared<FileBlockDevice>(devicePath, totalBytes, sectorSize);
        BtrfsFileSystem fs(dev);

        std::cout << "[*] Formatting volume with clean-room Btrfs architecture...\n";
        if (!fs.format(totalBytes, sectorSize, nodeSize, label)) {
            std::cerr << "[-] Error: Failed to format Btrfs volume on " << devicePath << "\n";
            return 1;
        }

        std::cout << "[+] Btrfs volume successfully formatted!\n";
        std::cout << "    Label:       " << label << "\n";
        std::cout << "    Total Size:  " << (totalBytes / (1024 * 1024)) << " MB\n";
        std::cout << "    Sector Size: " << sectorSize << " bytes\n";
        std::cout << "    Node Size:   " << nodeSize << " bytes\n";
        std::cout << "    Superblock:  offset 0x10000 (Magic: _BHRfS_M at 0x10040)\n";
        std::cout << "    Root Subvol: @ (ID 5, FS_TREE)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[-] Exception: " << ex.what() << "\n";
        return 1;
    }
}
