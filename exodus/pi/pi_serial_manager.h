#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SerialPort;

class PiSerialManager
{
public:
    PiSerialManager();
    ~PiSerialManager();

    bool connect(const std::string& preferredPort = std::string());
    void disconnect();

    bool isConnected() const { return connected_.load(std::memory_order_acquire); }

    bool sendMouseDelta(int dx, int dy);

    void updatePresetList(const std::vector<std::string>& presets);

    void setPresetSelectionCallback(std::function<void(const std::string&)> callback);

    std::string statusMessage() const;

    std::string activePort() const;

private:
    bool attemptPort(const std::string& portName);
    void readerThread();
    void handleIncomingLine(const std::string& line);
    void sendPresetListLocked();
    std::vector<std::string> enumerateCandidatePorts(const std::string& preferredPort) const;
    void setStatus(const std::string& status) const;

    mutable std::mutex stateMutex_;
    std::unique_ptr<SerialPort> port_;
    std::thread reader_;
    std::atomic<bool> stopReader_;
    std::atomic<bool> connected_;

    std::vector<std::string> cachedPresets_;
    std::function<void(const std::string&)> presetCallback_;
    mutable std::mutex presetMutex_;

    mutable std::mutex writeMutex_;
    mutable std::mutex statusMutex_;
    mutable std::string status_;
};

extern PiSerialManager gPiSerialManager;
