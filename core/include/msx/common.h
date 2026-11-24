#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace msx {

using Bytes = std::vector<uint8_t>;

enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity;
    int line;         // 1-based source line, 0 when unknown
    int column;       // 1-based source column, 0 when unknown
    uint64_t sample;  // sample position for audio diagnostics, 0 when unknown
    std::string message;
};

class Diagnostics {
public:
    void add(Severity severity, int line, int column, const std::string& message);
    void addAtSample(Severity severity, uint64_t sample, const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void info(int line, int column, const std::string& message);
    void warning(int line, int column, const std::string& message);
    void error(int line, int column, const std::string& message);

    bool hasErrors() const;
    std::size_t errorCount() const { return errors_; }
    std::size_t warningCount() const { return warnings_; }
    const std::vector<Diagnostic>& items() const { return items_; }
    std::string format() const;

private:
    std::vector<Diagnostic> items_;
    std::size_t errors_ = 0;
    std::size_t warnings_ = 0;
};

inline uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline void appendLe16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void appendLe32(Bytes& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

std::string formatSeconds(uint64_t sample, unsigned sampleRate);
std::string hexByte(uint8_t value);

// ---------------------------------------------------------------------------
// Minimal stream interfaces.  The core never touches a path directly so that
// the Win32 host can open files with CreateFileW (Unicode paths) while the
// command line tool and tests use the stdio adapters below.
// ---------------------------------------------------------------------------
class IByteReader {
public:
    virtual ~IByteReader() {}
    virtual std::size_t read(uint8_t* dst, std::size_t bytes) = 0;
    virtual bool seek(uint64_t position) = 0;
    virtual uint64_t position() const = 0;
    virtual uint64_t size() const = 0;
};

class IByteWriter {
public:
    virtual ~IByteWriter() {}
    virtual bool write(const uint8_t* src, std::size_t bytes) = 0;
    virtual bool seek(uint64_t position) = 0;
    virtual uint64_t position() const = 0;
    virtual bool flush() = 0;
};

class MemoryReader : public IByteReader {
public:
    MemoryReader(const uint8_t* data, std::size_t size);
    explicit MemoryReader(const Bytes& data);
    std::size_t read(uint8_t* dst, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override { return pos_; }
    uint64_t size() const override { return size_; }

private:
    const uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

class MemoryWriter : public IByteWriter {
public:
    bool write(const uint8_t* src, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override { return pos_; }
    bool flush() override { return true; }
    const Bytes& data() const { return data_; }
    Bytes& data() { return data_; }

private:
    Bytes data_;
    std::size_t pos_ = 0;
};

class StdFileReader : public IByteReader {
public:
    explicit StdFileReader(const std::string& path);
    ~StdFileReader();
    bool isOpen() const { return file_ != nullptr; }
    std::size_t read(uint8_t* dst, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override { return size_; }

private:
    std::FILE* file_ = nullptr;
    uint64_t size_ = 0;
};

class StdFileWriter : public IByteWriter {
public:
    explicit StdFileWriter(const std::string& path);
    ~StdFileWriter();
    bool isOpen() const { return file_ != nullptr; }
    bool write(const uint8_t* src, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    bool flush() override;

private:
    std::FILE* file_ = nullptr;
};

bool readWholeFile(const std::string& path, Bytes& out, std::string& error);
bool writeWholeFile(const std::string& path, const Bytes& data, std::string& error);

}  // namespace msx
