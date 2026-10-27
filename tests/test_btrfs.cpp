#include "titanbtrfs/btrfs.hpp"
#include "titanbtrfs/device.hpp"
#include "titanbtrfs/compression.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <filesystem>
#include <cstring>

using namespace titanbtrfs;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] Assertion FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

bool Test_CRC32C_Castagnoli() {
    std::cout << "[*] Running Test_CRC32C_Castagnoli...\n";
    // Standard test vector for Castagnoli CRC32C: "123456789" -> 0xE3069283
    const char* testStr = "123456789";
    uint32_t crc = Crc32c::Calculate(testStr, 9);
    TEST_ASSERT(crc == 0xE3069283U, "CRC32C Castagnoli standard test vector mismatch");
    std::cout << "    [PASS] Castagnoli CRC32C standard test vector verified (0xE3069283)\n";
    return true;
}

bool Test_Superblock_Validation_And_Magic() {
    std::cout << "[*] Running Test_Superblock_Validation_And_Magic...\n";
    std::string imgPath = "test_sb.img";
    uint64_t sizeBytes = 32 * 1024 * 1024; // 32 MB

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);

    TEST_ASSERT(fs.format(sizeBytes, 4096, 16384, "TEST_BTRFS"), "Format failed");

    // Read back raw superblock from disk at 64 KiB
    BtrfsSuperBlock sb{};
    TEST_ASSERT(dev->read(BTRFS_SUPER_INFO_OFFSET, &sb, sizeof(sb)), "Failed to read raw superblock");
    TEST_ASSERT(std::memcmp(sb.magic, BTRFS_MAGIC_STR, 8) == 0, "Magic signature _BHRfS_M mismatch");
    TEST_ASSERT(sb.total_bytes == sizeBytes, "Total bytes mismatch");
    TEST_ASSERT(sb.sectorsize == 4096, "Sector size mismatch");
    TEST_ASSERT(sb.nodesize == 16384, "Node size mismatch");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Superblock magic _BHRfS_M and on-disk geometry verified\n";
    return true;
}

bool Test_Format_And_Mount() {
    std::cout << "[*] Running Test_Format_And_Mount...\n";
    std::string imgPath = "test_mount.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    TEST_ASSERT(fs.format(sizeBytes, 4096, 16384, "TitanDrive"), "Format failed");
    TEST_ASSERT(fs.getLabel() == "TitanDrive", "Label mismatch");

    // Remount
    BtrfsFileSystem fs2(dev);
    TEST_ASSERT(fs2.mount(), "Mount failed");
    TEST_ASSERT(fs2.getLabel() == "TitanDrive", "Remount label mismatch");
    TEST_ASSERT(fs2.getCurrentSubvolumeId() == BTRFS_FS_TREE_OBJECTID, "Default subvolume should be 5");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Format and remount roundtrip verified\n";
    return true;
}

bool Test_Subvolume_Creation_And_Listing() {
    std::cout << "[*] Running Test_Subvolume_Creation_And_Listing...\n";
    std::string imgPath = "test_subvol.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    fs.format(sizeBytes, 4096, 16384, "SubvolTest");

    uint64_t homeId = 0, varId = 0;
    TEST_ASSERT(fs.createSubvolume("@home", BTRFS_FS_TREE_OBJECTID, &homeId), "Failed to create @home");
    TEST_ASSERT(fs.createSubvolume("@var", BTRFS_FS_TREE_OBJECTID, &varId), "Failed to create @var");
    TEST_ASSERT(homeId != varId && homeId >= 256, "Subvolume ID allocation failed");

    std::vector<BtrfsSubvolumeInfo> subvols;
    TEST_ASSERT(fs.listSubvolumes(subvols), "Failed to list subvolumes");
    TEST_ASSERT(subvols.size() == 3, "Expected 3 subvolumes (@, @home, @var)");

    bool foundHome = false, foundVar = false, foundRoot = false;
    for (const auto& s : subvols) {
        if (s.name == "@") foundRoot = true;
        if (s.name == "@home") foundHome = true;
        if (s.name == "@var") foundVar = true;
    }
    TEST_ASSERT(foundRoot && foundHome && foundVar, "Missing subvolume in listing");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Subvolume hierarchy and metadata listing verified\n";
    return true;
}

