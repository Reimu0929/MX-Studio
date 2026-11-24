#include "win_util.h"

#include <commdlg.h>

#include <algorithm>
#include <cstring>

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                        nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, nullptr, nullptr);
    return out;
}

std::string getWindowTextUtf8(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return std::string();
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, &text[0], len + 1);
    text.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
    return wideToUtf8(text);
}

void setWindowTextUtf8(HWND hwnd, const std::string& text) {
    SetWindowTextW(hwnd, utf8ToWide(text).c_str());
}

std::wstring exeDirectory() {
    wchar_t path[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    std::wstring p(path);
    const std::size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) return name;
    if (directory[directory.size() - 1] == L'\\' || directory[directory.size() - 1] == L'/') {
        return directory + name;
    }
    return directory + L"\\" + name;
}

std::wstring baseNameWithoutExtension(const std::wstring& path) {
    std::size_t slash = path.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const std::size_t dot = file.find_last_of(L'.');
    if (dot != std::wstring::npos) file = file.substr(0, dot);
    return file;
}

std::wstring extensionOf(const std::wstring& path) {
    const std::size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return std::wstring();
    return path.substr(dot);
}

std::string normalizeToCrlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out.push_back('\r');
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                out.push_back('\n');
                ++i;
            } else {
                out.push_back('\n');
            }
        } else if (c == '\n') {
            out.push_back('\r');
            out.push_back('\n');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

namespace {
std::wstring runDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* title,
                       const wchar_t* defaultExtension, const std::wstring& initialDir,
                       const std::wstring& suggestedName) {
    wchar_t file[MAX_PATH * 4];
    file[0] = 0;
    if (!suggestedName.empty()) {
        lstrcpynW(file, suggestedName.c_str(), static_cast<int>(sizeof(file) / sizeof(file[0])));
    }
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file) / sizeof(file[0]);
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defaultExtension;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(file) : std::wstring();
}
}  // namespace

std::wstring showOpenDialog(HWND owner, const wchar_t* filter, const wchar_t* title,
                            const wchar_t* defaultExtension, const std::wstring& initialDir) {
    return runDialog(owner, false, filter, title, defaultExtension, initialDir, std::wstring());
}

std::wstring showSaveDialog(HWND owner, const wchar_t* filter, const wchar_t* title,
                            const wchar_t* defaultExtension, const std::wstring& initialDir,
                            const std::wstring& suggestedName) {
    return runDialog(owner, true, filter, title, defaultExtension, initialDir, suggestedName);
}

Win32FileWriter::Win32FileWriter(const std::wstring& path) {
    handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
}

Win32FileWriter::~Win32FileWriter() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
}

bool Win32FileWriter::write(const uint8_t* src, std::size_t bytes) {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    std::size_t done = 0;
    while (done < bytes) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes - done, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(handle_, src + done, chunk, &written, nullptr) || written == 0) return false;
        done += written;
        position_ += written;
    }
    return true;
}

bool Win32FileWriter::seek(uint64_t position) {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    LONG high = static_cast<LONG>(position >> 32);
    const DWORD low = SetFilePointer(handle_, static_cast<LONG>(position & 0xFFFFFFFFu), &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) return false;
    position_ = position;
    return true;
}

uint64_t Win32FileWriter::position() const { return position_; }

bool Win32FileWriter::flush() {
    return handle_ != INVALID_HANDLE_VALUE && FlushFileBuffers(handle_) != 0;
}

Win32FileReader::Win32FileReader(const std::wstring& path) {
    handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(handle_, &size)) size_ = static_cast<uint64_t>(size.QuadPart);
    }
}

Win32FileReader::~Win32FileReader() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
}

std::size_t Win32FileReader::read(uint8_t* dst, std::size_t bytes) {
    if (handle_ == INVALID_HANDLE_VALUE || bytes == 0) return 0;
    const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes, 1u << 20));
    DWORD got = 0;
    if (!ReadFile(handle_, dst, chunk, &got, nullptr)) return 0;
    position_ += got;
    return got;
}

bool Win32FileReader::seek(uint64_t position) {
    if (handle_ == INVALID_HANDLE_VALUE || position > size_) return false;
    LONG high = static_cast<LONG>(position >> 32);
    const DWORD low = SetFilePointer(handle_, static_cast<LONG>(position & 0xFFFFFFFFu), &high, FILE_BEGIN);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) return false;
    position_ = position;
    return true;
}

uint64_t Win32FileReader::position() const { return position_; }

bool readFileUtf8Path(const std::wstring& path, std::string& text) {
    Win32FileReader reader(path);
    if (!reader.isOpen()) return false;
    msx::Bytes bytes(static_cast<std::size_t>(reader.size()));
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t n = reader.read(&bytes[done], bytes.size() - done);
        if (n == 0) break;
        done += n;
    }
    bytes.resize(done);
    text.assign(bytes.empty() ? "" : reinterpret_cast<const char*>(&bytes[0]), bytes.size());
    return true;
}

bool writeFileUtf8Path(const std::wstring& path, const std::string& text) {
    Win32FileWriter writer(path);
    if (!writer.isOpen()) return false;
    return writer.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) && writer.flush();
}
