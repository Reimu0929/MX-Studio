#include "meter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct MeterState {
    double dbfs = -999.0;
    bool clip = false;
    bool active = false;
    std::wstring label = L"信号なし";
};

const wchar_t* kMeterClass = L"MsxCf2000LevelMeter";

std::wstring formatDbfs(double dbfs) {
    if (dbfs <= -998.0) return L"−∞ dBFS";
    wchar_t buf[64];
    swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%.1f dBFS", dbfs);
    return buf;
}

LRESULT CALLBACK meterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MeterState* state = reinterpret_cast<MeterState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            state = new MeterState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            return 0;
        }
        case WM_DESTROY:
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH back = CreateSolidBrush(RGB(236, 233, 216));
            FillRect(dc, &rc, back);
            DeleteObject(back);

            RECT border = rc;
            DrawEdge(dc, &border, EDGE_SUNKEN, BF_RECT);
            InflateRect(&rc, -4, -4);
            const int labelWidth = 92;
            RECT bar = rc;
            bar.left += labelWidth;

            HBRUSH off = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &bar, off);
            DeleteObject(off);

            if (state && state->active) {
                const double dbfs = state->dbfs;
                const double minDb = -48.0;
                const double maxDb = 0.0;
                const double clamped = std::max(minDb, std::min(maxDb, dbfs));
                const int width = bar.right - bar.left - 2;
                const int filled = dbfs <= -998.0 ? 0 : static_cast<int>((clamped - minDb) / (maxDb - minDb) * width);
                RECT fill = bar;
                fill.left += 1;
                fill.right = fill.left + filled;
                fill.top += 1;
                fill.bottom -= 1;
                COLORREF color = RGB(0, 160, 0);
                if (dbfs > -6.0) color = RGB(220, 180, 0);
                if (dbfs > -3.0) color = RGB(200, 0, 0);
                HBRUSH brush = CreateSolidBrush(color);
                FillRect(dc, &fill, brush);
                DeleteObject(brush);

                // Scale marks at -36, -24, -12, -6, -3 dB.
                const double marks[] = {-36, -24, -12, -6, -3};
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
                HGDIOBJ oldPen = SelectObject(dc, pen);
                for (int i = 0; i < 5; ++i) {
                    const int x = bar.left + 1 + static_cast<int>((marks[i] - minDb) / (maxDb - minDb) * width);
                    MoveToEx(dc, x, bar.top + 1, nullptr);
                    LineTo(dc, x, bar.bottom - 1);
                }
                SelectObject(dc, oldPen);
                DeleteObject(pen);

                if (state->clip) {
                    RECT clipRect = bar;
                    clipRect.left = clipRect.right - 44;
                    HBRUSH red = CreateSolidBrush(RGB(200, 0, 0));
                    FillRect(dc, &clipRect, red);
                    DeleteObject(red);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(255, 255, 255));
                    DrawTextW(dc, L"CLIP", -1, &clipRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0, 0, 0));
            RECT left = rc;
            left.right = bar.left - 2;
            DrawTextW(dc, state ? state->label.c_str() : L"信号なし", -1, &left,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (state && state->active) {
                const std::wstring value = formatDbfs(state->dbfs);
                RECT valueRect = bar;
                valueRect.left = bar.right - 110;
                valueRect.right -= 2;
                SetTextColor(dc, RGB(0, 0, 0));
                DrawTextW(dc, value.c_str(), -1, &valueRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

HWND createLevelMeter(HWND parent, HINSTANCE instance, int controlId) {
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = meterProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kMeterClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, kMeterClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), instance, nullptr);
}

void setLevelMeter(HWND meter, double dbfs, bool clip, const std::wstring& label, bool active) {
    if (!meter) return;
    MeterState* state = reinterpret_cast<MeterState*>(GetWindowLongPtrW(meter, GWLP_USERDATA));
    if (!state) return;
    state->dbfs = dbfs;
    state->clip = clip;
    state->label = label;
    state->active = active;
    InvalidateRect(meter, nullptr, FALSE);
}
