#include "pi_serial_manager.h"

#include "serial_port.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

PiSerialManager gPiSerialManager;

PiSerialManager::PiSerialManager()
    : stopReader_(false),
    connected_(false)
{
    setStatus("Disconnected");
}

PiSerialManager::~PiSerialManager()
{
    disconnect();
}

bool PiSerialManager::connect(const std::string& preferredPort)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (connected_)
        return true;

    setStatus("Scanning for Pi...");

    auto ports = enumerateCandidatePorts(preferredPort);
    for (const auto& portName : ports)
    {
        if (attemptPort(portName))
        {
            return true;
        }
    }

    setStatus("Pi not detected");
    return false;
}

void PiSerialManager::disconnect()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (reader_.joinable())
        {
            stopReader_.store(true, std::memory_order_release);
            if (port_)
                port_->cancelIo();
        }
    }

    if (reader_.joinable())
        reader_.join();

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (port_)
        port_->close();
    port_.reset();
    connected_.store(false, std::memory_order_release);
    stopReader_.store(false, std::memory_order_release);
    setStatus("Disconnected");
}

bool PiSerialManager::attemptPort(const std::string& portName)
{
    auto port = std::make_unique<SerialPort>();
    if (!port->open(portName))
        return false;

    setStatus("Handshake on " + portName + "...");

    if (!port->writeLine("PC_HELLO"))
    {
        port->close();
        return false;
    }

    bool fatalError = false;
    std::string response;
    if (!port->readLine(response, 2000, fatalError))
    {
        port->close();
        return false;
    }

    if (response != "PI_READY")
    {
        port->close();
        return false;
    }

    if (!port->writeLine("PC_ACK"))
    {
        port->close();
        return false;
    }

    port_ = std::move(port);
    connected_.store(true, std::memory_order_release);
    setStatus("Connected on " + portName);

    stopReader_.store(false, std::memory_order_release);
    reader_ = std::thread(&PiSerialManager::readerThread, this);

    sendPresetListLocked();

    return true;
}

bool PiSerialManager::sendMouseDelta(int dx, int dy)
{
    if (!isConnected())
        return false;

    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!port_ || !port_->isOpen())
        return false;

    std::ostringstream oss;
    oss << "MOUSE:" << dx << ',' << dy << '\n';
    return port_->write(oss.str());
}

void PiSerialManager::updatePresetList(const std::vector<std::string>& presets)
{
    {
        std::lock_guard<std::mutex> lock(presetMutex_);
        cachedPresets_ = presets;
    }

    if (isConnected())
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        sendPresetListLocked();
    }
}

void PiSerialManager::sendPresetListLocked()
{
    if (!port_ || !port_->isOpen())
        return;

    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!port_->isOpen())
        return;

    std::vector<std::string> presetsCopy;
    {
        std::lock_guard<std::mutex> presetLock(presetMutex_);
        presetsCopy = cachedPresets_;
    }

    std::ostringstream oss;
    oss << "PRESETS:";
    for (size_t i = 0; i < presetsCopy.size(); ++i)
    {
        if (i != 0)
            oss << ',';
        oss << presetsCopy[i];
    }
    oss << '\n';

    port_->write(oss.str());
}

void PiSerialManager::setPresetSelectionCallback(std::function<void(const std::string&)> callback)
{
    std::lock_guard<std::mutex> lock(presetMutex_);
    presetCallback_ = std::move(callback);
}

void PiSerialManager::readerThread()
{
    std::string buffer;
    buffer.reserve(128);

    while (!stopReader_.load(std::memory_order_acquire))
    {
        if (!port_ || !port_->isOpen())
            break;

        char ch = 0;
        bool fatalError = false;
        if (!port_->readByte(ch, fatalError))
        {
            if (fatalError)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (ch == '\n')
        {
            handleIncomingLine(buffer);
            buffer.clear();
        }
        else if (ch != '\r')
        {
            buffer.push_back(ch);
        }
    }

    connected_.store(false, std::memory_order_release);
    setStatus("Disconnected");

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (port_)
    {
        port_->close();
        port_.reset();
    }
}

void PiSerialManager::handleIncomingLine(const std::string& line)
{
    if (line.rfind("SELECT:", 0) == 0)
    {
        std::string presetName = line.substr(7);
        std::function<void(const std::string&)> callback;
        {
            std::lock_guard<std::mutex> lock(presetMutex_);
            callback = presetCallback_;
        }

        if (callback)
        {
            callback(presetName);
        }
    }
    else if (!line.empty())
    {
        std::cout << "[PiSerial] " << line << std::endl;
    }
}

std::vector<std::string> PiSerialManager::enumerateCandidatePorts(const std::string& preferredPort) const
{
    std::vector<std::string> ports;
    if (!preferredPort.empty())
        ports.push_back(preferredPort);

    const int maxPorts = 64;
    for (int i = 1; i <= maxPorts; ++i)
    {
        std::ostringstream oss;
        oss << "COM" << i;
        std::string candidate = oss.str();
        if (!preferredPort.empty() && candidate == preferredPort)
            continue;
        ports.push_back(candidate);
    }

    return ports;
}

std::string PiSerialManager::statusMessage() const
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

std::string PiSerialManager::activePort() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (port_)
        return port_->portName();
    return {};
}

void PiSerialManager::setStatus(const std::string& status) const
{
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = status;
}