bool Test_Snapshot_Creation_Instant_CoW() {
    std::cout << "[*] Running Test_Snapshot_Creation_Instant_CoW...\n";
    std::string imgPath = "test_snap.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    fs.format(sizeBytes, 4096, 16384, "SnapTest");

    uint64_t homeId = 0;
    fs.createSubvolume("@home", BTRFS_FS_TREE_OBJECTID, &homeId);
    fs.setCurrentSubvolume(homeId);

    // Create file inside @home
    fs.createFile("/profile.txt");
    std::string initialData = "User profile configuration v1.0";
    std::vector<uint8_t> inBuf(initialData.begin(), initialData.end());
    size_t written = 0;
    fs.writeFile("/profile.txt", 0, inBuf, written);

    // Snapshot @home to @home_snap1
    uint64_t snapId = 0;
    TEST_ASSERT(fs.createSnapshot(homeId, "@home_snap1", &snapId), "Snapshot creation failed");
    TEST_ASSERT(snapId > homeId, "Snapshot ID invalid");

    // Modify file in original @home
    std::string modifiedData = "User profile configuration MODIFIED v2.0";
    std::vector<uint8_t> modBuf(modifiedData.begin(), modifiedData.end());
    fs.writeFile("/profile.txt", 0, modBuf, written);

    // Verify snapshot still has original data
    fs.setCurrentSubvolume(snapId);
    std::vector<uint8_t> readBuf;
    size_t bytesRead = 0;
    TEST_ASSERT(fs.readFile("/profile.txt", 0, 1024, readBuf, bytesRead), "Failed to read file from snapshot");
    std::string snapData(readBuf.begin(), readBuf.end());
    TEST_ASSERT(snapData == initialData, "Snapshot data changed! Copy-on-Write isolation failed");

    // Verify source has modified data
    fs.setCurrentSubvolume(homeId);
    readBuf.clear();
    fs.readFile("/profile.txt", 0, 1024, readBuf, bytesRead);
    std::string curData(readBuf.begin(), readBuf.end());
    TEST_ASSERT(curData == modifiedData, "Source data was not modified correctly");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Instant Copy-on-Write snapshot isolation verified\n";
    return true;
}

bool Test_File_IO_Lifecycle() {
    std::cout << "[*] Running Test_File_IO_Lifecycle...\n";
    std::string imgPath = "test_io.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    fs.format(sizeBytes, 4096, 16384, "IOTest");

    TEST_ASSERT(fs.createFile("/notes.txt"), "Create file failed");

    std::string text = "TitanBtrfs High-Performance Storage Engine";
    std::vector<uint8_t> writeData(text.begin(), text.end());
    size_t written = 0;
    TEST_ASSERT(fs.writeFile("/notes.txt", 0, writeData, written), "Write failed");
    TEST_ASSERT(written == text.size(), "Write size mismatch");

    BtrfsFileInfo info{};
    TEST_ASSERT(fs.getFileInfo("/notes.txt", info), "GetFileInfo failed");
    TEST_ASSERT(info.size == text.size(), "File size mismatch");
    TEST_ASSERT(!info.isDirectory, "Should not be directory");

    std::vector<uint8_t> readData;
    size_t readCount = 0;
    TEST_ASSERT(fs.readFile("/notes.txt", 0, 1024, readData, readCount), "Read failed");
    TEST_ASSERT(readCount == text.size(), "Read count mismatch");
    TEST_ASSERT(std::string(readData.begin(), readData.end()) == text, "Data mismatch");

    // Truncate
    TEST_ASSERT(fs.truncateFile("/notes.txt", 5), "Truncate failed");
    fs.getFileInfo("/notes.txt", info);
    TEST_ASSERT(info.size == 5, "Truncated size mismatch");

    // Delete
    TEST_ASSERT(fs.deleteFile("/notes.txt"), "Delete failed");
    TEST_ASSERT(!fs.getFileInfo("/notes.txt", info), "File should no longer exist");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] File create, write, read, truncate, and delete verified\n";
    return true;
}

bool Test_Directory_Lifecycle() {
    std::cout << "[*] Running Test_Directory_Lifecycle...\n";
    std::string imgPath = "test_dir.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    fs.format(sizeBytes, 4096, 16384, "DirTest");

    TEST_ASSERT(fs.createDirectory("/docs"), "Create directory /docs failed");
    TEST_ASSERT(fs.createDirectory("/docs/projects"), "Create directory /docs/projects failed");
    TEST_ASSERT(fs.createFile("/docs/projects/spec.md"), "Create /docs/projects/spec.md failed");

    std::vector<BtrfsDirEntry> entries;
    TEST_ASSERT(fs.listDirectory("/docs", entries), "List /docs failed");
    TEST_ASSERT(entries.size() == 1 && entries[0].name == "projects", "List /docs contents mismatch");

    entries.clear();
    TEST_ASSERT(fs.listDirectory("/docs/projects", entries), "List /docs/projects failed");
    TEST_ASSERT(entries.size() == 1 && entries[0].name == "spec.md", "List /docs/projects contents mismatch");

    // Rename
    TEST_ASSERT(fs.renamePath("/docs/projects/spec.md", "/docs/projects/readme.md"), "Rename failed");
    entries.clear();
    fs.listDirectory("/docs/projects", entries);
    TEST_ASSERT(entries.size() == 1 && entries[0].name == "readme.md", "Renamed file not found");

    // Directory deletion
    TEST_ASSERT(!fs.removeDirectory("/docs/projects"), "Should not delete non-empty directory");
    fs.deleteFile("/docs/projects/readme.md");
    TEST_ASSERT(fs.removeDirectory("/docs/projects"), "Should delete empty directory");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Directory hierarchy, listing, rename, and cleanup verified\n";
    return true;
}

