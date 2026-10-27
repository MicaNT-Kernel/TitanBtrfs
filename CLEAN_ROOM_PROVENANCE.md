# TitanBtrfs Clean-Room Provenance & Compliance Declaration

## 1. Executive Summary & Purpose
**TitanBtrfs** is an independent, clean-room implementation of the B-tree File System (Btrfs on-disk architecture). The purpose of this project is to achieve **functional interoperability** with storage volumes, partitions, and disk images formatted according to standard Btrfs layouts, enabling high-performance read/write access, subvolume management, and WinFsp user-mode driver mounting on Windows without kernel instability or licensing entanglements.

---

## 2. Legal Framework & Fair Use Interoperability
This implementation is developed in accordance with established United States and international copyright laws and fair use precedents governing computer program interoperability:
- **17 U.S.C. § 1201(f) (Reverse Engineering & Interoperability Exemption)**: Expressly permits reverse engineering and functional analysis for the sole purpose of enabling interoperability of independently created software.
- **Google LLC v. Oracle America, Inc. (141 S. Ct. 1183, 2021)**: Confirmed that functional declarations, data formats, and API structures essential for software compatibility and interoperability fall squarely within fair use.
- **Sega Enterprises Ltd. v. Accolade, Inc. (977 F.2d 1510, 9th Cir. 1992)** & **Sony Computer Entertainment, Inc. v. Connectix Corp. (203 F.3d 596, 9th Cir. 2000)**: Established the clean-room "Chinese Wall" reverse-engineering standard as a legally valid mechanism to create compatible implementations.

---

## 3. Strict Clean-Room Rules & Zero-Exposure Guarantee
1. **Zero Copyleft GPL Contamination**:
   - The codebase does not copy, borrow, or link against any GPL Linux kernel or btrfs-progs source code. TitanBtrfs is written from first principles under the Apache License 2.0.
2. **Original ISO C++23 Expression**:
   - All source code, B-tree search algorithms, Castagnoli CRC32C checksum verifiers, and extent allocation routines are 100% original creative expressions implemented in modern C++23.
3. **Public Technical References**:
   - Structural alignments, magic numbers (`_BHRfS_M` at offset `0x10040`), key types (`(objectid, type, offset)`), and superblock field definitions are derived solely from publicly published filesystem format specifications and open academic documentation.
4. **Automated Clean-Room Sentinel CI Enforcement**:
   - Every commit and pull request is scrutinized by an automated Clean-Room Sentinel (`scripts/clean_room_sentinel.js`) to block any non-compliant constructs or copyleft markers from ever entering the repository.
