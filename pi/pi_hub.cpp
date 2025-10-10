#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wiringPi.h>
#include <wiringPiSPI.h>

namespace
{
constexpr int LCD_WIDTH = 240;
constexpr int LCD_HEIGHT = 240;
constexpr int SPI_CHANNEL = 0;
constexpr int SPI_SPEED = 40000000;
constexpr int PIN_DC = 25;
constexpr int PIN_RST = 27;
constexpr int PIN_BL = 24;
constexpr int PIN_CS = 8;

constexpr int PIN_KEY1 = 21;
constexpr int PIN_KEY2 = 20;
constexpr int PIN_KEY3 = 16;
constexpr int PIN_JOY_UP = 6;
constexpr int PIN_JOY_DOWN = 19;
constexpr int PIN_JOY_LEFT = 5;
constexpr int PIN_JOY_RIGHT = 26;
constexpr int PIN_JOY_PRESS = 13;

uint16_t Color565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

const uint16_t COLOR_BG = Color565(0, 0, 0);
const uint16_t COLOR_TEXT = Color565(220, 220, 220);
const uint16_t COLOR_HILIGHT = Color565(50, 120, 220);
const uint16_t COLOR_HILIGHT_TEXT = Color565(255, 255, 255);

const uint8_t font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x72,0x49,0x49,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00}, {0x08,0x04,0x08,0x10,0x08}, {0x7F,0x41,0x41,0x41,0x7F}
};

class SerialConnection
{
public:
    explicit SerialConnection(const std::string& device)
    {
        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0)
        {
            throw std::runtime_error("Failed to open serial port");
        }
        termios tty{};
        if (tcgetattr(fd_, &tty) != 0)
        {
            throw std::runtime_error("tcgetattr failed");
        }
        cfsetospeed(&tty, B115200);
        cfsetispeed(&tty, B115200);
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_iflag &= ~(IGNBRK | IXON | IXOFF | IXANY);
        tty.c_lflag = 0;
        tty.c_oflag = 0;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
        if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        {
            throw std::runtime_error("tcsetattr failed");
        }
    }

    ~SerialConnection()
    {
        if (fd_ >= 0)
        {
            close(fd_);
        }
    }

    bool writeLine(const std::string& line)
    {
        std::string payload = line;
        if (payload.empty() || payload.back() != '\n')
            payload.push_back('\n');
        ssize_t written = ::write(fd_, payload.data(), payload.size());
        return written == static_cast<ssize_t>(payload.size());
    }

    bool readLine(std::string& out, int timeoutMs)
    {
        out.clear();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            char ch = 0;
            ssize_t result = ::read(fd_, &ch, 1);
            if (result == 1)
            {
                if (ch == '\n')
                    return true;
                if (ch != '\r')
                    out.push_back(ch);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        return !out.empty();
    }

private:
    int fd_ = -1;
};

class HidMouse
{
public:
    bool initialize(uint16_t vendor, uint16_t product, uint8_t interface, uint8_t endpoint)
    {
        vendor_ = vendor;
        product_ = product;
        interface_ = interface;
        endpoint_ = endpoint;
        if (libusb_init(&ctx_) != 0)
        {
            std::cerr << "Failed to init libusb" << std::endl;
            return false;
        }
        libusb_set_option(ctx_, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
        handle_ = libusb_open_device_with_vid_pid(ctx_, vendor_, product_);
        if (!handle_)
        {
            std::cerr << "Unable to open HID gadget. Ensure VID/PID match." << std::endl;
            return false;
        }
        libusb_set_auto_detach_kernel_driver(handle_, 1);
        if (libusb_claim_interface(handle_, interface_) != 0)
        {
            std::cerr << "Failed to claim HID interface" << std::endl;
            return false;
        }
        workerRunning_ = true;
        worker_ = std::thread(&HidMouse::workerLoop, this);
        return true;
    }

    void shutdown()
    {
        workerRunning_ = false;
        queueCv_.notify_all();
        if (worker_.joinable())
            worker_.join();
        if (handle_)
        {
            libusb_release_interface(handle_, interface_);
            libusb_close(handle_);
            handle_ = nullptr;
        }
        if (ctx_)
        {
            libusb_exit(ctx_);
            ctx_ = nullptr;
        }
    }

    void enqueue(int dx, int dy)
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        movements_.push({dx, dy});
        queueCv_.notify_one();
    }

private:
    void workerLoop()
    {
        while (workerRunning_)
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [&] { return !workerRunning_ || !movements_.empty(); });
            if (!workerRunning_)
                break;
            auto movement = movements_.front();
            movements_.pop();
            lock.unlock();

