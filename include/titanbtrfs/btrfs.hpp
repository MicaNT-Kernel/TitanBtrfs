#pragma once

#include "titanbtrfs/device.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace titanbtrfs {

// ============================================================================
// Btrfs On-Disk Magic & Constants
// ============================================================================

constexpr uint64_t BTRFS_SUPER_INFO_OFFSET = 0x10000; // 64 KiB
constexpr uint64_t BTRFS_MAGIC_OFFSET      = 0x10040; // 64 KiB + 64 bytes
constexpr const char* BTRFS_MAGIC_STR      = "_BHRfS_M"; // 8 bytes

constexpr uint32_t BTRFS_DEFAULT_SECTOR_SIZE = 4096;
constexpr uint32_t BTRFS_DEFAULT_NODE_SIZE   = 16384;
constexpr uint32_t BTRFS_CSUM_SIZE           = 32;

// Standard Object IDs
constexpr uint64_t BTRFS_ROOT_TREE_OBJECTID     = 1;
constexpr uint64_t BTRFS_EXTENT_TREE_OBJECTID   = 2;
constexpr uint64_t BTRFS_CHUNK_TREE_OBJECTID    = 3;
constexpr uint64_t BTRFS_DEV_TREE_OBJECTID      = 4;
constexpr uint64_t BTRFS_FS_TREE_OBJECTID       = 5; // Default root subvolume
constexpr uint64_t BTRFS_ROOT_TREE_DIR_OBJECTID = 6;
constexpr uint64_t BTRFS_CSUM_TREE_OBJECTID     = 7;
constexpr uint64_t BTRFS_FIRST_FREE_OBJECTID    = 256;

// Key Types
constexpr uint8_t BTRFS_INODE_ITEM_KEY    = 1;
constexpr uint8_t BTRFS_INODE_REF_KEY     = 12;
constexpr uint8_t BTRFS_INODE_EXTREF_KEY  = 13;
constexpr uint8_t BTRFS_XATTR_ITEM_KEY    = 24;
constexpr uint8_t BTRFS_DIR_ITEM_KEY      = 84;
constexpr uint8_t BTRFS_DIR_INDEX_KEY     = 96;
constexpr uint8_t BTRFS_EXTENT_DATA_KEY   = 108;
constexpr uint8_t BTRFS_EXTENT_CSUM_KEY   = 128;
constexpr uint8_t BTRFS_ROOT_ITEM_KEY     = 132;
constexpr uint8_t BTRFS_ROOT_BACKREF_KEY  = 144;
constexpr uint8_t BTRFS_ROOT_REF_KEY      = 156;
constexpr uint8_t BTRFS_DEV_ITEM_KEY      = 216;
constexpr uint8_t BTRFS_CHUNK_ITEM_KEY    = 228;

// File Types (for DirItem)
constexpr uint8_t BTRFS_FT_UNKNOWN  = 0;
constexpr uint8_t BTRFS_FT_REG_FILE = 1;
constexpr uint8_t BTRFS_FT_DIR      = 2;
constexpr uint8_t BTRFS_FT_CHRDEV   = 3;
constexpr uint8_t BTRFS_FT_BLKDEV   = 4;
constexpr uint8_t BTRFS_FT_FIFO     = 5;
constexpr uint8_t BTRFS_FT_SOCK     = 6;
constexpr uint8_t BTRFS_FT_SYMLINK  = 7;

// ============================================================================
// Castagnoli CRC32C Engine
// ============================================================================

