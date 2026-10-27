#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace titanbtrfs {

// ============================================================================
// IPC Commands & Wire Protocol
// ============================================================================

enum IpcCommand : uint32_t {
    IpcCmdPing              = 0,
    IpcCmdVolumeStatus      = 1,
    IpcCmdVolumeMount       = 2,
    IpcCmdVolumeUnmount     = 3,
    IpcCmdScrubStart        = 4,
    IpcCmdScrubStatus       = 5,
    IpcCmdServiceStatus     = 6,
    IpcCmdListMounts        = 7,
    IpcCmdSubvolumeList     = 8,
    IpcCmdSubvolumeCreate   = 9,
    IpcCmdSubvolumeSnapshot = 10
};

#pragma pack(push, 1)
struct IpcHeader {
    uint64_t magic{0x4254524653495031ULL}; // "BTRFSIP1"
    uint32_t command{0};
    uint32_t payloadLength{0};
};

struct IpcResponseHeader {
    uint64_t magic{0x4254524653495031ULL};
    uint32_t statusCode{0};
    uint32_t payloadLength{0};
};
#pragma pack(pop)

using IpcHandler = std::function<int(uint32_t cmd, const std::string& request, std::string& response)>;

// ============================================================================
// IPC Client
// ============================================================================

class IpcClient {
public:
    static bool isServerRunning(uint32_t timeoutMs = 150) {
#ifdef _WIN32
        const wchar_t* pipeName = L"\\\\.\\pipe\\titanbtrfs_ipc";
        if (WaitNamedPipeW(pipeName, timeoutMs)) {
            return true;
        }
        DWORD err = GetLastError();
        return (err == ERROR_SEM_TIMEOUT || err == ERROR_PIPE_BUSY || err == ERROR_ACCESS_DENIED);
#else
        (void)timeoutMs;
        const char* sockPath = "/tmp/titanbtrfs_ipc.sock";
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) return false;
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);
        bool ok = (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
        close(s);
        return ok;
#endif
    }

    static int send(uint32_t cmd, const std::string& request, std::string& response, uint32_t timeoutMs = 5000) {
#ifdef _WIN32
        const wchar_t* pipeName = L"\\\\.\\pipe\\titanbtrfs_ipc";
        if (!WaitNamedPipeW(pipeName, timeoutMs)) {
            if (GetLastError() != ERROR_PIPE_BUSY) return -1;
            if (!WaitNamedPipeW(pipeName, timeoutMs)) return -1;
        }

        HANDLE hPipe = CreateFileW(
            pipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        if (hPipe == INVALID_HANDLE_VALUE) return -2;

        IpcHeader hdr{};
        hdr.magic = 0x4254524653495031ULL;
        hdr.command = cmd;
        hdr.payloadLength = static_cast<uint32_t>(request.size());

        DWORD written = 0;
        if (!WriteFile(hPipe, &hdr, sizeof(hdr), &written, nullptr) || written != sizeof(hdr)) {
            CloseHandle(hPipe);
            return -3;
        }

        if (!request.empty()) {
            if (!WriteFile(hPipe, request.data(), static_cast<DWORD>(request.size()), &written, nullptr) ||
                written != request.size()) {
                CloseHandle(hPipe);
                return -4;
            }
        }

        IpcResponseHeader respHdr{};
        DWORD bytesRead = 0;
        if (!ReadFile(hPipe, &respHdr, sizeof(respHdr), &bytesRead, nullptr) || bytesRead != sizeof(respHdr)) {
            CloseHandle(hPipe);
            return -5;
        }

        if (respHdr.magic != 0x4254524653495031ULL) {
            CloseHandle(hPipe);
            return -6;
        }

        response.clear();
        if (respHdr.payloadLength > 0) {
            response.resize(respHdr.payloadLength);
            DWORD totalRead = 0;
            while (totalRead < respHdr.payloadLength) {
                DWORD chunk = 0;
                if (!ReadFile(hPipe, response.data() + totalRead, respHdr.payloadLength - totalRead, &chunk, nullptr) || chunk == 0) {
                    CloseHandle(hPipe);
                    return -7;
                }
                totalRead += chunk;
            }
        }

        CloseHandle(hPipe);
        return static_cast<int>(respHdr.statusCode);
#else
        const char* sockPath = "/tmp/titanbtrfs_ipc.sock";
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s < 0) return -1;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);

        if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(s);
            return -2;
        }

        IpcHeader hdr{};
        hdr.magic = 0x4254524653495031ULL;
        hdr.command = cmd;
        hdr.payloadLength = static_cast<uint32_t>(request.size());

        if (::write(s, &hdr, sizeof(hdr)) != sizeof(hdr)) {
            close(s);
            return -3;
        }

        if (!request.empty()) {
            if (::write(s, request.data(), request.size()) != static_cast<ssize_t>(request.size())) {
                close(s);
                return -4;
            }
        }

        IpcResponseHeader respHdr{};
        if (::read(s, &respHdr, sizeof(respHdr)) != sizeof(respHdr)) {
            close(s);
            return -5;
        }

        if (respHdr.magic != 0x4254524653495031ULL) {
            close(s);
            return -6;
        }

        response.clear();
        if (respHdr.payloadLength > 0) {
            response.resize(respHdr.payloadLength);
            size_t totalRead = 0;
            while (totalRead < respHdr.payloadLength) {
                ssize_t chunk = ::read(s, response.data() + totalRead, respHdr.payloadLength - totalRead);
                if (chunk <= 0) {
                    close(s);
                    return -7;
                }
                totalRead += chunk;
            }
        }

        close(s);
        return static_cast<int>(respHdr.statusCode);