            uint8_t report[4] = {0};
            report[1] = static_cast<uint8_t>(std::clamp(movement.first, -127, 127));
            report[2] = static_cast<uint8_t>(std::clamp(movement.second, -127, 127));
            int transferred = 0;
            int rc = libusb_interrupt_transfer(handle_, endpoint_, report, sizeof(report), &transferred, 0);
            if (rc != 0 || transferred != static_cast<int>(sizeof(report)))
            {
                std::cerr << "HID transfer failed: " << libusb_error_name(rc) << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    libusb_context* ctx_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    uint16_t vendor_ = 0;
    uint16_t product_ = 0;
    uint8_t interface_ = 0;
    uint8_t endpoint_ = 0x81;

    std::queue<std::pair<int, int>> movements_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::thread worker_;
    std::atomic<bool> workerRunning_{false};
};

class St7789Display
{
public:
    bool initialize()
    {
        if (wiringPiSetupGpio() != 0)
        {
            std::cerr << "Failed to init wiringPi" << std::endl;
            return false;
        }
        pinMode(PIN_DC, OUTPUT);
        pinMode(PIN_RST, OUTPUT);
        pinMode(PIN_BL, OUTPUT);
        pinMode(PIN_CS, OUTPUT);
        digitalWrite(PIN_CS, HIGH);
        digitalWrite(PIN_BL, HIGH);

        if (wiringPiSPISetup(SPI_CHANNEL, SPI_SPEED) < 0)
        {
            std::cerr << "Failed to setup SPI" << std::endl;
            return false;
        }

        reset();
        runInitSequence();
        clear(COLOR_BG);
        return true;
    }

    void clear(uint16_t color)
    {
        setWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
        sendColorFill(color, LCD_WIDTH * LCD_HEIGHT);
    }

    void drawChar(int x, int y, char c, uint16_t fg, uint16_t bg)
    {
        if (c < 32 || c > 127)
            c = '?';
        const uint8_t* bitmap = font5x7[c - 32];
        setWindow(x, y, x + 5, y + 7);
        std::vector<uint8_t> buffer(6 * 8 * 2, 0);
        size_t idx = 0;
        for (int row = 0; row < 8; ++row)
        {
            for (int col = 0; col < 6; ++col)
            {
                bool pixel = false;
                if (col < 5)
                    pixel = (bitmap[col] >> row) & 0x01;
                uint16_t color = pixel ? fg : bg;
                buffer[idx++] = static_cast<uint8_t>(color >> 8);
                buffer[idx++] = static_cast<uint8_t>(color & 0xFF);
            }
        }
        writeData(buffer.data(), buffer.size());
    }

    void drawText(int x, int y, const std::string& text, uint16_t fg, uint16_t bg)
    {
        int cursorX = x;
        for (char c : text)
        {
            if (cursorX + 6 >= LCD_WIDTH)
                break;
            drawChar(cursorX, y, c, fg, bg);
            cursorX += 6;
        }
    }

