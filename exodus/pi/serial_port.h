#pragma once

#include <string>
#include <windows.h>

class SerialPort
{
public:
    SerialPort();
    ~SerialPort();

    bool open(const std::string& portName, DWORD baudRate = 115200);
    void close();

    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool write(const std::string& data);
    bool writeLine(const std::string& line);

    bool readByte(char& ch, bool& fatalError);
    bool readLine(std::string& line, unsigned int timeoutMs, bool& fatalError);

    void cancelIo();

    const std::string& portName() const { return portName_; }

private:
    bool configurePort(DWORD baudRate);
    void updateTimeouts(DWORD intervalMs, DWORD totalMs);

    HANDLE handle_;
    std::string portName_;
    DWORD readInterval_;
    DWORD readTotal_;
};