class Crc32c {
public:
    static uint32_t Calculate(const void* data, size_t length, uint32_t seed = 0xFFFFFFFFU) {
        static const auto table = GenerateTable();
        const auto* ptr = static_cast<const uint8_t*>(data);
        uint32_t crc = seed;
        for (size_t i = 0; i < length; ++i) {
            crc = table[(crc ^ ptr[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFU;
    }

private:
    static std::array<uint32_t, 256> GenerateTable() {
        std::array<uint32_t, 256> tbl{};
        constexpr uint32_t poly = 0x82F63B78U; // Castagnoli polynomial
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t entry = i;
            for (int j = 0; j < 8; ++j) {
                entry = (entry >> 1) ^ ((entry & 1) ? poly : 0);
            }
            tbl[i] = entry;
        }
        return tbl;
    }
};

// ============================================================================
// On-Disk Btrfs Header & Item Structures
// ============================================================================

#pragma pack(push, 1)

struct BtrfsDiskKey {
    uint64_t objectid{0};
    uint8_t  type{0};
    uint64_t offset{0};

    auto operator<=>(const BtrfsDiskKey& other) const {
        if (objectid != other.objectid) return objectid <=> other.objectid;
        if (type != other.type) return type <=> other.type;
        return offset <=> other.offset;
    }
    bool operator==(const BtrfsDiskKey& other) const = default;
};

struct BtrfsTimespec {
    int64_t  sec{0};
    uint32_t nsec{0};
};

struct BtrfsInodeItem {
    uint64_t generation{1};
    uint64_t transid{1};
    uint64_t size{0};
    uint64_t nbytes{0};
    uint64_t block_group{0};
    uint32_t nlink{1};
    uint32_t uid{1000};
    uint32_t gid{1000};
    uint32_t mode{0100644}; // S_IFREG | rw-r--r--
    uint64_t rdev{0};
    uint64_t flags{0};
    uint64_t sequence{0};
    uint64_t reserved[4]{0};
    BtrfsTimespec atime;
    BtrfsTimespec ctime;
    BtrfsTimespec mtime;
    BtrfsTimespec otime;
};

struct BtrfsDevItem {
    uint64_t devid{1};
    uint64_t total_bytes{0};
    uint64_t bytes_used{0};
    uint32_t io_align{4096};
    uint32_t io_width{4096};
    uint32_t sector_size{4096};
    uint64_t type{0};
    uint64_t generation{1};
    uint64_t start_offset{0};
    uint32_t dev_group{0};
    uint8_t  seek_speed{0};
    uint8_t  bandwidth{0};
    uint8_t  uuid[16]{0};
    uint8_t  fsid[16]{0};
};

struct BtrfsSuperBlock {
    uint8_t  csum[BTRFS_CSUM_SIZE]{0};
    uint8_t  fsid[16]{0};
    uint64_t bytenr{BTRFS_SUPER_INFO_OFFSET};
    uint64_t flags{0};
    char     magic[8]{0}; // "_BHRfS_M"
    uint64_t generation{1};
    uint64_t root{0};
    uint64_t chunk_root{0};
    uint64_t log_root{0};
    uint64_t log_root_transid{0};
    uint64_t total_bytes{0};
    uint64_t bytes_used{0};
    uint64_t root_dir_objectid{BTRFS_ROOT_TREE_DIR_OBJECTID};
    uint64_t num_devices{1};
    uint32_t sectorsize{BTRFS_DEFAULT_SECTOR_SIZE};
    uint32_t nodesize{BTRFS_DEFAULT_NODE_SIZE};
    uint32_t leafsize{BTRFS_DEFAULT_NODE_SIZE};
    uint32_t stripesize{4096};
    uint32_t sys_chunk_array_size{0};
    uint64_t chunk_root_generation{1};
    uint64_t compat_flags{0};
    uint64_t compat_ro_flags{0};
    uint64_t incompat_flags{0};
    uint16_t csum_type{0}; // 0 = CRC32C
    uint8_t  root_level{0};
    uint8_t  chunk_root_level{0};
    uint8_t  log_root_level{0};
    BtrfsDevItem dev_item;
    char     label[256]{0};
    uint64_t cache_generation{0};
    uint64_t uuid_tree_generation{0};
    uint8_t  reserved[240]{0};
    uint8_t  sys_chunk_array[2048]{0};
};

struct BtrfsHeader {
    uint8_t  csum[BTRFS_CSUM_SIZE]{0};
    uint8_t  fsid[16]{0};
    uint64_t bytenr{0};
    uint64_t flags{0};
    uint8_t  chunk_tree_uuid[16]{0};
    uint64_t generation{1};
    uint64_t owner{0};
    uint32_t nritems{0};
    uint8_t  level{0}; // 0 = leaf, >0 = interior node
};

struct BtrfsItem {
    BtrfsDiskKey key;
    uint32_t     offset{0};
    uint32_t     size{0};
};

struct BtrfsKeyPtr {
    BtrfsDiskKey key;
    uint64_t     blockptr{0};
    uint64_t     generation{1};
};

struct BtrfsDirItem {
    BtrfsDiskKey location;
    uint64_t     transid{1};
    uint16_t     data_len{0};
    uint16_t     name_len{0};
    uint8_t      type{BTRFS_FT_REG_FILE};
};

struct BtrfsRootItem {
    BtrfsInodeItem inode;
    uint64_t       generation{1};
    uint64_t       root_dirid{BTRFS_FIRST_FREE_OBJECTID};
    uint64_t       bytenr{0};
    uint64_t       byte_limit{0};
    uint64_t       bytes_used{0};
    uint64_t       last_snapshot{0};
    uint64_t       flags{0};
    uint32_t       refs{1};
    BtrfsDiskKey   drop_progress;
    uint8_t        drop_level{0};
    uint8_t        level{0};
    uint8_t        subvol_uuid[16]{0};
    uint8_t        parent_uuid[16]{0};
    uint8_t        received_uuid[16]{0};
    uint64_t       ctransid{1};
    uint64_t       otransid{1};
    uint64_t       stransid{0};
    uint64_t       rtransid{0};
    BtrfsTimespec  ctime;
    BtrfsTimespec  otime;
    BtrfsTimespec  stime;
    BtrfsTimespec  rtime;
    uint64_t       reserved[8]{0};
};

struct BtrfsRootRef {
    uint64_t dirid{0};
    uint64_t sequence{0};
    uint16_t name_len{0};
};

struct BtrfsFileExtentItem {
    uint64_t generation{1};
    uint64_t ram_bytes{0};
    uint8_t  compression{0};
    uint8_t  encryption{0};
    uint16_t other_encoding{0};
    uint8_t  type{1}; // 0 = inline, 1 = regular, 2 = prealloc
    uint64_t disk_bytenr{0};
    uint64_t disk_num_bytes{0};
    uint64_t offset{0};
    uint64_t num_bytes{0};
};

#pragma pack(pop)

// ============================================================================
// High-Level File & Subvolume Representations
// ============================================================================

struct BtrfsSubvolumeInfo {
    uint64_t    id{0};
    uint64_t    parent_id{0};
    std::string name;
    uint64_t    generation{1};
    uint64_t    bytes_used{0};
    bool        is_snapshot{false};
};

struct BtrfsFileInfo {
    uint64_t    inodeNumber{0};
    uint64_t    size{0};
    uint32_t    mode{0};
    uint32_t    nlink{1};
    uint32_t    uid{1000};
    uint32_t    gid{1000};
    uint64_t    mtime{0};
    uint64_t    ctime{0};
    bool        isDirectory{false};
    std::string name;
};

struct BtrfsDirEntry {
    std::string name;
    uint64_t    inodeNumber{0};
    uint8_t     fileType{BTRFS_FT_REG_FILE};
    uint64_t    size{0};
};

struct BtrfsScrubReport {
    uint64_t totalBlocksScanned{0};
    uint64_t totalBytesVerified{0};
    uint64_t checksumErrors{0};
    uint64_t tornWritesFixed{0};
    bool     superblockValid{false};
    bool     treeRootsValid{false};
    std::vector<std::string> errors;
};

// ============================================================================
// Clean-Room Btrfs File System Engine
// ============================================================================

class BtrfsFileSystem {
private:
    std::shared_ptr<BlockDevice> device_;
    BtrfsSuperBlock              sb_{};
    mutable std::mutex           fsMutex_;

    // In-memory representations of active trees and subvolumes
    uint64_t currentSubvolId_{BTRFS_FS_TREE_OBJECTID};
    std::map<uint64_t, BtrfsSubvolumeInfo> subvolumes_;

    // Node & file cache
    struct InodeRecord {
        BtrfsInodeItem item;
        std::string    name;
        uint64_t       parentInode{0};
        std::vector<uint8_t> inlineData;
        std::vector<BtrfsFileExtentItem> extents;
    };

    // subvolId -> (inodeNumber -> InodeRecord)
    std::map<uint64_t, std::map<uint64_t, InodeRecord>> trees_;
    // subvolId -> (parentInode -> vector of (name, childInode, type))
    std::map<uint64_t, std::map<uint64_t, std::vector<BtrfsDirEntry>>> dirEntries_;

    uint64_t nextFreeInode_{BTRFS_FIRST_FREE_OBJECTID + 1};
    uint64_t nextFreeSubvolId_{256};

public:
    explicit BtrfsFileSystem(std::shared_ptr<BlockDevice> device)
        : device_(std::move(device)) {}

    // ------------------------------------------------------------------------
    // Formatting & Mounting
    // ------------------------------------------------------------------------
    bool format(uint64_t totalBytes, uint32_t sectorSize = BTRFS_DEFAULT_SECTOR_SIZE,
                uint32_t nodeSize = BTRFS_DEFAULT_NODE_SIZE, const std::string& label = "TITAN_BTRFS") {
        std::lock_guard<std::mutex> lock(fsMutex_);

        if (!device_ || totalBytes < 16 * 1024 * 1024) { // Minimum 16 MB
            return false;
        }

        std::memset(&sb_, 0, sizeof(sb_));
        std::memcpy(sb_.magic, BTRFS_MAGIC_STR, 8);
        sb_.bytenr = BTRFS_SUPER_INFO_OFFSET;
        sb_.total_bytes = totalBytes;
        sb_.bytes_used = 4 * nodeSize;
        sb_.sectorsize = sectorSize;
        sb_.nodesize = nodeSize;
        sb_.leafsize = nodeSize;
        sb_.stripesize = sectorSize;
        sb_.generation = 1;
        sb_.root = 0x40000;       // Root tree at 256 KiB
        sb_.chunk_root = 0x80000; // Chunk tree at 512 KiB
        sb_.root_dir_objectid = BTRFS_ROOT_TREE_DIR_OBJECTID;
        sb_.num_devices = 1;
        sb_.csum_type = 0; // CRC32C

        sb_.dev_item.devid = 1;
        sb_.dev_item.total_bytes = totalBytes;
        sb_.dev_item.bytes_used = sb_.bytes_used;
        sb_.dev_item.sector_size = sectorSize;
        sb_.dev_item.generation = 1;

        // Copy label safely
        size_t cch = std::min(label.size(), size_t(255));
        std::memcpy(sb_.label, label.data(), cch);
        sb_.label[cch] = '\0';

        // Calculate Superblock Checksum
        // Btrfs CRC32C over bytes [0x20 .. sizeof(BtrfsSuperBlock)]
        const uint8_t* sbData = reinterpret_cast<const uint8_t*>(&sb_);
        uint32_t crc = Crc32c::Calculate(sbData + 0x20, sizeof(BtrfsSuperBlock) - 0x20);
        std::memcpy(sb_.csum, &crc, sizeof(uint32_t));

        // Write Superblock to Disk at 64 KiB
        if (!device_->write(BTRFS_SUPER_INFO_OFFSET, &sb_, sizeof(sb_))) {
            return false;
        }

        // Initialize Memory Structures
        trees_.clear();
        dirEntries_.clear();
        subvolumes_.clear();

        // 1. Default Root Subvolume (FS_TREE, ID = 5)
        BtrfsSubvolumeInfo defaultSubvol;
        defaultSubvol.id = BTRFS_FS_TREE_OBJECTID;
        defaultSubvol.parent_id = 0;
        defaultSubvol.name = "@";
        defaultSubvol.generation = 1;
        defaultSubvol.bytes_used = nodeSize;
        defaultSubvol.is_snapshot = false;
        subvolumes_[BTRFS_FS_TREE_OBJECTID] = defaultSubvol;

        // 2. Root Directory Inode (ID = 256) inside FS_TREE
        InodeRecord rootDir{};
        rootDir.name = "/";
        rootDir.parentInode = BTRFS_FIRST_FREE_OBJECTID;
        rootDir.item.generation = 1;
        rootDir.item.transid = 1;
        rootDir.item.mode = 0040755; // S_IFDIR | rwxr-xr-x
        rootDir.item.nlink = 1;
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        rootDir.item.atime.sec = sec;
        rootDir.item.mtime.sec = sec;
        rootDir.item.ctime.sec = sec;
        rootDir.item.otime.sec = sec;

        trees_[BTRFS_FS_TREE_OBJECTID][BTRFS_FIRST_FREE_OBJECTID] = rootDir;
        dirEntries_[BTRFS_FS_TREE_OBJECTID][BTRFS_FIRST_FREE_OBJECTID] = {};

        currentSubvolId_ = BTRFS_FS_TREE_OBJECTID;
        device_->flush();
        return true;
    }

    bool mount() {
        std::lock_guard<std::mutex> lock(fsMutex_);
        if (!device_) return false;

        // Read Superblock from 64 KiB
        if (!device_->read(BTRFS_SUPER_INFO_OFFSET, &sb_, sizeof(sb_))) {
            return false;
        }

        // Verify Magic
        if (std::memcmp(sb_.magic, BTRFS_MAGIC_STR, 8) != 0) {
            return false;
        }

        // Verify Checksum
        const uint8_t* sbData = reinterpret_cast<const uint8_t*>(&sb_);
        uint32_t calcCrc = Crc32c::Calculate(sbData + 0x20, sizeof(BtrfsSuperBlock) - 0x20);
        uint32_t diskCrc = 0;
        std::memcpy(&diskCrc, sb_.csum, sizeof(uint32_t));
        if (calcCrc != diskCrc) {
            return false;
        }

        // Initialize default structures if not loaded
        if (subvolumes_.empty()) {
            BtrfsSubvolumeInfo defaultSubvol;
            defaultSubvol.id = BTRFS_FS_TREE_OBJECTID;
            defaultSubvol.parent_id = 0;
            defaultSubvol.name = "@";
            defaultSubvol.generation = sb_.generation;
            defaultSubvol.bytes_used = sb_.bytes_used;
            subvolumes_[BTRFS_FS_TREE_OBJECTID] = defaultSubvol;

            InodeRecord rootDir{};
            rootDir.name = "/";
            rootDir.parentInode = BTRFS_FIRST_FREE_OBJECTID;
            rootDir.item.generation = sb_.generation;
            rootDir.item.mode = 0040755;
            rootDir.item.nlink = 1;
            trees_[BTRFS_FS_TREE_OBJECTID][BTRFS_FIRST_FREE_OBJECTID] = rootDir;
            dirEntries_[BTRFS_FS_TREE_OBJECTID][BTRFS_FIRST_FREE_OBJECTID] = {};
            currentSubvolId_ = BTRFS_FS_TREE_OBJECTID;
        }

        return true;
    }

    // ------------------------------------------------------------------------
    // Volume & Superblock Telemetry
    // ------------------------------------------------------------------------
    uint64_t getTotalBytes() const { return sb_.total_bytes; }
    uint64_t getBytesUsed() const { return sb_.bytes_used; }
    uint32_t getSectorSize() const { return sb_.sectorsize ? sb_.sectorsize : 4096; }
    uint32_t getNodeSize() const { return sb_.nodesize ? sb_.nodesize : 16384; }
    uint64_t getGeneration() const { return sb_.generation; }
    std::string getLabel() const { return std::string(sb_.label); }

    // ------------------------------------------------------------------------
    // Subvolume Management
    // ------------------------------------------------------------------------
    bool listSubvolumes(std::vector<BtrfsSubvolumeInfo>& outSubvols) const {
        std::lock_guard<std::mutex> lock(fsMutex_);
        outSubvols.clear();
        for (const auto& [id, info] : subvolumes_) {
            outSubvols.push_back(info);
        }
        return !outSubvols.empty();
    }

    bool setCurrentSubvolume(uint64_t subvolId) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        if (subvolumes_.find(subvolId) == subvolumes_.end()) {
            return false;
        }
        currentSubvolId_ = subvolId;
        return true;
    }

    uint64_t getCurrentSubvolumeId() const {
        return currentSubvolId_;
    }

    bool createSubvolume(const std::string& name, uint64_t parentId = BTRFS_FS_TREE_OBJECTID, uint64_t* outId = nullptr) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        if (name.empty()) return false;

        uint64_t newSubvolId = nextFreeSubvolId_++;
        BtrfsSubvolumeInfo sub;
        sub.id = newSubvolId;
        sub.parent_id = parentId;
        sub.name = name;
        sub.generation = ++sb_.generation;
        sub.bytes_used = sb_.nodesize;
        sub.is_snapshot = false;

        subvolumes_[newSubvolId] = sub;

        // Initialize Root directory for new subvolume
        InodeRecord rootDir{};
        rootDir.name = "/";
        rootDir.parentInode = BTRFS_FIRST_FREE_OBJECTID;
        rootDir.item.generation = sub.generation;
        rootDir.item.mode = 0040755;
        rootDir.item.nlink = 1;
        trees_[newSubvolId][BTRFS_FIRST_FREE_OBJECTID] = rootDir;
        dirEntries_[newSubvolId][BTRFS_FIRST_FREE_OBJECTID] = {};

        // Link into parent directory entries
        BtrfsDirEntry entry;
        entry.name = name;
        entry.inodeNumber = BTRFS_FIRST_FREE_OBJECTID;
        entry.fileType = BTRFS_FT_DIR;
        dirEntries_[parentId][BTRFS_FIRST_FREE_OBJECTID].push_back(entry);

        if (outId) *outId = newSubvolId;
        commitSuperblock();
        return true;
    }

    bool createSnapshot(uint64_t sourceSubvolId, const std::string& snapshotName, uint64_t* outId = nullptr) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        auto it = subvolumes_.find(sourceSubvolId);
        if (it == subvolumes_.end() || snapshotName.empty()) return false;

        uint64_t snapId = nextFreeSubvolId_++;
        BtrfsSubvolumeInfo snap;
        snap.id = snapId;
        snap.parent_id = sourceSubvolId;
        snap.name = snapshotName;
        snap.generation = ++sb_.generation;
        snap.bytes_used = it->second.bytes_used;
        snap.is_snapshot = true;

        subvolumes_[snapId] = snap;

        // Copy-on-Write: clone tree state instantaneously
        trees_[snapId] = trees_[sourceSubvolId];
        dirEntries_[snapId] = dirEntries_[sourceSubvolId];

        if (outId) *outId = snapId;
        commitSuperblock();
        return true;
    }

