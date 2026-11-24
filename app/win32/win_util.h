#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "msx/common.h"

std::wstring utf8ToWide(const std::string& text);
std::string wideToUtf8(const std::wstring& text);
std::string getWindowTextUtf8(HWND hwnd);
void setWindowTextUtf8(HWND hwnd, const std::string& text);
std::wstring showOpenDialog(HWND owner, const wchar_t* filter, const wchar_t* title,
                            const wchar_t* defaultExtension, const std::wstring& initialDir);
std::wstring showSaveDialog(HWND owner, const wchar_t* filter, const wchar_t* title,
                            const wchar_t* defaultExtension, const std::wstring& initialDir,
                            const std::wstring& suggestedName);
std::wstring exeDirectory();
std::wstring joinPath(const std::wstring& directory, const std::wstring& name);
std::wstring baseNameWithoutExtension(const std::wstring& path);
std::wstring extensionOf(const std::wstring& path);
std::string normalizeToCrlf(const std::string& text);

class Win32FileWriter : public msx::IByteWriter {
public:
    explicit Win32FileWriter(const std::wstring& path);
    ~Win32FileWriter() override;
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool write(const uint8_t* src, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    bool flush() override;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    mutable uint64_t position_ = 0;
};

class Win32FileReader : public msx::IByteReader {
public:
    explicit Win32FileReader(const std::wstring& path);
    ~Win32FileReader() override;
    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    std::size_t read(uint8_t* dst, std::size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override { return size_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    uint64_t size_ = 0;
    mutable uint64_t position_ = 0;
};

bool readFileUtf8Path(const std::wstring& path, std::string& text);
bool writeFileUtf8Path(const std::wstring& path, const std::string& text);
