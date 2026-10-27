#include "titanbtrfs/btrfs.hpp"
#include "titanbtrfs/device.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <iomanip>

using namespace titanbtrfs;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "========================================================================\n";
        std::cout << "        TitanBtrfs Inspection & Integrity Scrubber Utility              \n";
        std::cout << "========================================================================\n\n";
        std::cout << "Usage: titanbtrfs_check <image_path_or_device> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --repair, /f      Perform automated self-healing repair on damaged structures\n";
        std::cout << "  --subvols         List all subvolumes and snapshots\n";
        std::cout << "  --verbose         Display detailed B-tree node statistics\n\n";
        std::cout << "Examples:\n";
        std::cout << "  titanbtrfs_check btrfs_disk.img\n";
        std::cout << "  titanbtrfs_check btrfs_disk.img --subvols\n";
        std::cout << "  titanbtrfs_check \\\\.\\PhysicalDrive2 --repair\n\n";
        return 0;
    }

    std::string devicePath = argv[1];
    bool doRepair = false;
    bool showSubvols = false;
    bool verbose = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--repair" || arg == "/f" || arg == "-r") {
            doRepair = true;
        } else if (arg == "--subvols" || arg == "-s") {
            showSubvols = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }

    if (!std::filesystem::exists(devicePath) && devicePath.find("\\\\.\\") != 0) {
        std::cerr << "[-] Error: Specified image or device path does not exist: " << devicePath << "\n";
        return 1;
    }

    try {
        auto dev = std::make_shared<FileBlockDevice>(devicePath, 0);
        BtrfsFileSystem fs(dev);

        std::cout << "[*] Inspecting Btrfs device: " << devicePath << "\n";

        if (!fs.mount()) {
            std::cerr << "[-] Warning: Standard mount failed or superblock checksum is invalid.\n";
            if (doRepair) {
                std::cout << "[*] Attempting automated self-healing repair (--repair)...\n";
                BtrfsScrubReport report{};
                if (fs.repair(report)) {
                    std::cout << "[+] Self-healing repair complete! Superblock reconstructed.\n";
                    if (!fs.mount()) {
                        std::cerr << "[-] Error: Remounting repaired volume failed.\n";
                        return 1;
                    }
                } else {
                    std::cerr << "[-] Error: Repair engine could not restore volume.\n";
                    return 1;
                }
            } else {
                std::cerr << "[-] Hint: Run with '--repair' to automatically restore damaged structures.\n";
                return 1;
            }
        }

        std::cout << "[+] Btrfs Volume Mounted Successfully!\n";
        std::cout << "    Volume Label: " << fs.getLabel() << "\n";
        std::cout << "    Total Size:   " << (fs.getTotalBytes() / (1024 * 1024)) << " MB\n";
        std::cout << "    Bytes Used:   " << fs.getBytesUsed() << " bytes\n";
        std::cout << "    Sector Size:  " << fs.getSectorSize() << " bytes\n";
        std::cout << "    Node Size:    " << fs.getNodeSize() << " bytes\n";
        std::cout << "    Generation:   " << fs.getGeneration() << "\n";

        std::cout << "\n[*] Auditing B-Trees and Castagnoli CRC32C checksums...\n";
        BtrfsScrubReport scrubReport{};
        bool scrubOk = fs.scrub(scrubReport);

        std::cout << "    Blocks Scanned:  " << scrubReport.totalBlocksScanned << "\n";
        std::cout << "    Bytes Verified:  " << scrubReport.totalBytesVerified << "\n";
        std::cout << "    Checksum Errors: " << scrubReport.checksumErrors << "\n";
        std::cout << "    Superblock OK:   " << (scrubReport.superblockValid ? "YES" : "NO") << "\n";
        std::cout << "    Tree Roots OK:   " << (scrubReport.treeRootsValid ? "YES" : "NO") << "\n";

        if (showSubvols || verbose) {
            std::cout << "\n[*] Subvolume & Snapshot Hierarchy:\n";
            std::vector<BtrfsSubvolumeInfo> subvols;
            fs.listSubvolumes(subvols);
            for (const auto& sub : subvols) {
                std::cout << "    - Subvol ID " << sub.id
                          << " [Parent " << sub.parent_id << "]: \""
                          << sub.name << "\" ("
                          << (sub.is_snapshot ? "Snapshot" : "Subvolume")
                          << ", Generation " << sub.generation << ")\n";
            }
        }

        if (!scrubOk) {
            std::cerr << "\n[-] Integrity Violations Detected:\n";
            for (const auto& err : scrubReport.errors) {
                std::cerr << "    ! " << err << "\n";
            }

            if (doRepair) {
                std::cout << "\n[*] Executing automated self-healing repair (--repair)...\n";
                BtrfsScrubReport repResult{};
                if (fs.repair(repResult)) {
                    std::cout << "[+] Repaired " << repResult.tornWritesFixed << " damaged structures.\n";
                    std::cout << "[+] Status: HEALTHY (Repaired)\n";
                    return 0;
                }
            }
            std::cout << "\n[-] Status: CORRUPTED (Run with '--repair' to fix)\n";
            return 1;
        }

        std::cout << "\n[+] Status: CLEAN (100% Verified)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[-] Exception: " << ex.what() << "\n";
        return 1;
    }
}
