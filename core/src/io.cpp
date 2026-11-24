#include "msx/common.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace msx {

MemoryReader::MemoryReader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
MemoryReader::MemoryReader(const Bytes& data) : data_(data.empty() ? nullptr : &data[0]), size_(data.size()) {}

std::size_t MemoryReader::read(uint8_t* dst, std::size_t bytes) {
    if (pos_ >= size_) return 0;
    const std::size_t n = std::min(bytes, size_ - pos_);
    if (n != 0) std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return n;
}

bool MemoryReader::seek(uint64_t position) {
    if (position > size_) return false;
    pos_ = static_cast<std::size_t>(position);
    return true;
}

bool MemoryWriter::write(const uint8_t* src, std::size_t bytes) {
    if (bytes == 0) return true;
    if (pos_ + bytes > data_.size()) data_.resize(pos_ + bytes);
    std::memcpy(&data_[pos_], src, bytes);
    pos_ += bytes;
    return true;
}

bool MemoryWriter::seek(uint64_t position) {
    if (position > data_.size()) data_.resize(static_cast<std::size_t>(position));
    pos_ = static_cast<std::size_t>(position);
    return true;
}

namespace {
std::FILE* openUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return nullptr;
    std::vector<wchar_t> wpath(static_cast<std::size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);
    const int mlen = MultiByteToWideChar(CP_UTF8, 0, mode, -1, nullptr, 0);
    if (mlen <= 0) return nullptr;
    std::vector<wchar_t> wmode(static_cast<std::size_t>(mlen));
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, &wmode[0], mlen);
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, &wpath[0], &wmode[0]) != 0) return nullptr;
    return file;
#else
    return _wfopen(&wpath[0], &wmode[0]);
#endif
#else
    return std::fopen(path.c_str(), mode);
#endif
}
}  // namespace

StdFileReader::StdFileReader(const std::string& path) {
    file_ = openUtf8(path, "rb");
    if (file_) {
        if (std::fseek(file_, 0, SEEK_END) == 0) {
            const long end = std::ftell(file_);
            if (end > 0) size_ = static_cast<uint64_t>(end);
            std::fseek(file_, 0, SEEK_SET);
        }
    }
}

StdFileReader::~StdFileReader() {
    if (file_) std::fclose(file_);
}

std::size_t StdFileReader::read(uint8_t* dst, std::size_t bytes) {
    if (!file_ || bytes == 0) return 0;
    return std::fread(dst, 1, bytes, file_);
}

bool StdFileReader::seek(uint64_t position) {
    if (!file_ || position > 0x7FFFFFFFULL) return false;
    return std::fseek(file_, static_cast<long>(position), SEEK_SET) == 0;
}

uint64_t StdFileReader::position() const {
    if (!file_) return 0;
    const long p = std::ftell(file_);
    return p < 0 ? 0 : static_cast<uint64_t>(p);
}

StdFileWriter::StdFileWriter(const std::string& path) {
    file_ = openUtf8(path, "wb");
}

StdFileWriter::~StdFileWriter() {
    if (file_) std::fclose(file_);
}

bool StdFileWriter::write(const uint8_t* src, std::size_t bytes) {
    if (!file_) return false;
    return bytes == 0 || std::fwrite(src, 1, bytes, file_) == bytes;
}

bool StdFileWriter::seek(uint64_t position) {
    if (!file_ || position > 0x7FFFFFFFULL) return false;
    return std::fseek(file_, static_cast<long>(position), SEEK_SET) == 0;
}

uint64_t StdFileWriter::position() const {
    if (!file_) return 0;
    const long p = std::ftell(file_);
    return p < 0 ? 0 : static_cast<uint64_t>(p);
}

bool StdFileWriter::flush() {
    return !file_ || std::fflush(file_) == 0;
}

bool readWholeFile(const std::string& path, Bytes& out, std::string& error) {
    StdFileReader reader(path);
    if (!reader.isOpen()) {
        error = "cannot open file: " + path;
        return false;
    }
    out.clear();
    out.resize(static_cast<std::size_t>(reader.size()));
    std::size_t done = 0;
    while (done < out.size()) {
        const std::size_t n = reader.read(&out[done], out.size() - done);
        if (n == 0) break;
        done += n;
    }
    if (done != out.size()) {
        out.resize(done);
        error = "short read: " + path;
        return false;
    }
    return true;
}

bool writeWholeFile(const std::string& path, const Bytes& data, std::string& error) {
    StdFileWriter writer(path);
    if (!writer.isOpen()) {
        error = "cannot create file: " + path;
        return false;
    }
    const bool ok = data.empty() ? true : writer.write(&data[0], data.size());
    if (!ok || !writer.flush()) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

}  // namespace msx
