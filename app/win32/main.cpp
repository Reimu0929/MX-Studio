#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#define _RICHEDIT_VER 0x0200
#include <richedit.h>
#include <shellapi.h>
#include <imm.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "audio.h"
#include "meter.h"
#include "resource.h"
#include "win_util.h"

#include "msx/basic.h"
#include "msx/basic_complete.h"
#include "msx/basic_token.h"
#include "msx/basic_lexer.h"
#include "msx/charset.h"
#include "msx/common.h"
#include "msx/profile.h"
#include "msx/tape.h"
#include "msx/utf8.h"
#include "msx/wav.h"

namespace {

const wchar_t* kWindowClass = L"MsxCf2000StudioMain";
const wchar_t* kRegKey = L"Software\\MSXCF2000Studio";

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND editor = nullptr;
    HWND output = nullptr;
    HWND status = nullptr;
    HWND meter = nullptr;
    HWND popup = nullptr;
    HWND buttons[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    HFONT uiFont = nullptr;
    HFONT monoFont = nullptr;
    bool richEdit = false;
    bool modified = false;
    bool ignoreChange = false;
    bool highlighting = false;
    bool monitoring = false;
    std::wstring path;
    std::wstring logText;
    std::wstring tapeName = L"TEST";
    std::wstring lastDir;
    unsigned inputDevice = static_cast<unsigned>(-1);  // WAVE_MAPPER
    int baud = 1200;
    AudioEngine* audio = nullptr;

    // completion
    std::vector<msx::CompletionItem> completions;
    int completionWordStart = 0;
    int completionCaret = 0;
    double inputHoldDb = -999.0;
    DWORD inputHoldUntil = 0;
    int sendVolume = 100;
    bool sendVolumeAuto = true;
    bool autoNormalizeRecording = true;
    std::wstring lastRecordingPath;
};

#define WM_APP_AUTOCOMPLETE (WM_APP + 20)

App g_app;
WNDPROC g_editorProc = nullptr;

std::wstring severityLabel(msx::Severity severity) {
    switch (severity) {
        case msx::Severity::Error: return L"エラー";
        case msx::Severity::Warning: return L"警告";
        default: return L"情報";
    }
}

void appendLog(const std::wstring& text) {
    if (text.empty()) return;
    g_app.logText += text;
    if (g_app.logText.size() > 400000) {
        g_app.logText.erase(0, g_app.logText.size() - 300000);
    }
    SetWindowTextW(g_app.output, g_app.logText.c_str());
    SendMessageW(g_app.output, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(g_app.output, EM_SCROLLCARET, 0, 0);
}

void logLine(const std::wstring& text) { appendLog(text + L"\r\n"); }

void logDiagnostics(const msx::Diagnostics& diags) {
    for (std::size_t i = 0; i < diags.items().size(); ++i) {
        const msx::Diagnostic& d = diags.items()[i];
        std::wstring line;
        if (d.line > 0) {
            line += std::to_wstring(d.line);
            if (d.column > 0) {
                line += L":";
                line += std::to_wstring(d.column);
            }
            line += L": ";
        } else if (d.sample > 0) {
            line += L"sample ";
            line += std::to_wstring(d.sample);
            line += L": ";
        }
        line += severityLabel(d.severity);
        line += L": ";
        line += utf8ToWide(d.message);
        appendLog(line + L"\r\n");
    }
}

void updateTitle() {
    std::wstring title = L"MSX CF-2000 Studio";
    if (!g_app.path.empty()) {
        const std::size_t slash = g_app.path.find_last_of(L"\\/");
        title += L" - ";
        title += slash == std::wstring::npos ? g_app.path : g_app.path.substr(slash + 1);
    }
    if (g_app.modified) title += L" *";
    SetWindowTextW(g_app.main, title.c_str());
}

std::wstring formatDbfs(double dbfs) {
    if (dbfs <= -998.0) return L"−∞ dBFS";
    wchar_t buf[64];
    swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%.1f dBFS", dbfs);
    return buf;
}

std::wstring getEditorWide() {
    const int len = GetWindowTextLengthW(g_app.editor);
    if (len <= 0) return std::wstring();
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(g_app.editor, &text[0], len + 1);
    text.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
    return text;
}

std::string getEditorUtf8() { return wideToUtf8(getEditorWide()); }

// RichEdit's character positions count one character per line break, while
// WM_GETTEXT may return CRLF.  Normalize to LF for offset calculations so
// EM_EXSETSEL / EM_REPLACESEL positions stay in sync.
std::wstring getEditorWideLf() {
    const std::wstring wide = getEditorWide();
    std::wstring out;
    out.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\r') {
            out.push_back(L'\n');
            if (i + 1 < wide.size() && wide[i + 1] == L'\n') ++i;
        } else {
            out.push_back(wide[i]);
        }
    }
    return out;
}

void setEditorText(const std::wstring& text) {
    g_app.ignoreChange = true;
    SetWindowTextW(g_app.editor, text.c_str());
    SendMessageW(g_app.editor, EM_SETSEL, 0, 0);
    g_app.ignoreChange = false;
    InvalidateRect(g_app.editor, nullptr, TRUE);
    KillTimer(g_app.main, 1);
    PostMessageW(g_app.main, WM_TIMER, 1, 0);
}

bool saveFile(bool saveAs);

bool askSaveIfModified() {
    if (!g_app.modified) return true;
    const int answer = MessageBoxW(g_app.main, L"変更を保存しますか？", L"MSX CF-2000 Studio",
                                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDNO) return true;
    saveFile(false);
    return !g_app.modified;
}

bool openPath(const std::wstring& path);
bool saveTo(const std::wstring& path);