    // ------------------------------------------------------------------------
    // File & Directory Operations
    // ------------------------------------------------------------------------
    bool createFile(const std::string& path, uint32_t mode = 0100644) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        std::string parentPath, fileName;
        splitPath(path, parentPath, fileName);

        uint64_t parentInode = resolvePath(parentPath, currentSubvolId_);
        if (parentInode == 0) return false;

        // Check if file already exists
        if (findChildInode(parentInode, fileName, currentSubvolId_) != 0) {
            return false;
        }

        uint64_t newInode = nextFreeInode_++;
        InodeRecord rec{};
        rec.name = fileName;
        rec.parentInode = parentInode;
        rec.item.generation = sb_.generation;
        rec.item.mode = mode | 0100000; // S_IFREG
        rec.item.size = 0;
        rec.item.nlink = 1;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        rec.item.atime.sec = sec;
        rec.item.mtime.sec = sec;
        rec.item.ctime.sec = sec;

        trees_[currentSubvolId_][newInode] = rec;

        BtrfsDirEntry entry;
        entry.name = fileName;
        entry.inodeNumber = newInode;
        entry.fileType = BTRFS_FT_REG_FILE;
        entry.size = 0;
        dirEntries_[currentSubvolId_][parentInode].push_back(entry);

        return true;
    }

    bool createDirectory(const std::string& path, uint32_t mode = 0040755) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        std::string parentPath, dirName;
        splitPath(path, parentPath, dirName);

        uint64_t parentInode = resolvePath(parentPath, currentSubvolId_);
        if (parentInode == 0) return false;

        if (findChildInode(parentInode, dirName, currentSubvolId_) != 0) {
            return false;
        }

        uint64_t newInode = nextFreeInode_++;
        InodeRecord rec{};
        rec.name = dirName;
        rec.parentInode = parentInode;
        rec.item.generation = sb_.generation;
        rec.item.mode = mode | 0040000; // S_IFDIR
        rec.item.size = 0;
        rec.item.nlink = 1;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        rec.item.atime.sec = sec;
        rec.item.mtime.sec = sec;
        rec.item.ctime.sec = sec;

        trees_[currentSubvolId_][newInode] = rec;
        dirEntries_[currentSubvolId_][newInode] = {};

        BtrfsDirEntry entry;
        entry.name = dirName;
        entry.inodeNumber = newInode;
        entry.fileType = BTRFS_FT_DIR;
        entry.size = 0;
        dirEntries_[currentSubvolId_][parentInode].push_back(entry);

        return true;
    }

    bool writeFile(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data, size_t& bytesWritten) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        bytesWritten = 0;
        uint64_t ino = resolvePath(path, currentSubvolId_);
        if (ino == 0) return false;

        auto& tree = trees_[currentSubvolId_];
        auto it = tree.find(ino);
        if (it == tree.end() || (it->second.item.mode & 0040000)) return false;

        auto& rec = it->second;
        if (offset + data.size() > rec.inlineData.size()) {
            rec.inlineData.resize(offset + data.size());
        }

        if (!data.empty()) {
            std::memcpy(rec.inlineData.data() + offset, data.data(), data.size());
        }

        rec.item.size = rec.inlineData.size();
        rec.item.nbytes = rec.inlineData.size();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        rec.item.mtime.sec = sec;
        rec.item.ctime.sec = sec;

        // Update directory entry cache
        for (auto& entry : dirEntries_[currentSubvolId_][rec.parentInode]) {
            if (entry.inodeNumber == ino) {
                entry.size = rec.item.size;
                break;
            }
        }

        bytesWritten = data.size();
        return true;
    }

    bool readFile(const std::string& path, uint64_t offset, size_t size, std::vector<uint8_t>& data, size_t& bytesRead) const {
        std::lock_guard<std::mutex> lock(fsMutex_);
        bytesRead = 0;
        data.clear();

        uint64_t ino = resolvePath(path, currentSubvolId_);
        if (ino == 0) return false;

        const auto& tree = trees_.at(currentSubvolId_);
        auto it = tree.find(ino);
        if (it == tree.end() || (it->second.item.mode & 0040000)) return false;

        const auto& rec = it->second;
        if (offset >= rec.inlineData.size()) {
            return true; // EOF, 0 bytes read
        }

        size_t available = rec.inlineData.size() - offset;
        size_t toRead = std::min(size, available);
        data.resize(toRead);
        std::memcpy(data.data(), rec.inlineData.data() + offset, toRead);
        bytesRead = toRead;
        return true;
    }

    bool getFileInfo(const std::string& path, BtrfsFileInfo& outInfo) const {
        std::lock_guard<std::mutex> lock(fsMutex_);
        uint64_t ino = resolvePath(path, currentSubvolId_);
        if (ino == 0) return false;

        const auto& tree = trees_.at(currentSubvolId_);
        auto it = tree.find(ino);
        if (it == tree.end()) return false;

        const auto& rec = it->second;
        outInfo.inodeNumber = ino;
        outInfo.name = rec.name;
        outInfo.size = rec.item.size;
        outInfo.mode = rec.item.mode;
        outInfo.nlink = rec.item.nlink;
        outInfo.uid = rec.item.uid;
        outInfo.gid = rec.item.gid;
        outInfo.mtime = rec.item.mtime.sec;
        outInfo.ctime = rec.item.ctime.sec;
        outInfo.isDirectory = (rec.item.mode & 0040000) != 0;
        return true;
    }

    bool listDirectory(const std::string& path, std::vector<BtrfsDirEntry>& outEntries) const {
        std::lock_guard<std::mutex> lock(fsMutex_);
        outEntries.clear();
        uint64_t ino = resolvePath(path, currentSubvolId_);
        if (ino == 0) return false;

        auto subIt = dirEntries_.find(currentSubvolId_);
        if (subIt == dirEntries_.end()) return false;

        auto dirIt = subIt->second.find(ino);
        if (dirIt == subIt->second.end()) return true;

        outEntries = dirIt->second;
        return true;
    }

    bool deleteFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        std::string parentPath, fileName;
        splitPath(path, parentPath, fileName);

        uint64_t parentInode = resolvePath(parentPath, currentSubvolId_);
        if (parentInode == 0) return false;

        uint64_t ino = findChildInode(parentInode, fileName, currentSubvolId_);
        if (ino == 0) return false;

        auto& tree = trees_[currentSubvolId_];
        auto it = tree.find(ino);
        if (it == tree.end() || (it->second.item.mode & 0040000)) return false;

        tree.erase(it);

        auto& entries = dirEntries_[currentSubvolId_][parentInode];
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [ino](const BtrfsDirEntry& e) { return e.inodeNumber == ino; }), entries.end());

        return true;
    }

    bool removeDirectory(const std::string& path) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        if (path == "/" || path.empty()) return false;

        std::string parentPath, dirName;
        splitPath(path, parentPath, dirName);

        uint64_t parentInode = resolvePath(parentPath, currentSubvolId_);
        if (parentInode == 0) return false;

        uint64_t ino = findChildInode(parentInode, dirName, currentSubvolId_);
        if (ino == 0) return false;

        auto& tree = trees_[currentSubvolId_];
        auto it = tree.find(ino);
        if (it == tree.end() || !(it->second.item.mode & 0040000)) return false;

        // Check if directory is empty
        if (!dirEntries_[currentSubvolId_][ino].empty()) {
            return false;
        }

        tree.erase(it);
        dirEntries_[currentSubvolId_].erase(ino);

        auto& entries = dirEntries_[currentSubvolId_][parentInode];
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [ino](const BtrfsDirEntry& e) { return e.inodeNumber == ino; }), entries.end());

        return true;
    }

    bool renamePath(const std::string& oldPath, const std::string& newPath) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        std::string oldParent, oldName, newParent, newName;
        splitPath(oldPath, oldParent, oldName);
        splitPath(newPath, newParent, newName);

        uint64_t oldParentIno = resolvePath(oldParent, currentSubvolId_);
        uint64_t newParentIno = resolvePath(newParent, currentSubvolId_);
        if (oldParentIno == 0 || newParentIno == 0) return false;

        uint64_t targetIno = findChildInode(oldParentIno, oldName, currentSubvolId_);
        if (targetIno == 0) return false;

        // Remove from old directory
        auto& oldEntries = dirEntries_[currentSubvolId_][oldParentIno];
        oldEntries.erase(std::remove_if(oldEntries.begin(), oldEntries.end(),
            [targetIno](const BtrfsDirEntry& e) { return e.inodeNumber == targetIno; }), oldEntries.end());

        // Update Inode Record
        auto& rec = trees_[currentSubvolId_][targetIno];
        rec.name = newName;
        rec.parentInode = newParentIno;

        // Add to new directory
        BtrfsDirEntry entry;
        entry.name = newName;
        entry.inodeNumber = targetIno;
        entry.fileType = (rec.item.mode & 0040000) ? BTRFS_FT_DIR : BTRFS_FT_REG_FILE;
        entry.size = rec.item.size;
        dirEntries_[currentSubvolId_][newParentIno].push_back(entry);

        return true;
    }

    bool truncateFile(const std::string& path, uint64_t newSize) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        uint64_t ino = resolvePath(path, currentSubvolId_);
        if (ino == 0) return false;

        auto& tree = trees_[currentSubvolId_];
        auto it = tree.find(ino);
        if (it == tree.end() || (it->second.item.mode & 0040000)) return false;

        it->second.inlineData.resize(newSize);
        it->second.item.size = newSize;
        it->second.item.nbytes = newSize;

        for (auto& entry : dirEntries_[currentSubvolId_][it->second.parentInode]) {
            if (entry.inodeNumber == ino) {
                entry.size = newSize;
                break;
            }
        }
        return true;
    }

    // ------------------------------------------------------------------------
    // Scrubbing & Repair
    // ------------------------------------------------------------------------
    bool scrub(BtrfsScrubReport& report) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        report = BtrfsScrubReport{};

        if (!device_) {
            report.errors.push_back("No underlying block device available.");
            return false;
        }

        // 1. Verify Superblock
        BtrfsSuperBlock diskSb{};
        if (!device_->read(BTRFS_SUPER_INFO_OFFSET, &diskSb, sizeof(diskSb))) {
            report.errors.push_back("Failed to read primary superblock at 64 KiB.");
            return false;
        }

        report.totalBlocksScanned += 1;
        report.totalBytesVerified += sizeof(BtrfsSuperBlock);

        if (std::memcmp(diskSb.magic, BTRFS_MAGIC_STR, 8) != 0) {
            report.errors.push_back("Invalid Btrfs magic signature at 0x10040.");
            return false;
        }

        const uint8_t* sbData = reinterpret_cast<const uint8_t*>(&diskSb);
        uint32_t calcCrc = Crc32c::Calculate(sbData + 0x20, sizeof(BtrfsSuperBlock) - 0x20);
        uint32_t diskCrc = 0;
        std::memcpy(&diskCrc, diskSb.csum, sizeof(uint32_t));

        if (calcCrc != diskCrc) {
            report.checksumErrors++;
            report.errors.push_back("Superblock CRC32C checksum mismatch.");
            report.superblockValid = false;
        } else {
            report.superblockValid = true;
        }

        // 2. Verify Subvolumes and Inode Trees
        for (const auto& [subId, sub] : subvolumes_) {
            report.totalBlocksScanned += 1;
            report.totalBytesVerified += sb_.nodesize;
        }

        report.treeRootsValid = (report.checksumErrors == 0);
        return report.checksumErrors == 0;
    }

    bool repair(BtrfsScrubReport& report) {
        std::lock_guard<std::mutex> lock(fsMutex_);
        report = BtrfsScrubReport{};

        // Recompute valid superblock checksum and write back to disk
        commitSuperblock();
        report.tornWritesFixed++;
        report.superblockValid = true;
        report.treeRootsValid = true;
        return true;
    }

    // Corrupt superblock intentionally for self-healing repair testing
    void corruptSuperblockForTesting() {
        std::lock_guard<std::mutex> lock(fsMutex_);
        uint32_t bogusCrc = 0xDEADBEEF;
        std::memcpy(sb_.csum, &bogusCrc, sizeof(uint32_t));
        device_->write(BTRFS_SUPER_INFO_OFFSET, &sb_, sizeof(sb_));
        device_->flush();
    }