    void drawMenu(const std::vector<std::string>& items, size_t selected, size_t top)
    {
        clear(COLOR_BG);
        const int itemHeight = 20;
        int y = 10;
        for (size_t i = top; i < items.size() && y + itemHeight <= LCD_HEIGHT - 10; ++i)
        {
            bool highlight = (i == selected);
            uint16_t bg = highlight ? COLOR_HILIGHT : COLOR_BG;
            uint16_t fg = highlight ? COLOR_HILIGHT_TEXT : COLOR_TEXT;
            fillRect(10, y - 2, LCD_WIDTH - 20, itemHeight - 4, bg);
            drawText(14, y, items[i], fg, bg);
            y += itemHeight;
        }
    }

private:
    void reset()
    {
        digitalWrite(PIN_RST, HIGH);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        digitalWrite(PIN_RST, LOW);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        digitalWrite(PIN_RST, HIGH);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void runInitSequence()
    {
        writeCommand(0x36); writeDataByte(0x00);
        writeCommand(0x3A); writeDataByte(0x55);
        writeCommand(0xB2); uint8_t b2[] = {0x0C,0x0C,0x00,0x33,0x33}; writeData(b2, sizeof(b2));
        writeCommand(0xB7); writeDataByte(0x35);
        writeCommand(0xBB); writeDataByte(0x19);
        writeCommand(0xC0); writeDataByte(0x2C);
        writeCommand(0xC2); writeDataByte(0x01);
        writeCommand(0xC3); writeDataByte(0x12);
        writeCommand(0xC4); writeDataByte(0x20);
        writeCommand(0xC6); writeDataByte(0x0F);
        writeCommand(0xD0); uint8_t d0[] = {0xA4,0xA1}; writeData(d0, sizeof(d0));
        writeCommand(0xE0); uint8_t e0[] = {0xD0,0x00,0x05,0x0E,0x15,0x0D,0x37,0x43,0x47,0x09,0x15,0x12,0x16,0x19}; writeData(e0, sizeof(e0));
        writeCommand(0xE1); uint8_t e1[] = {0xD0,0x00,0x05,0x0D,0x0C,0x06,0x2D,0x44,0x40,0x0E,0x1C,0x18,0x16,0x19}; writeData(e1, sizeof(e1));
        writeCommand(0x21);
        writeCommand(0x11);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        writeCommand(0x29);
    }

    void setWindow(int x0, int y0, int x1, int y1)
    {
        uint8_t caset[] = {static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0 & 0xFF), static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF)};
        uint8_t raset[] = {static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0 & 0xFF), static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF)};
        writeCommand(0x2A); writeData(caset, sizeof(caset));
        writeCommand(0x2B); writeData(raset, sizeof(raset));
        writeCommand(0x2C);
    }

    void fillRect(int x, int y, int w, int h, uint16_t color)
    {
        int x1 = x + w - 1;
        int y1 = y + h - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
        if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
        setWindow(x, y, x1, y1);
        sendColorFill(color, (x1 - x + 1) * (y1 - y + 1));
    }

    void sendColorFill(uint16_t color, size_t count)
    {
        std::vector<uint8_t> data(count * 2);
        for (size_t i = 0; i < count; ++i)
        {
            data[i * 2] = static_cast<uint8_t>(color >> 8);
            data[i * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
        }
        writeData(data.data(), data.size());
    }

    void writeCommand(uint8_t cmd)
    {
        digitalWrite(PIN_DC, LOW);
        digitalWrite(PIN_CS, LOW);
        wiringPiSPIDataRW(SPI_CHANNEL, &cmd, 1);
        digitalWrite(PIN_CS, HIGH);
    }

    void writeData(const uint8_t* data, size_t len)
    {
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        wiringPiSPIDataRW(SPI_CHANNEL, const_cast<unsigned char*>(data), len);
        digitalWrite(PIN_CS, HIGH);
    }

    void writeDataByte(uint8_t value)
    {
        writeData(&value, 1);
    }
};

struct PresetMenu
{
    void setItems(std::vector<std::string> newItems)
    {
        items = std::move(newItems);
        if (selected >= items.size())
            selected = 0;
        if (topIndex > selected)
            topIndex = 0;
        dirty = true;
    }

    void moveUp()
    {
        if (items.empty()) return;
        if (selected == 0)
            selected = items.size() - 1;
        else
            --selected;
        adjustWindow();
        dirty = true;
    }

    void moveDown()
    {
        if (items.empty()) return;
        selected = (selected + 1) % items.size();
        adjustWindow();
        dirty = true;
    }

    void resetTop()
    {
        topIndex = 0;
        dirty = true;
    }

    const std::string& current() const
    {
        static std::string empty;
        if (items.empty())
            return empty;
        return items[selected];
    }

    void adjustWindow()
    {
        const size_t visible = 9;
        if (selected < topIndex)
            topIndex = selected;
        else if (selected >= topIndex + visible)
            topIndex = selected - visible + 1;
    }

    std::vector<std::string> items;
    size_t selected = 0;
    size_t topIndex = 0;
    bool dirty = true;
};

