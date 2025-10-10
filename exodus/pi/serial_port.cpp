#include "serial_port.h"

#include <sstream>

SerialPort::SerialPort()
    : handle_(INVALID_HANDLE_VALUE),
    readInterval_(50),
    readTotal_(50)
{
}

SerialPort::~SerialPort()
{
    close();
}

bool SerialPort::open(const std::string& portName, DWORD baudRate)
{
    close();

    std::string fullName = "\\\\.\\" + portName;
    handle_ = CreateFileA(
        fullName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle_ == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    portName_ = portName;

    if (!configurePort(baudRate))
    {
        close();
        return false;
    }

    return true;
}

void SerialPort::close()
{
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        CancelIo(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    portName_.clear();
}

bool SerialPort::configurePort(DWORD baudRate)
{
    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(handle_, &dcb))
    {
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(handle_, &dcb))
    {
        return false;
    }

    updateTimeouts(readInterval_, readTotal_);
    PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

void SerialPort::updateTimeouts(DWORD intervalMs, DWORD totalMs)
{
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = intervalMs;
    timeouts.ReadTotalTimeoutConstant = totalMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(handle_, &timeouts);
}

bool SerialPort::write(const std::string& data)
{
    if (!isOpen())
        return false;

    DWORD bytesWritten = 0;
    if (!WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr))
    {
        return false;
    }

    return bytesWritten == data.size();
}

bool SerialPort::writeLine(const std::string& line)
{
    std::string withNewline = line;
    if (withNewline.empty() || withNewline.back() != '\n')
        withNewline += '\n';
    return write(withNewline);
}

bool SerialPort::readByte(char& ch, bool& fatalError)
{
    fatalError = false;

    if (!isOpen())
    {
        fatalError = true;
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(handle_, &ch, 1, &bytesRead, nullptr))
    {
        DWORD err = GetLastError();
        if (err == ERROR_TIMEOUT || err == ERROR_OPERATION_ABORTED)
        {
            return false;
        }
        fatalError = true;
        return false;
    }

    if (bytesRead == 0)
        return false;

    return true;
}

bool SerialPort::readLine(std::string& line, unsigned int timeoutMs, bool& fatalError)
{
    fatalError = false;
    line.clear();

    auto deadline = GetTickCount64() + timeoutMs;
    std::string buffer;

    while (GetTickCount64() <= deadline)
    {
        char ch = 0;
        bool fatal = false;
        if (!readByte(ch, fatal))
        {
            if (fatal)
            {
                fatalError = true;
                return false;
            }
            Sleep(5);
            continue;
        }

        if (ch == '\n')
        {
            line = buffer;
            return true;
        }
        if (ch == '\r')
            continue;

        buffer.push_back(ch);
    }

    return false;
}

void SerialPort::cancelIo()
{
    if (handle_ != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(handle_, nullptr);
    }
}