bool Test_Scrubbing_And_Self_Healing() {
    std::cout << "[*] Running Test_Scrubbing_And_Self_Healing...\n";
    std::string imgPath = "test_scrub.img";
    uint64_t sizeBytes = 32 * 1024 * 1024;

    auto dev = std::make_shared<FileBlockDevice>(imgPath, sizeBytes);
    BtrfsFileSystem fs(dev);
    fs.format(sizeBytes, 4096, 16384, "ScrubTest");

    BtrfsScrubReport report{};
    TEST_ASSERT(fs.scrub(report), "Clean volume should pass scrub");
    TEST_ASSERT(report.superblockValid && report.treeRootsValid, "Roots should be valid");
    TEST_ASSERT(report.checksumErrors == 0, "Zero errors expected");

    // Inject deliberate corruption in superblock checksum
    fs.corruptSuperblockForTesting();

    BtrfsScrubReport badReport{};
    TEST_ASSERT(!fs.scrub(badReport), "Corrupted volume should fail scrub");
    TEST_ASSERT(badReport.checksumErrors > 0, "Checksum errors should be detected");
    TEST_ASSERT(!badReport.superblockValid, "Superblock should be marked invalid");

    // Execute Self-Healing Repair
    BtrfsScrubReport repReport{};
    TEST_ASSERT(fs.repair(repReport), "Repair should succeed");
    TEST_ASSERT(repReport.tornWritesFixed > 0, "Should have fixed torn writes");

    // Re-scrub to confirm clean status
    BtrfsScrubReport finalReport{};
    TEST_ASSERT(fs.scrub(finalReport), "Post-repair volume must pass scrub cleanly");
    TEST_ASSERT(finalReport.checksumErrors == 0, "Post-repair checksum errors must be 0");

    dev->close();
    std::filesystem::remove(imgPath);
    std::cout << "    [PASS] Scrubbing, bit-rot detection, and automated repair verified\n";
    return true;
}

bool Test_Compression_LZ4_And_RLE() {
    std::cout << "[*] Running Test_Compression_LZ4_And_RLE...\n";

    // 1. RLE Uniform Buffer
    std::vector<uint8_t> zeros(65536, 0);
    std::vector<uint8_t> rleComp;
    size_t rleSz = CompressionEngine::CompressRLE(zeros, rleComp);
    TEST_ASSERT(rleSz == 9, "All-zero RLE block should compress to 9 bytes");

    std::vector<uint8_t> rleDecomp(65536, 0xFF);
    TEST_ASSERT(CompressionEngine::DecompressRLE(rleComp, rleDecomp), "RLE decompression failed");
    TEST_ASSERT(rleDecomp == zeros, "RLE data mismatch");

    // 2. LZ4 Compression
    std::string text = "Btrfs B-Tree File System Copy-on-Write Subvolumes Snapshots CRC32C Extent Trees ";
    std::string repeated;
    for (int i = 0; i < 50; ++i) repeated += text;

    std::vector<uint8_t> raw(repeated.begin(), repeated.end());
    std::vector<uint8_t> lz4Comp;
    size_t lz4Sz = CompressionEngine::CompressLZ4(raw, lz4Comp);
    TEST_ASSERT(lz4Sz > 0 && lz4Sz < raw.size(), "LZ4 compression failed to reduce size");

    std::vector<uint8_t> lz4Decomp(raw.size());
    TEST_ASSERT(CompressionEngine::DecompressLZ4(lz4Comp, lz4Decomp), "LZ4 decompression failed");
    TEST_ASSERT(lz4Decomp == raw, "LZ4 decompressed data mismatch");

    std::cout << "    [PASS] Clean-room LZ4 and RLE compression verified\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "TitanBtrfs Clean-Room Verification Suite                \n";
    std::cout << "========================================================\n";

    if (!Test_CRC32C_Castagnoli()) return 1;
    if (!Test_Superblock_Validation_And_Magic()) return 1;
    if (!Test_Format_And_Mount()) return 1;
    if (!Test_Subvolume_Creation_And_Listing()) return 1;
    if (!Test_Snapshot_Creation_Instant_CoW()) return 1;
    if (!Test_File_IO_Lifecycle()) return 1;
    if (!Test_Directory_Lifecycle()) return 1;
    if (!Test_Scrubbing_And_Self_Healing()) return 1;
    if (!Test_Compression_LZ4_And_RLE()) return 1;

    std::cout << "========================================================\n";
    std::cout << "[+] All TitanBtrfs unit tests passed successfully (100% Green)\n";
    std::cout << "========================================================\n";
    return 0;
}