#endif
    }
};

// ============================================================================
// IPC Server
// ============================================================================

class IpcServer {
private:
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    IpcHandler handler_;

#ifdef _WIN32
    HANDLE hStopEvent_{NULL};
#else
    int listenSock_{-1};
#endif

public:
    IpcServer() = default;
    ~IpcServer() {
        stop();
    }

    bool start(IpcHandler handler) {
        if (running_) return true;
        handler_ = std::move(handler);
        running_ = true;

#ifdef _WIN32
        hStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        serverThread_ = std::thread(&IpcServer::serverLoopWin32, this);
#else
        serverThread_ = std::thread(&IpcServer::serverLoopPosix, this);
#endif
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;

#ifdef _WIN32
        if (hStopEvent_) {
            SetEvent(hStopEvent_);
        }
        // Wake up a blocked ConnectNamedPipe by momentarily opening the pipe client-side
        std::string dummyResp;
        IpcClient::send(IpcCmdPing, "", dummyResp, 50);

        if (serverThread_.joinable()) {
            serverThread_.join();
        }
        if (hStopEvent_) {
            CloseHandle(hStopEvent_);
            hStopEvent_ = NULL;
        }
#else
        if (listenSock_ >= 0) {
            ::close(listenSock_);
            listenSock_ = -1;
            ::unlink("/tmp/titanbtrfs_ipc.sock");
        }
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
#endif
    }