void loadSettings() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, L"TapeName", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
        g_app.tapeName = buffer;
    }
    size = sizeof(buffer);
    if (RegQueryValueExW(key, L"LastDir", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size) ==
            ERROR_SUCCESS &&
        type == REG_SZ) {
        g_app.lastDir = buffer;
    }
    DWORD baud = 1200;
    size = sizeof(baud);
    if (RegQueryValueExW(key, L"Baud", nullptr, &type, reinterpret_cast<LPBYTE>(&baud), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        g_app.baud = (baud == 2400) ? 2400 : 1200;
    }
    DWORD device = static_cast<DWORD>(-1);
    size = sizeof(device);
    if (RegQueryValueExW(key, L"InputDevice", nullptr, &type, reinterpret_cast<LPBYTE>(&device), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD) {
        g_app.inputDevice = static_cast<unsigned>(device);
    }
    DWORD sendVolume = 100;
    size = sizeof(sendVolume);
    if (RegQueryValueExW(key, L"SendVolume", nullptr, &type, reinterpret_cast<LPBYTE>(&sendVolume),
                         &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        if (sendVolume < 1) sendVolume = 1;
        if (sendVolume > 100) sendVolume = 100;
        g_app.sendVolume = static_cast<int>(sendVolume);
    }
    DWORD flag = 1;
    size = sizeof(flag);
    if (RegQueryValueExW(key, L"SendVolumeAuto", nullptr, &type, reinterpret_cast<LPBYTE>(&flag), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD) {
        g_app.sendVolumeAuto = flag != 0;
    }
    DWORD autoNormalize = 1;
    size = sizeof(autoNormalize);
    if (RegQueryValueExW(key, L"AutoNormalizeRecording", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&autoNormalize), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        g_app.autoNormalizeRecording = autoNormalize != 0;
    }
    RegCloseKey(key);
    if (g_app.audio) g_app.audio->setInputDevice(g_app.inputDevice);
}

void saveSettings() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, L"TapeName", 0, REG_SZ, reinterpret_cast<const BYTE*>(g_app.tapeName.c_str()),
                   static_cast<DWORD>((g_app.tapeName.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(key, L"LastDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(g_app.lastDir.c_str()),
                   static_cast<DWORD>((g_app.lastDir.size() + 1) * sizeof(wchar_t)));
    DWORD baud = static_cast<DWORD>(g_app.baud);
    RegSetValueExW(key, L"Baud", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&baud), sizeof(baud));
    DWORD device = static_cast<DWORD>(g_app.inputDevice);
    RegSetValueExW(key, L"InputDevice", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&device), sizeof(device));
    DWORD sendVolume = static_cast<DWORD>(g_app.sendVolume);
    RegSetValueExW(key, L"SendVolume", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sendVolume),
                   sizeof(sendVolume));
    DWORD sendAuto = g_app.sendVolumeAuto ? 1u : 0u;
    RegSetValueExW(key, L"SendVolumeAuto", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sendAuto),
                   sizeof(sendAuto));
    DWORD normalize = g_app.autoNormalizeRecording ? 1u : 0u;
    RegSetValueExW(key, L"AutoNormalizeRecording", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&normalize), sizeof(normalize));
    RegCloseKey(key);
}