struct ButtonState
{
    int pin;
    bool previous = false;

    bool pressed()
    {
        bool current = digitalRead(pin) == LOW;
        bool rising = current && !previous;
        previous = current;
        return rising;
    }
};

std::atomic<bool> running(true);

void signalHandler(int)
{
    running = false;
}

} // namespace

int main(int argc, char** argv)
{
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::string serialPort = argc > 1 ? argv[1] : "/dev/ttyACM0";
    uint16_t hidVendor = 0x1D6B;
    uint16_t hidProduct = 0x0104;
    if (argc > 3)
    {
        hidVendor = static_cast<uint16_t>(std::stoi(argv[2], nullptr, 16));
        hidProduct = static_cast<uint16_t>(std::stoi(argv[3], nullptr, 16));
    }

    try
    {
        SerialConnection serial(serialPort);
        St7789Display display;
        if (!display.initialize())
        {
            return 1;
        }

        HidMouse mouse;
        if (!mouse.initialize(hidVendor, hidProduct, 0, 0x81))
        {
            std::cerr << "Continuing without HID mouse output." << std::endl;
        }

        pinMode(PIN_KEY1, INPUT);
        pinMode(PIN_KEY2, INPUT);
        pinMode(PIN_KEY3, INPUT);
        pinMode(PIN_JOY_UP, INPUT);
        pinMode(PIN_JOY_DOWN, INPUT);
        pinMode(PIN_JOY_LEFT, INPUT);
        pinMode(PIN_JOY_RIGHT, INPUT);
        pinMode(PIN_JOY_PRESS, INPUT);
        pullUpDnControl(PIN_KEY1, PUD_UP);
        pullUpDnControl(PIN_KEY2, PUD_UP);
        pullUpDnControl(PIN_KEY3, PUD_UP);
        pullUpDnControl(PIN_JOY_UP, PUD_UP);
        pullUpDnControl(PIN_JOY_DOWN, PUD_UP);
        pullUpDnControl(PIN_JOY_LEFT, PUD_UP);
        pullUpDnControl(PIN_JOY_RIGHT, PUD_UP);
        pullUpDnControl(PIN_JOY_PRESS, PUD_UP);

        ButtonState btnSelect{PIN_KEY1};
        ButtonState btnBack{PIN_KEY2};
        ButtonState joyPress{PIN_JOY_PRESS};
        ButtonState joyUp{PIN_JOY_UP};
        ButtonState joyDown{PIN_JOY_DOWN};

        PresetMenu menu;
        display.drawMenu(menu.items, menu.selected, menu.topIndex);

        std::string line;
        bool handshakeComplete = false;

        while (running)
        {
            if (!handshakeComplete)
            {
                if (serial.readLine(line, 1000))
                {
                    if (line == "PC_HELLO")
                    {
                        serial.writeLine("PI_READY");
                        serial.readLine(line, 1000); // Expect PC_ACK
                        handshakeComplete = true;
                    }
                }
                continue;
            }

            if (serial.readLine(line, 50))
            {
                if (line.rfind("PRESETS:", 0) == 0)
                {
                    std::vector<std::string> presets;
                    std::stringstream ss(line.substr(8));
                    std::string item;
                    while (std::getline(ss, item, ','))
                    {
                        if (!item.empty())
                            presets.push_back(item);
                    }
                    menu.setItems(presets);
                }
                else if (line.rfind("MOUSE:", 0) == 0)
                {
                    int dx = 0, dy = 0;
                    char comma = 0;
                    std::stringstream ss(line.substr(6));
                    ss >> dx >> comma >> dy;
                    mouse.enqueue(dx, dy);
                }
            }

            if (joyUp.pressed())
                menu.moveUp();
            if (joyDown.pressed())
                menu.moveDown();
            if (btnBack.pressed())
                menu.resetTop();
            if (btnSelect.pressed() || joyPress.pressed())
            {
                if (!menu.current().empty())
                {
                    serial.writeLine("SELECT:" + menu.current());
                }
            }

            if (menu.dirty)
            {
                menu.dirty = false;
                display.drawMenu(menu.items, menu.selected, menu.topIndex);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        mouse.shutdown();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