    bool isRunning() const {
        return running_;
    }

private:
#ifdef _WIN32
    void serverLoopWin32() {
        const wchar_t* pipeName = L"\\\\.\\pipe\\titanbtrfs_ipc";

        while (running_) {
            HANDLE hPipe = CreateNamedPipeW(
                pipeName,
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                65536,
                65536,
                5000,
                nullptr
            );

            if (hPipe == INVALID_HANDLE_VALUE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            BOOL connectPending = ConnectNamedPipe(hPipe, &ov);
            if (!connectPending) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE events[2] = { ov.hEvent, hStopEvent_ };
                    DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                    if (waitRes == WAIT_OBJECT_0 + 1 || !running_) {
                        CloseHandle(ov.hEvent);
                        CloseHandle(hPipe);
                        break;
                    }
                } else if (err != ERROR_PIPE_CONNECTED) {
                    CloseHandle(ov.hEvent);
                    CloseHandle(hPipe);
                    continue;
                }
            }

            CloseHandle(ov.hEvent);

            if (!running_) {
                DisconnectNamedPipe(hPipe);
                CloseHandle(hPipe);
                break;
            }

            // Read Request Header
            IpcHeader hdr{};
            DWORD bytesRead = 0;
            if (ReadFile(hPipe, &hdr, sizeof(hdr), &bytesRead, nullptr) && bytesRead == sizeof(hdr) &&
                hdr.magic == 0x4254524653495031ULL) {

                std::string request;
                if (hdr.payloadLength > 0) {
                    request.resize(hdr.payloadLength);
                    DWORD total = 0;
                    while (total < hdr.payloadLength) {
                        DWORD chunk = 0;
                        if (!ReadFile(hPipe, request.data() + total, hdr.payloadLength - total, &chunk, nullptr) || chunk == 0) {
                            break;
                        }
                        total += chunk;
                    }
                }

                std::string response;
                int status = 0;
                if (handler_) {
                    status = handler_(hdr.command, request, response);
                }

                IpcResponseHeader respHdr{};
                respHdr.magic = 0x4254524653495031ULL;
                respHdr.statusCode = static_cast<uint32_t>(status);
                respHdr.payloadLength = static_cast<uint32_t>(response.size());

                DWORD written = 0;
                WriteFile(hPipe, &respHdr, sizeof(respHdr), &written, nullptr);
                if (!response.empty()) {
                    WriteFile(hPipe, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
                }
                FlushFileBuffers(hPipe);
            }

            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
    }
#else
    void serverLoopPosix() {
        const char* sockPath = "/tmp/titanbtrfs_ipc.sock";
        ::unlink(sockPath);

        listenSock_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenSock_ < 0) return;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);

        if (bind(listenSock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listenSock_);
            listenSock_ = -1;
            return;
        }

        if (listen(listenSock_, 10) < 0) {
            ::close(listenSock_);
            listenSock_ = -1;
            return;
        }

        while (running_) {
            struct pollfd pfd{};
            pfd.fd = listenSock_;
            pfd.events = POLLIN;

            int pollRes = poll(&pfd, 1, 500);
            if (pollRes <= 0 || !(pfd.revents & POLLIN)) {
                continue;
            }

            int clientSock = accept(listenSock_, nullptr, nullptr);
            if (clientSock < 0) continue;

            IpcHeader hdr{};
            if (::read(clientSock, &hdr, sizeof(hdr)) == sizeof(hdr) &&
                hdr.magic == 0x4254524653495031ULL) {

                std::string request;
                if (hdr.payloadLength > 0) {
                    request.resize(hdr.payloadLength);
                    size_t total = 0;
                    while (total < hdr.payloadLength) {
                        ssize_t chunk = ::read(clientSock, request.data() + total, hdr.payloadLength - total);
                        if (chunk <= 0) break;
                        total += chunk;
                    }
                }

                std::string response;
                int status = 0;
                if (handler_) {
                    status = handler_(hdr.command, request, response);
                }

                IpcResponseHeader respHdr{};
                respHdr.magic = 0x4254524653495031ULL;
                respHdr.statusCode = static_cast<uint32_t>(status);
                respHdr.payloadLength = static_cast<uint32_t>(response.size());

                ::write(clientSock, &respHdr, sizeof(respHdr));
                if (!response.empty()) {
                    ::write(clientSock, response.data(), response.size());
                }
            }

            ::close(clientSock);
        }

        if (listenSock_ >= 0) {
            ::close(listenSock_);
            listenSock_ = -1;
            ::unlink(sockPath);
        }
    }
#endif
};

} // namespace titanbtrfs
