#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <mutex>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace titanbtrfs {

class BlockDevice {
public:
    virtual ~BlockDevice() = default;
    virtual bool read(uint64_t offset, void* buffer, size_t size) = 0;
    virtual bool write(uint64_t offset, const void* buffer, size_t size) = 0;
    virtual bool flush() = 0;
    virtual uint64_t size() const = 0;
    virtual uint32_t sectorSize() const = 0;
    virtual const std::string& path() const = 0;
};

class FileBlockDevice : public BlockDevice {
private:
    std::string path_;
    uint64_t size_{0};
    uint32_t sectorSize_{4096};
    mutable std::mutex ioMutex_;
#ifdef _WIN32
    HANDLE fileHandle_{INVALID_HANDLE_VALUE};
#else
    std::fstream stream_;
#endif

public:
    FileBlockDevice(const std::string& path, uint64_t initialSize = 0, uint32_t sectorSize = 4096)
        : path_(path), sectorSize_(sectorSize) {
        openOrCreate(initialSize);
    }

    ~FileBlockDevice() override {
        close();
    }

    void openOrCreate(uint64_t initialSize) {
        std::lock_guard<std::mutex> lock(ioMutex_);
#ifdef _WIN32
        DWORD access = GENERIC_READ | GENERIC_WRITE;
        DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD disposition = OPEN_ALWAYS;
        DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS;

        fileHandle_ = CreateFileA(
            path_.c_str(),
            access,
            share,
            nullptr,
            disposition,
            flags,
            nullptr
        );

        if (fileHandle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open or create block device file: " + path_);
        }

        LARGE_INTEGER liSize;
        if (GetFileSizeEx(fileHandle_, &liSize)) {
            size_ = static_cast<uint64_t>(liSize.QuadPart);
        }

        if (size_ < initialSize && initialSize > 0) {
            LARGE_INTEGER liNewSize;
            liNewSize.QuadPart = static_cast<LONGLONG>(initialSize);
            if (SetFilePointerEx(fileHandle_, liNewSize, nullptr, FILE_BEGIN) &&
                SetEndOfFile(fileHandle_)) {
                size_ = initialSize;
            }
        }
#else
        stream_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        if (!stream_.is_open()) {
            stream_.clear();
            stream_.open(path_, std::ios::out | std::ios::binary);
            stream_.close();
            stream_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
        }

        if (!stream_.is_open()) {
            throw std::runtime_error("Failed to open or create block device file: " + path_);
        }

        stream_.seekg(0, std::ios::end);
        size_ = static_cast<uint64_t>(stream_.tellg());

        if (size_ < initialSize && initialSize > 0) {
            stream_.seekp(initialSize - 1);
            stream_.put('\0');
            stream_.flush();
            size_ = initialSize;
        }
#endif
    }

    void close() {
        std::lock_guard<std::mutex> lock(ioMutex_);
#ifdef _WIN32
        if (fileHandle_ != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(fileHandle_);
            CloseHandle(fileHandle_);
            fileHandle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (stream_.is_open()) {
            stream_.flush();
            stream_.close();
        }
#endif
    }

    bool read(uint64_t offset, void* buffer, size_t size) override {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (offset + size > size_) {
            return false;
        }

#ifdef _WIN32
        LARGE_INTEGER liOffset;
        liOffset.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(fileHandle_, liOffset, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(fileHandle_, buffer, static_cast<DWORD>(size), &bytesRead, nullptr)) {
            return false;
        }
        return bytesRead == size;
#else
        stream_.seekg(static_cast<std::streamoff>(offset));
        stream_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
        return stream_.gcount() == static_cast<std::streamsize>(size);
#endif
    }

    bool write(uint64_t offset, const void* buffer, size_t size) override {
        std::lock_guard<std::mutex> lock(ioMutex_);
#ifdef _WIN32
        LARGE_INTEGER liOffset;
        liOffset.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(fileHandle_, liOffset, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(fileHandle_, buffer, static_cast<DWORD>(size), &bytesWritten, nullptr)) {
            return false;
        }

        if (offset + size > size_) {
            size_ = offset + size;
        }
        return bytesWritten == size;
#else
        stream_.seekp(static_cast<std::streamoff>(offset));
        stream_.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(size));
        if (stream_.bad() || stream_.fail()) {
            return false;
        }

        if (offset + size > size_) {
            size_ = offset + size;
        }
        return true;
#endif
    }

    bool flush() override {
        std::lock_guard<std::mutex> lock(ioMutex_);
#ifdef _WIN32
        if (fileHandle_ != INVALID_HANDLE_VALUE) {
            return FlushFileBuffers(fileHandle_) != FALSE;
        }
        return false;
#else
        if (stream_.is_open()) {
            stream_.flush();
            return !stream_.bad();
        }
        return false;
#endif
    }

    uint64_t size() const override {
        return size_;
    }

    uint32_t sectorSize() const override {
        return sectorSize_;
    }

    const std::string& path() const override {
        return path_;
    }
};

} // namespace titanbtrfs