void setStatus(int part, const std::wstring& text) {
    SendMessageW(g_app.status, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
}

void updateButtons() {
    const bool recording = g_app.audio && (g_app.audio->isRecording() || g_app.audio->isPlaying());
    EnableWindow(g_app.buttons[0], !recording);
    EnableWindow(g_app.buttons[1], !recording);
    EnableWindow(g_app.buttons[2], !recording);
    EnableWindow(g_app.buttons[3], recording);
    EnableWindow(g_app.buttons[4], !recording);
}

void logAudioState() {
    if (!g_app.audio) return;
    if (g_app.audio->isPlaying()) {
        setStatus(0, L"送信中");
    } else if (g_app.audio->isRecording()) {
        setStatus(0, g_app.audio->isMonitoring() ? L"入力監視中" : L"録音中");
    } else {
        setStatus(0, L"待機");
    }
}

BOOL CALLBACK tapeDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowTextW(GetDlgItem(hwnd, IDC_TAPENAME), g_app.tapeName.c_str());
            SendDlgItemMessageW(hwnd, IDC_TAPENAME, EM_SETLIMITTEXT, 6, 0);
            CheckRadioButton(hwnd, IDC_BAUD1, IDC_BAUD2, g_app.baud == 2400 ? IDC_BAUD2 : IDC_BAUD1);
            SetDlgItemInt(hwnd, IDC_VOLUME, static_cast<UINT>(g_app.sendVolume), FALSE);
            CheckDlgButton(hwnd, IDC_VOLUME_AUTO, g_app.sendVolumeAuto ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_REC_NORMALIZE,
                           g_app.autoNormalizeRecording ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(hwnd, IDC_VOLUME), g_app.sendVolumeAuto ? FALSE : TRUE);
            HWND combo = GetDlgItem(hwnd, IDC_DEVICE);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            int defaultItem = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                            reinterpret_cast<LPARAM>(L"既定の入力 (WAVE_MAPPER)")));
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(defaultItem),
                         static_cast<LPARAM>(static_cast<INT_PTR>(-1)));
            int selected = defaultItem;
            const UINT count = waveInGetNumDevs();
            for (UINT i = 0; i < count && i < 32; ++i) {
                WAVEINCAPSW caps;
                std::memset(&caps, 0, sizeof(caps));
                if (waveInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
                const int item = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0,
                                                               reinterpret_cast<LPARAM>(caps.szPname)));
                SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(item),
                             static_cast<LPARAM>(static_cast<INT_PTR>(i)));
                if (g_app.inputDevice == i) selected = item;
            }
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
            SetFocus(GetDlgItem(hwnd, IDC_TAPENAME));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_VOLUME_AUTO && HIWORD(wParam) == BN_CLICKED) {
                const bool automatic = IsDlgButtonChecked(hwnd, IDC_VOLUME_AUTO) == BST_CHECKED;
                EnableWindow(GetDlgItem(hwnd, IDC_VOLUME), automatic ? FALSE : TRUE);
                return TRUE;
            }
            if (LOWORD(wParam) == IDOK) {
                wchar_t name[128];
                GetDlgItemTextW(hwnd, IDC_TAPENAME, name, static_cast<int>(sizeof(name) / sizeof(name[0])));
                const std::string utf8Name = wideToUtf8(name);
                msx::Diagnostics diags;
                std::string msxName;
                if (!msx::nameToMsx6(utf8Name, msxName, diags)) {
                    MessageBoxW(hwnd, L"ファイル名は MSX の 6 バイト以内にしてください。\n"
                                      L"（日本語の文字は 2 バイトとして数えます）",
                                L"テープ設定", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                g_app.tapeName = name;
                g_app.baud = IsDlgButtonChecked(hwnd, IDC_BAUD2) == BST_CHECKED ? 2400 : 1200;
                const bool autoVolume = IsDlgButtonChecked(hwnd, IDC_VOLUME_AUTO) == BST_CHECKED;
                BOOL translated = FALSE;
                UINT volume = GetDlgItemInt(hwnd, IDC_VOLUME, &translated, FALSE);
                if (autoVolume) {
                    volume = 100;
                } else if (!translated || volume < 1 || volume > 100) {
                    MessageBoxW(hwnd, L"送信音量は 1～100% の範囲で指定してください。", L"テープ設定",
                                MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                g_app.sendVolume = static_cast<int>(volume);
                g_app.sendVolumeAuto = autoVolume;
                g_app.autoNormalizeRecording =
                    IsDlgButtonChecked(hwnd, IDC_REC_NORMALIZE) == BST_CHECKED;
                HWND combo = GetDlgItem(hwnd, IDC_DEVICE);
                const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
                    g_app.inputDevice = data < 0 ? static_cast<unsigned>(-1) : static_cast<unsigned>(data);
                }
                if (g_app.audio) g_app.audio->setInputDevice(g_app.inputDevice);
                saveSettings();
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        default:
            break;
    }
    return FALSE;
}

bool showTapeSettings() {
    return DialogBoxParamW(g_app.instance, MAKEINTRESOURCEW(IDD_TAPE), g_app.main, tapeDialogProc, 0) == IDOK;
}

bool compileEditor(msx::CompileResult& result) {
    const std::string text = getEditorUtf8();
    msx::CompileOptions options;
    options.profile = &msx::cf2000Profile();
    result = msx::compileProgram(text, options);
    logDiagnostics(result.diags);
    return !result.diags.hasErrors();
}

bool buildTape(msx::CasImage& tape) {
    msx::CompileResult compiled;
    if (!compileEditor(compiled)) return false;
    msx::Diagnostics diags;
    tape = msx::makeBasicTape(msx::toTapeBody(compiled.image, false), wideToUtf8(g_app.tapeName), diags);
    logDiagnostics(diags);
    return !diags.hasErrors();
}

int sendVolumePercent() {
    int percent = g_app.sendVolumeAuto ? 100 : g_app.sendVolume;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    return percent;
}

int sendAmplitude() { return 14745 * sendVolumePercent() / 100; }

void sendToMsx() {
    if (g_app.audio->isRecording() || g_app.audio->isPlaying()) {
        MessageBoxW(g_app.main, L"送信の前に録音・監視・再生を停止してください。", L"送信",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (!showTapeSettings()) return;
    msx::CasImage tape;
    if (!buildTape(tape)) {
        logLine(L"送信を中止しました（コンパイルエラー）。");
        return;
    }
    msx::MemoryWriter writer;
    msx::FskOptions options;
    options.baud = g_app.baud;
    options.amplitude = sendAmplitude();
    msx::Diagnostics diags;
    msx::FskStats stats;
    if (!msx::renderTapeToWav(tape, writer, options, diags, &stats)) {
        logDiagnostics(diags);
        return;
    }
    std::string error;
    if (!g_app.audio->startPlayback(writer.data(), error)) {
        MessageBoxW(g_app.main, utf8ToWide(error).c_str(), L"送信", MB_OK | MB_ICONERROR);
        return;
    }
    logLine(L"送信を開始しました（" + std::to_wstring(g_app.baud) + L" baud、" +
            std::to_wstring(stats.samples / 48000) + L" 秒、音量 " +
            std::to_wstring(sendVolumePercent()) + L"%）。");
    logLine(L"再生の成功は CF-2000 側の受信成功を意味しません。実機で CLOAD / RUN を確認してください。");
    updateButtons();
    logAudioState();
}

void saveSendWav() {
    if (!showTapeSettings()) return;
    const std::wstring path = showSaveDialog(
        g_app.main, L"WAV ファイル (*.wav)\0*.wav\0すべてのファイル (*.*)\0*.*\0\0", L"送信 WAV を保存",
        L"wav", g_app.lastDir, g_app.tapeName + L".wav");
    if (path.empty()) return;
    msx::CasImage tape;
    if (!buildTape(tape)) return;
    Win32FileWriter writer(path);
    if (!writer.isOpen()) {
        MessageBoxW(g_app.main, L"WAV ファイルを作成できません。", L"エラー", MB_OK | MB_ICONERROR);
        return;
    }
    msx::FskOptions options;
    options.baud = g_app.baud;
    options.amplitude = sendAmplitude();
    msx::Diagnostics diags;
    msx::FskStats stats;
    if (!msx::renderTapeToWav(tape, writer, options, diags, &stats)) {
        logDiagnostics(diags);
        MessageBoxW(g_app.main, L"WAV の生成に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return;
    }
    logLine(L"WAV を保存しました: " + path + L"（" + std::to_wstring(stats.samples) + L" サンプル、" +
            std::to_wstring(g_app.baud) + L" baud、音量 " + std::to_wstring(sendVolumePercent()) + L"%）");
}

void startRecording() {
    if (g_app.audio->isPlaying()) {
        MessageBoxW(g_app.main, L"送信の再生中は録音を開始できません。", L"録音", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::wstring path = showSaveDialog(
        g_app.main, L"WAV ファイル (*.wav)\0*.wav\0すべてのファイル (*.*)\0*.*\0\0", L"録音 WAV の保存先",
        L"wav", g_app.lastDir, L"REC.wav");
    if (path.empty()) return;
    std::string error;
    g_app.monitoring = false;
    g_app.lastRecordingPath.clear();
    if (!g_app.audio->startRecording(path, error)) {
        MessageBoxW(g_app.main, utf8ToWide(error).c_str(), L"録音", MB_OK | MB_ICONERROR);
        return;
    }
    g_app.lastRecordingPath = path;
    logLine(L"録音を開始しました: " + path);
    logLine(L"マイクブースト・AGC・ノイズ抑制を無効にし、レベルが -18～-6 dBFS になるよう調整してください。");
    updateButtons();
    logAudioState();
}

double peakDbfsValue(int peak) {
    if (peak <= 0) return -999.0;
    return 20.0 * std::log10(static_cast<double>(peak) / 32768.0);
}

std::wstring normalizedRecordingPath(const std::wstring& path, int suffix) {
    std::wstring base = path;
    std::wstring extension;
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        base = path.substr(0, dot);
        extension = path.substr(dot);
    }
    std::wstring out = base + L".normalized";
    if (suffix > 0) out += L"-" + std::to_wstring(suffix);
    if (extension.empty()) out += L".wav";
    else out += extension;
    return out;
}

void normalizeLatestRecording() {
    if (g_app.lastRecordingPath.empty()) return;
    const std::wstring input = g_app.lastRecordingPath;
    Win32FileReader reader(input);
    if (!reader.isOpen()) {
        logLine(L"録音ファイルを開けないため、正規化コピーを作成しませんでした。");
        return;
    }
    std::wstring output;
    for (int suffix = 0; suffix < 100; ++suffix) {
        const std::wstring candidate = normalizedRecordingPath(input, suffix);
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            output = candidate;
            break;
        }
    }
    if (output.empty()) {
        logLine(L"正規化コピーのファイル名を確保できませんでした。");
        return;
    }
    Win32FileWriter writer(output);
    if (!writer.isOpen()) {
        logLine(L"正規化コピーを作成できません: " + output);
        return;
    }
    setStatus(0, L"録音の正規化中...");
    UpdateWindow(g_app.main);
    HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    msx::Diagnostics diags;
    msx::NormalizeStats stats;
    const bool ok = msx::normalizeWav(reader, writer, msx::NormalizeOptions(), diags, &stats);
    SetCursor(oldCursor);
    logDiagnostics(diags);
    if (!ok) {
        DeleteFileW(output.c_str());
        logLine(L"録音の正規化コピーを作成しませんでした（原音WAVは保持されています）。");
        logAudioState();
        return;
    }
    wchar_t gain[64];
    swprintf(gain, sizeof(gain) / sizeof(gain[0]), L"%+.1f dB", stats.gainDb);
    logLine(L"録音の正規化コピー: " + output);
    logLine(L"  ピーク " + formatDbfs(peakDbfsValue(stats.inputPeak)) + L" → " +
            formatDbfs(peakDbfsValue(stats.outputPeak)) + L"（ゲイン " + gain +
            L"、原音は無加工で保持）");
    logAudioState();
}

void toggleMonitoring() {
    if (g_app.audio->isPlaying()) {
        MessageBoxW(g_app.main, L"送信の再生中は入力監視を開始できません。", L"入力監視",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (g_app.audio->isRecording()) {
        g_app.audio->stopRecording();
        logLine(L"入力監視を停止しました。");
        g_app.monitoring = false;
        updateButtons();
        logAudioState();
        return;
    }
    std::string error;
    if (!g_app.audio->startMonitoring(error)) {
        MessageBoxW(g_app.main, utf8ToWide(error).c_str(), L"入力監視", MB_OK | MB_ICONERROR);
        return;
    }
    g_app.monitoring = true;
    logLine(L"入力レベルを監視しています（録音はしていません）。");
    updateButtons();
    logAudioState();
}

void stopAudio() {
    if (g_app.audio->isRecording()) {
        const bool wasMonitoring = g_app.audio->isMonitoring();
        g_app.audio->stopRecording();
        logLine(wasMonitoring ? L"入力監視を停止しました。" : L"録音を停止しました。");
    }
    if (g_app.audio->isPlaying()) {
        g_app.audio->stopPlayback();
        logLine(L"送信を停止しました。");
    }
    g_app.monitoring = false;
    updateButtons();
    logAudioState();
}

void decodeWav() {
    const std::wstring path = showOpenDialog(g_app.main,
                                             L"WAV ファイル (*.wav)\0*.wav\0すべてのファイル (*.*)\0*.*\0\0",
                                             L"テープ WAV を復号", L"wav", g_app.lastDir);
    if (path.empty()) return;
    Win32FileReader reader(path);
    if (!reader.isOpen()) {
        MessageBoxW(g_app.main, L"WAV を開けません。", L"復号", MB_OK | MB_ICONERROR);
        return;
    }
    logLine(L"復号中: " + path);
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    msx::Diagnostics diags;
    msx::DecodeResult decoded = msx::demodulateWav(reader, msx::DecodeOptions(), diags);
    SetCursor(old);
    logDiagnostics(diags);
    for (std::size_t i = 0; i < decoded.blocks.size(); ++i) {
        const msx::DecodedBlock& block = decoded.blocks[i];
        std::wstring line = L"ブロック " + std::to_wstring(i) + L": " + std::to_wstring(block.bytes.size()) +
                            L" バイト、" + std::to_wstring(block.baud) + L" baud";
        if (block.header) line += L"、ヘッダー " + utf8ToWide(msx::tapeTypeName(block.type));
        if (block.frameErrors > 0) line += L"、フレームエラー " + std::to_wstring(block.frameErrors);
        logLine(line);
    }
    if (decoded.blocks.empty()) {
        MessageBoxW(g_app.main, L"リーダーを検出できませんでした。", L"復号", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!decoded.clean) {
        logLine(L"警告: フレームエラーまたは途中切れがあります。取り出したデータは不完全です。");
    }
    msx::CasImage image = msx::decodedToCas(decoded, diags);
    logDiagnostics(diags);
    const std::wstring base = baseNameWithoutExtension(path);
    const std::wstring casPath = showSaveDialog(
        g_app.main, L"CAS ファイル (*.cas)\0*.cas\0すべてのファイル (*.*)\0*.*\0\0", L"復号した CAS を保存",
        L"cas", g_app.lastDir, base + L"-decoded.cas");
    if (!casPath.empty()) {
        Win32FileWriter writer(casPath);
        if (writer.isOpen()) {
            const msx::Bytes cas = msx::serializeCas(image, true, diags);
            writer.write(cas.empty() ? nullptr : &cas[0], cas.size());
            writer.flush();
            logLine(L"CAS を保存しました: " + casPath);
        }
    }
    int basicIndex = -1;
    int asciiIndex = -1;
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        if (image.files[i].type == msx::TapeFileType::Basic && basicIndex < 0) basicIndex = static_cast<int>(i);
        if (image.files[i].type == msx::TapeFileType::Ascii && asciiIndex < 0) asciiIndex = static_cast<int>(i);
    }
    if (basicIndex < 0 && asciiIndex >= 0) {
        const msx::Bytes& payload = image.files[static_cast<std::size_t>(asciiIndex)].payload;
        std::size_t eof = payload.size();
        for (std::size_t i = 0; i < payload.size(); ++i) {
            if (payload[i] == 0x1A) {
                eof = i;
                break;
            }
        }
        msx::Bytes body(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(eof));
        std::string ascii = msx::charset::decode(body);
        ascii = normalizeToCrlf(ascii);
        const std::wstring asciiPath = showSaveDialog(
            g_app.main, L"BASIC/ASCII ソース (*.bas)\0*.bas\0テキスト (*.txt)\0*.txt\0すべてのファイル (*.*)\0*.*\0\0",
            L"取り出した ASCII を保存", L"bas", g_app.lastDir, base + L".bas");
        if (!asciiPath.empty() && writeFileUtf8Path(asciiPath, ascii)) {
            logLine(L"ASCII データを保存しました: " + asciiPath);
            if (MessageBoxW(g_app.main, L"取り出したテキストをエディターで開きますか？", L"復号",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                openPath(asciiPath);
            }
        }
        return;
    }
    if (basicIndex < 0) {
        logLine(L"BASIC / ASCII ファイルは見つかりませんでした。");
        return;
    }
    msx::ParseImageResult parsed =
        msx::parseProgramImage(image.files[static_cast<std::size_t>(basicIndex)].payload, msx::cf2000Profile());
    logDiagnostics(parsed.diags);
    if (!parsed.ok()) {
        MessageBoxW(g_app.main, L"BASIC の構造が壊れているため、BASIC テキストは保存しません。",
                    L"復号", MB_OK | MB_ICONERROR);
        return;
    }
    msx::TextOptions textOptions;
    textOptions.crlf = true;
    msx::Diagnostics textDiags;
    const std::string text = msx::detokenize(parsed.program, textOptions, textDiags);
    logDiagnostics(textDiags);
    const std::wstring basPath = showSaveDialog(
        g_app.main, L"BASIC ソース (*.bas)\0*.bas\0すべてのファイル (*.*)\0*.*\0\0", L"取り出した BASIC を保存",
        L"bas", g_app.lastDir, base + L".bas");
    if (!basPath.empty() && writeFileUtf8Path(basPath, text)) {
        logLine(L"BASIC を保存しました: " + basPath);
        if (MessageBoxW(g_app.main, L"取り出した BASIC をエディターで開きますか？", L"復号",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            openPath(basPath);
        }
    }
}

bool openPath(const std::wstring& path) {
    std::string text;
    if (!readFileUtf8Path(path, text)) {
        MessageBoxW(g_app.main, L"ファイルを開けません。", L"開く", MB_OK | MB_ICONERROR);
        return false;
    }
    std::string noBom;
    msx::utf8::stripBom(text, noBom);
    {
        std::size_t pos = 0;
        while (pos < noBom.size()) {
            uint32_t cp = 0;
            if (!msx::utf8::decode(noBom, pos, cp)) {
                MessageBoxW(g_app.main, L"UTF-8 として不正なバイト列があります。ファイルを開けません。",
                            L"開く", MB_OK | MB_ICONERROR);
                return false;
            }
        }
    }
    std::wstring wide = utf8ToWide(noBom);
    // Normalize CR/LF for the RichEdit control.
    std::wstring display;
    display.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\r') {
            display.push_back(L'\r');
            if (i + 1 < wide.size() && wide[i + 1] == L'\n') {
                display.push_back(L'\n');
                ++i;
            } else {
                display.push_back(L'\n');
            }
        } else if (wide[i] == L'\n') {
            display.push_back(L'\r');
            display.push_back(L'\n');
        } else {
            display.push_back(wide[i]);
        }
    }
    setEditorText(display);
    g_app.path = path;
    g_app.modified = false;
    const std::size_t slash = path.find_last_of(L"\\/");
    g_app.lastDir = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    updateTitle();
    logLine(L"開きました: " + path);
    return true;
}

bool saveTo(const std::wstring& path) {
    const std::string text = normalizeToCrlf(getEditorUtf8());
    if (!writeFileUtf8Path(path, text)) {
        MessageBoxW(g_app.main, L"ファイルを保存できません。", L"保存", MB_OK | MB_ICONERROR);
        return false;
    }
    g_app.path = path;
    g_app.modified = false;
    const std::size_t slash = path.find_last_of(L"\\/");
    g_app.lastDir = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    updateTitle();
    logLine(L"保存しました: " + path);
    return true;
}

bool saveFile(bool saveAs) {
    std::wstring path = g_app.path;
    if (saveAs || path.empty()) {
        std::wstring suggested = path.empty() ? std::wstring(L"PROGRAM.bas") : path;
        path = showSaveDialog(g_app.main, L"BASIC ソース (*.bas)\0*.bas\0すべてのファイル (*.*)\0*.*\0\0",
                              L"BASIC ソースを保存", L"bas", g_app.lastDir, suggested);
        if (path.empty()) return false;
    }
    return saveTo(path);
}

void newFile() {
    if (!askSaveIfModified()) return;
    setEditorText(L"");
    g_app.path.clear();
    g_app.modified = false;
    updateTitle();
    SetFocus(g_app.editor);
}

void openFile() {
    if (!askSaveIfModified()) return;
    const std::wstring path =
        showOpenDialog(g_app.main, L"BASIC ソース (*.bas)\0*.bas\0すべてのファイル (*.*)\0*.*\0\0",
                       L"BASIC ソースを開く", L"bas", g_app.lastDir);
    if (!path.empty()) openPath(path);
}

void updateMeterAndStatus() {
    if (!g_app.audio) return;
    const bool recording = g_app.audio->isRecording();
    const bool playing = g_app.audio->isPlaying();
    const double inDbfs = g_app.audio->inputDbfs();
    const double outDbfs = g_app.audio->outputDbfs();
    const bool clip = g_app.audio->clipLatched();

    if (recording) {
        const unsigned seconds = g_app.audio->recordSeconds();
        std::wstring state = g_app.audio->isMonitoring() ? L"入力監視中" : L"録音中 ";
        state += std::to_wstring(seconds) + L" 秒";
        setStatus(0, state);
        const DWORD now = GetTickCount();
        if (inDbfs > g_app.inputHoldDb || now > g_app.inputHoldUntil) {
            g_app.inputHoldDb = inDbfs;
            g_app.inputHoldUntil = now + 1000;
        }
        const double shown = g_app.inputHoldDb;
        setStatus(3, clip ? L"入力過大（CLIP）" : (shown <= -998 ? L"信号なし" : formatDbfs(shown)));
        setLevelMeter(g_app.meter, shown, clip, L"入力（マイク）", true);
    } else if (playing) {
        const int percent = static_cast<int>(g_app.audio->playbackProgress() * 100.0);
        setStatus(0, L"送信中 " + std::to_wstring(percent) + L"%");
        setStatus(3, L"");
        setLevelMeter(g_app.meter, outDbfs, false, L"送信（デジタル）", true);
    } else {
        setStatus(0, L"待機");
        setStatus(3, L"");
        g_app.inputHoldDb = -999.0;
        setLevelMeter(g_app.meter, -999.0, false, L"信号なし", false);
    }
    setStatus(4, playing ? (outDbfs <= -998 ? L"出力 −∞ dBFS" : L"出力 " + formatDbfs(outDbfs)) : L"");
    setStatus(5, g_app.path.empty() ? L"(未保存)" : g_app.path);
    if (HMENU menu = GetMenu(g_app.main)) {
        CheckMenuItem(menu, IDM_TAPE_MONITOR,
                      MF_BYCOMMAND | ((g_app.audio->isRecording() && g_app.audio->isMonitoring()) ? MF_CHECKED
                                                                                                : MF_UNCHECKED));
        CheckMenuItem(menu, IDM_TAPE_RECORD,
                      MF_BYCOMMAND | ((g_app.audio->isRecording() && !g_app.audio->isMonitoring()) ? MF_CHECKED
                                                                                                  : MF_UNCHECKED));
    }
}

int utf16LengthFromUtf8(const std::string& text, std::size_t byteOffset) {
    std::size_t i = 0;
    int len = 0;
    while (i < byteOffset && i < text.size()) {
        const std::size_t before = i;
        uint32_t cp = 0;
        msx::utf8::decode(text, i, cp);
        len += (cp > 0xFFFF) ? 2 : 1;
        if (i == before) break;
    }
    return len;
}

bool editorImeOpen() {
    if (!g_app.richEdit || !g_app.editor) return false;
    HIMC context = ImmGetContext(g_app.editor);
    if (!context) return false;
    const bool open = ImmGetOpenStatus(context) != 0;
    ImmReleaseContext(g_app.editor, context);
    return open;
}

void updateCaretInfo();

void highlightNow() {
    KillTimer(g_app.main, 1);
    if (!g_app.richEdit || g_app.highlighting) return;
    if (editorImeOpen()) return;
    const std::wstring wideLf = getEditorWideLf();
    if (wideLf.size() > 120000) return;
    const std::string text = wideToUtf8(wideLf);
    if (text.empty()) return;
    msx::LexResult lex = msx::lexSource(text, msx::cf2000Profile());
    if (lex.tokens.empty()) return;

    g_app.highlighting = true;
    CHARRANGE saved;
    SendMessageW(g_app.editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    SendMessageW(g_app.editor, WM_SETREDRAW, FALSE, 0);

    CHARRANGE all = {0, -1};
    SendMessageW(g_app.editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    CHARFORMATW cf;
    std::memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = 0;
    cf.crTextColor = RGB(0, 0, 0);
    SendMessageW(g_app.editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        const msx::LexToken& token = lex.tokens[i];
        if (token.kind == msx::LexKind::EndOfLine || token.length == 0) continue;
        COLORREF color = RGB(0, 0, 0);
        bool bold = false;
        switch (token.kind) {
            case msx::LexKind::LineNumber: color = RGB(128, 128, 128); break;
            case msx::LexKind::Integer:
            case msx::LexKind::Float:
            case msx::LexKind::Hex:
            case msx::LexKind::Octal:
            case msx::LexKind::Binary: color = RGB(128, 0, 128); break;
            case msx::LexKind::String: color = RGB(163, 21, 21); break;
            case msx::LexKind::Comment: color = RGB(0, 128, 0); break;
            case msx::LexKind::Data: color = RGB(128, 80, 0); break;
            case msx::LexKind::Keyword: {
                color = RGB(0, 0, 180);
                const msx::Keyword* kw =
                    token.tokenCode ? msx::keywordByCode(token.tokenCode) : nullptr;
                bold = !(kw && (kw->flags & msx::KF_Function));
                break;
            }
            case msx::LexKind::Operator: color = RGB(0, 112, 112); break;
            case msx::LexKind::Separator: color = RGB(96, 96, 96); break;
            case msx::LexKind::Variable: color = RGB(0, 90, 90); break;
            default: color = RGB(0, 0, 0); break;
        }
        if (color == RGB(0, 0, 0)) continue;
        const int start = utf16LengthFromUtf8(text, token.offset);
        const int end = utf16LengthFromUtf8(text, token.offset + token.length);
        if (end <= start) continue;
        CHARRANGE range = {start, end};
        SendMessageW(g_app.editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.dwEffects = bold ? CFE_BOLD : 0;
        cf.crTextColor = color;
        SendMessageW(g_app.editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }

    SendMessageW(g_app.editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&saved));
    SendMessageW(g_app.editor, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_app.editor, nullptr, FALSE);
    g_app.highlighting = false;
    updateCaretInfo();
}

bool editorWordChar(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
           c == L'_' || c == L'$' || c == L'%' || c == L'!' || c == L'.' || c == L'#';
}

void updateCaretInfo() {
    if (!g_app.editor || !g_app.status) return;
    if (g_app.highlighting) return;
    CHARRANGE sel;
    SendMessageW(g_app.editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    const int line = static_cast<int>(SendMessageW(g_app.editor, EM_LINEFROMCHAR,
                                                   static_cast<WPARAM>(sel.cpMin), 0)) + 1;
    const int lineIndex = static_cast<int>(SendMessageW(g_app.editor, EM_LINEINDEX,
                                                        static_cast<WPARAM>(line - 1), 0));
    const int column = (lineIndex >= 0 && sel.cpMin >= lineIndex) ? sel.cpMin - lineIndex + 1 : 1;
    setStatus(1, L"行 " + std::to_wstring(line) + L": " + std::to_wstring(column));

    if (!g_app.richEdit) return;
    const std::wstring wide = getEditorWideLf();
    if (wide.size() > 200000) {
        setStatus(2, L"");
        return;
    }
    const std::string full = wideToUtf8(wide);
    const std::size_t caretByte =
        wideToUtf8(wide.substr(0, static_cast<std::size_t>(sel.cpMin))).size();
    const std::string hint = msx::argumentHintAt(full, caretByte, msx::cf2000Profile());
    setStatus(2, hint.empty() ? L"" : utf8ToWide(hint));
}

void hideCompletion() {
    if (g_app.popup) ShowWindow(g_app.popup, SW_HIDE);
    g_app.completions.clear();
}

void showCompletion(bool requirePrefix = false) {
    if (!g_app.richEdit) return;
    CHARRANGE range;
    SendMessageW(g_app.editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    const std::wstring wide = getEditorWideLf();
    if (range.cpMin < 0 || static_cast<std::size_t>(range.cpMin) > wide.size()) return;
    int wordStart = range.cpMin;
    while (wordStart > 0) {
        const wchar_t c = wide[static_cast<std::size_t>(wordStart - 1)];
        if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
            c == L'_' || c == L'$' || c == L'%' || c == L'!' || c == L'.' || c == L'#') {
            --wordStart;
        } else {
            break;
        }
    }
    if (requirePrefix && wordStart >= range.cpMin) {
        hideCompletion();
        return;
    }
    const std::string full = wideToUtf8(wide);
    const std::size_t caretByte = wideToUtf8(wide.substr(0, static_cast<std::size_t>(range.cpMin))).size();
    std::vector<msx::CompletionItem> items = msx::completeAt(full, caretByte, msx::cf2000Profile());
    if (items.empty()) {
        hideCompletion();
        return;
    }
    if (items.size() > 24) items.resize(24);
    g_app.completions = items;
    g_app.completionWordStart = wordStart;
    g_app.completionCaret = range.cpMin;

    SendMessageW(g_app.popup, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::wstring label = utf8ToWide(items[i].label);
        if (!items[i].detail.empty()) {
            label += L"  ";
            label += utf8ToWide(items[i].detail);
        }
        SendMessageW(g_app.popup, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(g_app.popup, LB_SETCURSEL, 0, 0);

    POINT pt;
    GetCaretPos(&pt);
    MapWindowPoints(g_app.editor, g_app.main, &pt, 1);
    RECT client;
    GetClientRect(g_app.main, &client);
    const int itemHeight = 16;
    const int visible = std::min<int>(10, static_cast<int>(items.size()));
    int width = 240;
    int height = visible * itemHeight + 4;
    int x = pt.x;
    int y = pt.y + 16;
    if (x + width > client.right) x = client.right - width;
    if (y + height > client.bottom) y = pt.y - height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(g_app.popup, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void acceptCompletion() {
    if (!g_app.popup || g_app.completions.empty()) return;
    const int sel = static_cast<int>(SendMessageW(g_app.popup, LB_GETCURSEL, 0, 0));
    if (sel < 0 || static_cast<std::size_t>(sel) >= g_app.completions.size()) {
        hideCompletion();
        return;
    }
    const std::wstring insert = utf8ToWide(g_app.completions[static_cast<std::size_t>(sel)].insertText);
    CHARRANGE range;
    range.cpMin = g_app.completionWordStart;
    range.cpMax = g_app.completionCaret;
    SendMessageW(g_app.editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(g_app.editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(insert.c_str()));
    hideCompletion();
    SetFocus(g_app.editor);
}

LRESULT CALLBACK editorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP_AUTOCOMPLETE) {
        if (GetFocus() == hwnd) showCompletion(true);
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        const bool popupVisible = g_app.popup && IsWindowVisible(g_app.popup);
        if (wParam == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
            // Do not steal Ctrl+Space from the Japanese IME while composing.
            if (!editorImeOpen()) {
                showCompletion();
                return 0;
            }
        }
        if (popupVisible) {
            if (wParam == VK_ESCAPE) {
                hideCompletion();
                return 0;
            }
            if (wParam == VK_UP || wParam == VK_DOWN) {
                const int count = static_cast<int>(SendMessageW(g_app.popup, LB_GETCOUNT, 0, 0));
                int sel = static_cast<int>(SendMessageW(g_app.popup, LB_GETCURSEL, 0, 0));
                if (wParam == VK_UP) sel = sel <= 0 ? count - 1 : sel - 1;
                if (wParam == VK_DOWN) sel = sel + 1 >= count ? 0 : sel + 1;
                SendMessageW(g_app.popup, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
                return 0;
            }
            if (wParam == VK_RETURN || wParam == VK_TAB) {
                acceptCompletion();
                return 0;
            }
        }
    } else if (msg == WM_CHAR) {
        const wchar_t c = static_cast<wchar_t>(wParam);
        const bool popupVisible = g_app.popup && IsWindowVisible(g_app.popup);
        const bool digit = c >= L'0' && c <= L'9';
        if (editorWordChar(c) && (!digit || popupVisible) && !editorImeOpen()) {
            PostMessageW(hwnd, WM_APP_AUTOCOMPLETE, 0, 0);
        } else {
            hideCompletion();
        }
    } else if (msg == WM_LBUTTONDOWN) {
        hideCompletion();
    } else if (msg == WM_KILLFOCUS) {
        hideCompletion();
    }
    return CallWindowProcW(g_editorProc, hwnd, msg, wParam, lParam);
}

void createEditor(HWND parent) {
    HMODULE rich = LoadLibraryW(L"riched20.dll");
    if (rich) {
        g_app.editor = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"RichEdit20W", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                ES_NOHIDESEL | WS_TABSTOP,
            0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_EDITOR), g_app.instance, nullptr);
        g_app.richEdit = g_app.editor != nullptr;
    }
    if (!g_app.editor) {
        g_app.editor = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_TABSTOP, 0, 0,
            100, 100, parent, reinterpret_cast<HMENU>(IDC_EDITOR), g_app.instance, nullptr);
        g_app.richEdit = false;
    }
    SendMessageW(g_app.editor, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.monoFont), TRUE);
    if (g_app.richEdit) {
        SendMessageW(g_app.editor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        SendMessageW(g_app.editor, EM_EXLIMITTEXT, 0, 1024 * 1024);
        SendMessageW(g_app.editor, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
        g_editorProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_app.editor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editorProc)));
    }
    SendMessageW(g_app.editor, EM_SETLIMITTEXT, 1024 * 1024, 0);
}

void layout(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    int statusHeight = 0;
    if (g_app.status) {
        RECT statusRect;
        GetWindowRect(g_app.status, &statusRect);
        statusHeight = statusRect.bottom - statusRect.top;
    }

    const int toolbarHeight = 34;
    const int meterHeight = 26;
    const int editorTop = toolbarHeight + meterHeight + 6;
    const int outputHeight = 110;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    int editorHeight = height - editorTop - outputHeight - statusHeight - 6;
    if (editorHeight < 80) editorHeight = 80;

    int x = 6;
    for (int i = 0; i < 5; ++i) {
        SetWindowPos(g_app.buttons[i], nullptr, x, 5, 72, 24, SWP_NOZORDER);
        x += 76;
    }
    SetWindowPos(g_app.meter, nullptr, 6, toolbarHeight + 1, width - 12, meterHeight, SWP_NOZORDER);
    SetWindowPos(g_app.editor, nullptr, 6, editorTop, width - 12, editorHeight, SWP_NOZORDER);
    SetWindowPos(g_app.output, nullptr, 6, editorTop + editorHeight + 3, width - 12, outputHeight,
                 SWP_NOZORDER);
    SendMessageW(g_app.status, WM_SIZE, 0, 0);
}

void createChildren(HWND hwnd) {
    g_app.buttons[0] = CreateWindowExW(0, L"BUTTON", L"検証 (F5)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
                                       72, 24, hwnd, reinterpret_cast<HMENU>(IDB_VALIDATE), g_app.instance,
                                       nullptr);
    g_app.buttons[1] = CreateWindowExW(0, L"BUTTON", L"送信 (F6)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
                                       72, 24, hwnd, reinterpret_cast<HMENU>(IDB_SEND), g_app.instance, nullptr);
    g_app.buttons[2] = CreateWindowExW(0, L"BUTTON", L"録音 (F7)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
                                       72, 24, hwnd, reinterpret_cast<HMENU>(IDB_RECORD), g_app.instance,
                                       nullptr);
    g_app.buttons[3] = CreateWindowExW(0, L"BUTTON", L"停止", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 72,
                                       24, hwnd, reinterpret_cast<HMENU>(IDB_STOP), g_app.instance, nullptr);
    g_app.buttons[4] = CreateWindowExW(0, L"BUTTON", L"復号 (F8)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
                                       72, 24, hwnd, reinterpret_cast<HMENU>(IDB_DECODE), g_app.instance,
                                       nullptr);
    for (int i = 0; i < 5; ++i) SendMessageW(g_app.buttons[i], WM_SETFONT, reinterpret_cast<WPARAM>(g_app.uiFont), TRUE);

    g_app.status = CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", hwnd, IDC_STATUS);
    SendMessageW(g_app.status, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.uiFont), TRUE);
    int parts[6] = {150, 100, 320, 110, 110, -1};
    SendMessageW(g_app.status, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));

    g_app.meter = createLevelMeter(hwnd, g_app.instance, IDC_METER);

    createEditor(hwnd);

    g_app.output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                       ES_READONLY,
                                   0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_OUTPUT), g_app.instance,
                                   nullptr);
    SendMessageW(g_app.output, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.monoFont), TRUE);

    g_app.popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LISTBOX", L"",
                                  WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 0, 0, 100, 100, hwnd, nullptr,
                                  g_app.instance, nullptr);
    SendMessageW(g_app.popup, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.monoFont), TRUE);
}

void onCommand(HWND hwnd, int id, int notifyCode, HWND control) {
    if (control == g_app.editor && notifyCode == EN_SELCHANGE) {
        updateCaretInfo();
        return;
    }
    if (control == g_app.editor && notifyCode == EN_CHANGE) {
        if (!g_app.ignoreChange) {
            g_app.modified = true;
            updateTitle();
            SetTimer(hwnd, 1, 300, nullptr);
        }
        return;
    }
    switch (id) {
        case IDM_FILE_NEW:
            newFile();
            break;
        case IDM_FILE_OPEN:
            openFile();
            break;
        case IDM_FILE_SAVE:
            saveFile(false);
            break;
        case IDM_FILE_SAVE_AS:
            saveFile(true);
            break;
        case IDM_FILE_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case IDM_EDIT_UNDO:
            SendMessageW(g_app.editor, WM_UNDO, 0, 0);
            break;
        case IDM_EDIT_REDO:
            if (g_app.richEdit) SendMessageW(g_app.editor, EM_REDO, 0, 0);
            break;
        case IDM_EDIT_CUT:
            SendMessageW(g_app.editor, WM_CUT, 0, 0);
            break;
        case IDM_EDIT_COPY:
            SendMessageW(g_app.editor, WM_COPY, 0, 0);
            break;
        case IDM_EDIT_PASTE:
            SendMessageW(g_app.editor, WM_PASTE, 0, 0);
            break;
        case IDM_EDIT_SELECT_ALL:
            SendMessageW(g_app.editor, EM_SETSEL, 0, -1);
            break;
        case IDM_EDIT_COMPLETE:
            SetFocus(g_app.editor);
            showCompletion();
            break;
        case IDB_VALIDATE:
        case IDM_RUN_VALIDATE: {
            g_app.logText.clear();
            SetWindowTextW(g_app.output, L"");
            msx::CompileResult result;
            if (compileEditor(result)) {
                logLine(L"検証 OK: " + std::to_wstring(result.lines.size()) + L" 行、" +
                        std::to_wstring(result.image.size()) + L" バイト（変数・配列を除く）");
                setStatus(0, L"検証 OK");
            } else {
                logLine(L"検証 NG: エラーを修正してください。");
                setStatus(0, L"検証 NG");
            }
            break;
        }
        case IDM_RUN_SEND:
        case IDB_SEND:
            sendToMsx();
            break;
        case IDM_RUN_STOP:
        case IDB_STOP:
            stopAudio();
            break;
        case IDM_RUN_CLEAR_LOG:
            g_app.logText.clear();
            SetWindowTextW(g_app.output, L"");
            break;
        case IDM_TAPE_SETTINGS:
            showTapeSettings();
            break;
        case IDM_TAPE_SAVE_WAV:
            saveSendWav();
            break;
        case IDM_TAPE_RECORD:
        case IDB_RECORD:
            if (g_app.audio->isRecording() || g_app.audio->isPlaying()) {
                stopAudio();
            } else {
                startRecording();
            }
            break;
        case IDM_TAPE_MONITOR:
            toggleMonitoring();
            break;
        case IDM_TAPE_RESET_CLIP:
            g_app.audio->resetClip();
            g_app.inputHoldDb = -999.0;
            logLine(L"入力のピークホールドと CLIP 表示を解除しました。");
            break;
        case IDM_TAPE_MIXER:
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", L"sndvol32.exe", L"/r", nullptr,
                                                        SW_SHOWNORMAL)) <= 32) {
                MessageBoxW(hwnd, L"録音ミキサー (sndvol32.exe /r) を開けませんでした。\n"
                                  L"Windows の音量コントロールから録音タブを開いてください。",
                            L"録音ミキサー", MB_OK | MB_ICONWARNING);
            }
            break;
        case IDM_TAPE_DECODE:
        case IDB_DECODE:
            decodeWav();
            break;
        case IDM_HELP_GUIDE:
            MessageBoxW(
                hwnd,
                L"1. .bas を開いて編集します（UTF-8、行番号必須）。\n"
                L"2. F5 で検証し、エラー・警告・空き容量をログで確認します。\n"
                L"3. F6 でテープ設定を確認し、PC の音声出力から CF-2000 へ送信します。\n"
                L"   CF-2000 側は CLOAD\"名前\" で待機させてください。\n"
                L"4. F7 で録音します。入力は 48 kHz / 16 bit / モノラルです。\n"
                L"5. F8 で WAV を復号し、CAS と BASIC を取り出します。\n\n"
                L"送信完了は実機の受信成功を意味しません。RUN / LIST で確認してください。\n"
                L"漢字は CF-2000 の文字コードにないため、テープへは出力できません。",
                L"使い方", MB_OK | MB_ICONINFORMATION);
            break;
        case IDM_HELP_ABOUT:
            MessageBoxW(hwnd,
                        L"MSX CF-2000 Studio 0.1.0\n"
                        L"National CF-2000（日本語 MSX1 / MSX BASIC 1.0 / 16 KB）向け\n"
                        L"BASIC 編集・トークン化・FSK 変復調・WAV 録音\n\n"
                        L"仕様: MSX-CF-2000-technical-spec.md\n"
                        L"文字コード: openMSX MSXVIDJP.TXT (2021-11-21) 準拠\n"
                        L"実機・XP での受入試験は未実施です（プレビュー版）。",
                        L"バージョン情報", MB_OK | MB_ICONINFORMATION);
            break;
        default:
            break;
    }
    if (id == IDB_VALIDATE || id == IDB_SEND || id == IDB_RECORD || id == IDB_STOP || id == IDB_DECODE) {
        if (g_app.editor) SetFocus(g_app.editor);
    }
}

LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_app));
            g_app.main = hwnd;
            g_app.instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
            HDC dc = GetDC(hwnd);
            const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
            ReleaseDC(hwnd, dc);
            g_app.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            g_app.monoFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                         FIXED_PITCH | FF_MODERN, L"ＭＳ ゴシック");
            if (!g_app.monoFont) {
                g_app.monoFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"MS Gothic");
            }
            g_app.audio = new AudioEngine(hwnd);
            createChildren(hwnd);
            layout(hwnd);
            updateCaretInfo();
            PostMessageW(hwnd, WM_SIZE, 0, 0);
            loadSettings();
            updateTitle();
            updateButtons();
            SetTimer(hwnd, 2, 66, nullptr);
            logLine(L"MSX CF-2000 Studio 0.1.0");
            logLine(L"実機・XP での受入試験は未実施のプレビュー版です。送信完了は受信成功ではありません。");
            return 0;
        }
        case WM_SIZE:
            layout(hwnd);
            return 0;
        case WM_SETFOCUS:
            if (g_app.editor) SetFocus(g_app.editor);
            return 0;
        case WM_TIMER:
            if (wParam == 1) {
                highlightNow();
            } else if (wParam == 2) {
                updateMeterAndStatus();
            }
            return 0;
        case WM_COMMAND:
            onCommand(hwnd, LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_APP_AUDIO_DONE:
            if (static_cast<AudioOutcome>(wParam) == AudioOutcome::PlayDone) {
                logLine(L"送信の再生が終了しました（実機の受信成功ではありません）。");
            } else {
                logLine(L"録音を終了しました（" + std::to_wstring(g_app.audio->recordSeconds()) + L" 秒）。");
                if (g_app.autoNormalizeRecording && !g_app.lastRecordingPath.empty()) {
                    normalizeLatestRecording();
                }
            }
            updateButtons();
            logAudioState();
            return 0;
        case WM_APP_AUDIO_ERROR: {
            const std::string error = g_app.audio->lastError();
            if (!error.empty()) {
                logLine(L"音声エラー: " + utf8ToWide(error));
                MessageBoxW(hwnd, utf8ToWide(error).c_str(), L"音声", MB_OK | MB_ICONERROR);
            }
            updateButtons();
            logAudioState();
            return 0;
        }
        case WM_CLOSE:
            if (!askSaveIfModified()) return 0;
            if (g_app.audio) g_app.audio->stopAll();
            saveSettings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);
            delete g_app.audio;
            g_app.audio = nullptr;
            if (g_app.monoFont) DeleteObject(g_app.monoFont);
            PostQuitMessage(0);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 800;
            info->ptMinTrackSize.y = 600;
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    g_app.instance = instance;

    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = mainProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAIN_MENU);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"MSX CF-2000 Studio", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 900, 640, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);
    if (g_app.editor) SetFocus(g_app.editor);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) openPath(argv[1]);
        LocalFree(argv);
    }

    HACCEL accel = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCELERATORS));
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (accel && TranslateAcceleratorW(hwnd, accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