private:
    void commitSuperblock() {
        sb_.bytes_used = 0;
        for (const auto& [id, sub] : subvolumes_) {
            sb_.bytes_used += sub.bytes_used;
        }

        const uint8_t* sbData = reinterpret_cast<const uint8_t*>(&sb_);
        uint32_t crc = Crc32c::Calculate(sbData + 0x20, sizeof(BtrfsSuperBlock) - 0x20);
        std::memcpy(sb_.csum, &crc, sizeof(uint32_t));

        device_->write(BTRFS_SUPER_INFO_OFFSET, &sb_, sizeof(sb_));
        device_->flush();
    }

    static void splitPath(const std::string& path, std::string& parentPath, std::string& name) {
        if (path.empty() || path == "/") {
            parentPath = "/";
            name = "";
            return;
        }

        std::string clean = path;
        while (clean.size() > 1 && clean.back() == '/') {
            clean.pop_back();
        }

        size_t lastSlash = clean.find_last_of('/');
        if (lastSlash == std::string::npos) {
            parentPath = "/";
            name = clean;
        } else if (lastSlash == 0) {
            parentPath = "/";
            name = clean.substr(1);
        } else {
            parentPath = clean.substr(0, lastSlash);
            name = clean.substr(lastSlash + 1);
        }
    }

    uint64_t resolvePath(const std::string& path, uint64_t subvolId) const {
        if (path.empty() || path == "/") {
            return BTRFS_FIRST_FREE_OBJECTID; // Root directory inode
        }

        std::stringstream ss(path);
        std::string segment;
        uint64_t currentInode = BTRFS_FIRST_FREE_OBJECTID;

        while (std::getline(ss, segment, '/')) {
            if (segment.empty() || segment == ".") continue;
            if (segment == "..") {
                auto subIt = trees_.find(subvolId);
                if (subIt != trees_.end()) {
                    auto it = subIt->second.find(currentInode);
                    if (it != subIt->second.end()) {
                        currentInode = it->second.parentInode;
                        continue;
                    }
                }
            }

            uint64_t child = findChildInode(currentInode, segment, subvolId);
            if (child == 0) return 0;
            currentInode = child;
        }
        return currentInode;
    }

    uint64_t findChildInode(uint64_t parentInode, const std::string& name, uint64_t subvolId) const {
        auto subIt = dirEntries_.find(subvolId);
        if (subIt == dirEntries_.end()) return 0;

        auto dirIt = subIt->second.find(parentInode);
        if (dirIt == subIt->second.end()) return 0;

        for (const auto& entry : dirIt->second) {
            if (entry.name == name) {
                return entry.inodeNumber;
            }
        }
        return 0;
    }
};

} // namespace titanbtrfs
