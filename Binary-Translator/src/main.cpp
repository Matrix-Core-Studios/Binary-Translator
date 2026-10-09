#include <string>
#include <sstream>
#include <vector>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <windowsx.h>

using namespace std;

namespace Col {
const COLORREF bg      = RGB(0x14, 0x15, 0x1D);
const COLORREF panel   = RGB(0x1D, 0x1E, 0x2B);
const COLORREF border  = RGB(0x2E, 0x30, 0x42);
const COLORREF accent  = RGB(0x4C, 0xC2, 0xFF);
const COLORREF logoink = RGB(0x0E, 0x14, 0x1E);
const COLORREF text    = RGB(0xE9, 0xEB, 0xF4);
const COLORREF dim     = RGB(0x8B, 0x90, 0xA6);
const COLORREF green   = RGB(0x66, 0xE0, 0x8C);
const COLORREF yellow  = RGB(0xF2, 0xC9, 0x4C);
const COLORREF red     = RGB(0xFF, 0x76, 0x76);
}

struct TextFmt {
    UINT cbSize; DWORD dwMask; DWORD dwEffects; LONG yHeight; LONG yOffset;
    COLORREF crTextColor; BYTE bCharSet; BYTE bPitchAndFamily; WCHAR szFaceName[LF_FACESIZE];
};
static_assert(sizeof(TextFmt) == 92, "bad charformat size");

struct GetLenEx { DWORD flags; UINT codepage; };

#define MSG_SETBKGND   (WM_USER + 67)
#define MSG_SETCFMT    (WM_USER + 68)
#define MSG_TEXTLENEX  (WM_USER + 95)
#define SCF_SEL        0x0001
#define CFM_COLORX     0x40000000
#define CFM_BOLDX      0x00000001
#define CFE_BOLDX      0x00000001

typedef HRESULT (WINAPI *PFNDwmAttr)(HWND, DWORD, LPCVOID, DWORD);
typedef BOOL (WINAPI *PFNDpiAware)(void);
typedef int (WINAPI *PFNAppMode)(int);

static HINSTANCE gInst;
static HWND gWnd, gIn, gOut, gPlace;
static bool gOutRich = false;
static HFONT gFntTitle, gFntSub, gFntTab, gFntTabB, gFntMono, gFntSmall, gFntSmallB, gFntLogo;
static HBRUSH gBrBg, gBrPanel;
static int gDpi = 96, gMode = 0;
static RECT gInBox, gOutBox, gTabR[5], gCopyR, gLogoR, gTitleR, gSubR;
static int gTabW[5];
static float gIndX, gIndW, gIndTX, gIndTW;
static bool gIndSet = false;
static float gHov[5], gPulse = 0, gCopyHov = 0, gCopyFlash = 0;
static int gHovIdx = -1;
static bool gCopyDown = false, gCopyOver = false, gTmrOn = false;
static int gDownTab = -1;
static wstring gStats;
static WNDPROC gInOldProc;

static const wchar_t* TAB_LBL[5] = {
    L"Text \u2192 Binary", L"Binary \u2192 Text", L"Number \u2192 Binary",
    L"Binary \u2192 Number", L"Help"
};
static const wchar_t* HINTS[5] = {
    L"type text on the left \u2014 binary appears instantly",
    L"paste 0s and 1s on the left \u2014 text appears instantly",
    L"type a decimal number on the left",
    L"paste binary digits on the left",
    L"read the quick guide on the right"
};
static const wchar_t* PLACE[5] = {
    L"Type any text \u2014 e.g.  Hello 123",
    L"Paste binary \u2014 e.g.  01001000 01101001",
    L"Type a whole number \u2014 e.g.  255  or  -12",
    L"Paste binary digits \u2014 e.g.  11111111",
    L"Guide mode \u2014 input is not needed"
};

struct Seg { wstring t; COLORREF c; bool bold; };

static int S(int v) { return MulDiv(v, gDpi, 96); }

static COLORREF mix(COLORREF a, COLORREF b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    int r = (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5f);
    int g = (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5f);
    int bl = (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5f);
    return RGB(r, g, bl);
}

static void beginAnim() {
    if (!gWnd) return;
    if (!gTmrOn) { SetTimer(gWnd, 1, 16, 0); gTmrOn = true; }
}

static wstring getW(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    wstring s;
    s.resize(n + 1);
    int got = GetWindowTextW(h, &s[0], n + 1);
    s.resize(got < 0 ? 0 : got);
    return s;
}

static string toU8(const wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    string s;
    s.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}

static wstring toW(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), 0, 0);
    wstring w;
    w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

static vector<string> splitSpaces(const string& s) {
    vector<string> out;
    istringstream is(s);
    string t;
    while (is >> t) out.push_back(t);
    return out;
}

static string textToBinary(const string& text) {
    ostringstream os;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = (unsigned char)text[i];
        for (int b = 7; b >= 0; --b) os << ((c >> b) & 1);
        if (i + 1 < text.size()) os << ' ';
    }
    return os.str();
}

static bool binaryToText(const string& in, string& out, string& err) {
    string bits;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        bits += (char)c;
    }
    if (bits.empty()) { err = "No binary digits found."; return false; }
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != '0' && bits[i] != '1') {
            err = "Invalid character '" + string(1, bits[i]) + "' at position " + to_string(i + 1) +
                " - only 0 and 1 are allowed.";
            return false;
        }
    }
    if (bits.size() % 8 != 0) {
        err = "Bit count must be a multiple of 8 (1 character = 8 bits). You entered " +
            to_string(bits.size()) + " bits.";
        return false;
    }
    out.clear();
    for (size_t i = 0; i < bits.size(); i += 8) {
        unsigned val = 0;
        for (int j = 0; j < 8; j++) val = (val << 1) | (unsigned)(bits[i + j] - '0');
        out += (char)val;
    }
    return true;
}

static bool parseWholeNumber(const string& in, bool& neg, unsigned long long& v, string& err) {
    size_t i = 0;
    neg = false;
    if (in.size() > 0 && (in[0] == '-' || in[0] == '+')) { neg = (in[0] == '-'); i = 1; }

    if (i >= in.size()) { err = "That is not a valid whole number. Example: 65 or -12"; return false; }
    for (size_t j = i; j < in.size(); j++)
        if (!isdigit((unsigned char)in[j])) { err = "That is not a valid whole number. Example: 65 or -12"; return false; }

    unsigned long long acc = 0;
    for (size_t j = i; j < in.size(); j++) {
        unsigned d = (unsigned)(in[j] - '0');
        if (acc > (ULLONG_MAX - d) / 10ULL) {
            err = "Number is too large. Maximum is 18,446,744,073,709,551,615.";
            return false;
        }
        acc = acc * 10ULL + d;
    }
    if (neg && acc > 9223372036854775808ULL) {
        err = "Negative number is below the 64-bit minimum (-9,223,372,036,854,775,808).";
        return false;
    }
    if (acc == 0) neg = false;
    v = acc;
    return true;
}

static string ullToBinary(unsigned long long v) {
    if (v == 0) return "0";
    string s;
    while (v) { s = string(1, char('0' + (v & 1))) + s; v >>= 1; }
    return s;
}

static string ullToHex(unsigned long long v) {
    if (v == 0) return "0";
    const char* d = "0123456789ABCDEF";
    string s;
    while (v) { s = string(1, d[v & 0xF]) + s; v >>= 4; }
    return s;
}

static string ullToOct(unsigned long long v) {
    if (v == 0) return "0";
    string s;
    while (v) { s = string(1, char('0' + (v & 7))) + s; v >>= 3; }
    return s;
}

static bool bitsToValue(const string& in, string& bits, unsigned long long& v, string& err) {
    bits.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        bits += (char)c;
    }
    if (bits.empty()) { err = "No binary digits found."; return false; }
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != '0' && bits[i] != '1') {
            err = "Invalid character '" + string(1, bits[i]) + "' at position " + to_string(i + 1) +
                " - only 0 and 1 are allowed.";
            return false;
        }
    }
    if (bits.size() > 64) {
        err = "Too many bits - maximum is 64. You entered " + to_string(bits.size()) + " bits.";
        return false;
    }
    unsigned long long acc = 0;
    for (size_t i = 0; i < bits.size(); ++i) acc = (acc << 1) | (unsigned long long)(bits[i] - '0');
    v = acc;
    return true;
}

static string sanitizeDisplay(const string& s) {
    string r;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127) r += "\xC2\xB7";
        else r += s[i];
    }
    return r;
}

static string charLabel(unsigned char c) {
    if (c == ' ') return "' '";
    if (c > 32 && c < 127) return string("'") + (char)c + "'";
    char buf[8];
    wsprintfA(buf, "0x%02X", (int)c);
    return string(buf);
}

static string group4(const string& b) {
    string r;
    for (size_t i = 0; i < b.size(); i++) {
        if (i && i % 4 == 0) r += ' ';
        r += b[i];
    }
    return r;
}

static long outLen() {
    GetLenEx g;
    g.flags = 0x2 | 0x8;
    g.codepage = 1200;
    return (long)SendMessageW(gOut, MSG_TEXTLENEX, (WPARAM)&g, 0);
}

static void outClear() {
    SendMessageW(gOut, WM_SETTEXT, 0, (LPARAM)L"");
}

static void outAdd(const wstring& t, COLORREF c, bool bold) {
    long a = outLen();
    SendMessageW(gOut, EM_SETSEL, (WPARAM)a, (LPARAM)a);
    SendMessageW(gOut, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    long b = outLen();
    SendMessageW(gOut, EM_SETSEL, (WPARAM)a, (LPARAM)b);
    TextFmt f;
    memset(&f, 0, sizeof f);
    f.cbSize = sizeof f;
    f.dwMask = CFM_COLORX | CFM_BOLDX;
    f.dwEffects = bold ? CFE_BOLDX : 0;
    f.crTextColor = c;
    SendMessageW(gOut, MSG_SETCFMT, SCF_SEL, (LPARAM)&f);
}

static void renderOut(const vector<Seg>& segs) {
    outClear();
    for (size_t i = 0; i < segs.size(); ++i) outAdd(segs[i].t, segs[i].c, segs[i].bold);
    SendMessageW(gOut, EM_SETSEL, 0, 0);
    SendMessageW(gOut, EM_SCROLLCARET, 0, 0);
}

static wstring padw(const wstring& s, int n) {
    wstring r = s;
    while ((int)r.size() < n) r += L' ';
    return r;
}

static void kvSeg(vector<Seg>& s, const wstring& key, const wstring& val, COLORREF vc, bool withNl) {
    s.push_back({ padw(key, 18), Col::dim, false });
    s.push_back({ val + (withNl ? L"\n" : L""), vc, false });
}

static void updatePlaceholder() {
    if (!gPlace) return;
    if (gMode == 4) { SetWindowTextW(gPlace, PLACE[4]); ShowWindow(gPlace, SW_SHOW); return; }
    bool empty = getW(gIn).empty();
    bool focused = (GetFocus() == gIn);
    if (empty && !focused) { SetWindowTextW(gPlace, PLACE[gMode]); ShowWindow(gPlace, SW_SHOW); }
    else ShowWindow(gPlace, SW_HIDE);
}

static void reconvert() {
    vector<Seg> segs;
    gStats.clear();
    wstring in = getW(gIn);

    if (gMode == 4) {
        segs.push_back({ L"BINARY TRANSLATOR \u2014 QUICK GUIDE\n", Col::text, true });
        segs.push_back({ L"\nWHAT IS BINARY?\n", Col::accent, true });
        segs.push_back({ L"Computers store everything as bits: 0 and 1.\n", Col::text, false });
        segs.push_back({ L"Every character has a code number \u2014 'A' is 65.\n", Col::text, false });
        segs.push_back({ L"65 in binary is 01000001.\n", Col::green, false });
        segs.push_back({ L"8 bits = 1 byte = 1 character.\n", Col::text, false });
        segs.push_back({ L"\nMODES\n", Col::accent, true });
        segs.push_back({ L"Text \u2192 Binary      ", Col::dim, false });
        segs.push_back({ L"Hello \u2192 01001000 01100101 \u2026\n", Col::green, false });
        segs.push_back({ L"Binary \u2192 Text      ", Col::dim, false });
        segs.push_back({ L"paste 0s and 1s, get text back\n", Col::text, false });
        segs.push_back({ L"Number \u2192 Binary    ", Col::dim, false });
        segs.push_back({ L"255 \u2192 11111111\n", Col::green, false });
        segs.push_back({ L"Binary \u2192 Number    ", Col::dim, false });
        segs.push_back({ L"11111111 \u2192 255\n", Col::green, false });
        segs.push_back({ L"\nTIPS\n", Col::accent, true });
        segs.push_back({ L"Spaces in binary input are ignored.\n", Col::text, false });
        segs.push_back({ L"Negative numbers use 64-bit two's complement.\n", Col::text, false });
        segs.push_back({ L"Everything updates live while you type \u2014 no button needed.", Col::text, false });
    }
    else if (in.empty()) {
        segs.push_back({ L"Result appears here as you type\u2026", Col::dim, false });
    }
    else {
        string u8 = toU8(in);
        if (gMode == 0) {
            vector<string> g = splitSpaces(textToBinary(u8));
            for (size_t i = 0; i < g.size(); i += 6) {
                wstring line;
                for (size_t j = i; j < g.size() && j < i + 6; j++) {
                    if (j > i) line += L" ";
                    line += toW(g[j]);
                }
                segs.push_back({ line + L"\n", Col::green, false });
            }
            if (g.size() <= 16 && !g.empty()) {
                segs.push_back({ L"\n", Col::dim, false });
                for (size_t i = 0; i < g.size(); i++) {
                    segs.push_back({ toW(charLabel((unsigned char)u8[i])) + L"  \u2192  ", Col::yellow, true });
                    segs.push_back({ toW(g[i]) + L"\n", Col::green, false });
                }
            }
            gStats = toW(to_string(g.size()) + " BYTES  \u2022  " + to_string(g.size() * 8) + " BITS");
        }
        else if (gMode == 1) {
            string text, err;
            if (!binaryToText(u8, text, err)) {
                segs.push_back({ toW(err), Col::red, false });
                segs.push_back({ L"\n\nCheck the input \u2014 only 0 and 1, in groups of 8.", Col::dim, false });
            }
            else {
                segs.push_back({ toW(sanitizeDisplay(text)), Col::text, false });
                gStats = toW(to_string(text.size()) + " BYTES  \u2022  " + to_string(text.size() * 8) + " BITS");
            }
        }
        else if (gMode == 2) {
            bool neg = false;
            unsigned long long v = 0;
            string err;
            if (!parseWholeNumber(trim(u8), neg, v, err)) {
                segs.push_back({ toW(err), Col::red, false });
                segs.push_back({ L"\n\nCheck the input \u2014 a whole number like 65 or -12.", Col::dim, false });
            }
            else {
                kvSeg(segs, L"BINARY", (neg ? L"-" : L"") + toW(group4(ullToBinary(v))) + L"\n", Col::green, true);
                if (neg) kvSeg(segs, L"TWO'S COMPLEMENT", toW(group4(ullToBinary(~v + 1ULL))) + L"\n", Col::green, true);
                segs.push_back({ L"\n", Col::dim, false });
                kvSeg(segs, L"HEX", toW("0x" + ullToHex(v)) + L"\n", Col::text, true);
                kvSeg(segs, L"OCTAL", toW("0" + ullToOct(v)) + L"\n", Col::text, true);
                kvSeg(segs, L"DECIMAL", toW((neg ? "-" : "") + to_string(v)), Col::text, false);
            }
        }
        else {
            string bits, err;
            unsigned long long v = 0;
            if (!bitsToValue(u8, bits, v, err)) {
                segs.push_back({ toW(err), Col::red, false });
                segs.push_back({ L"\n\nCheck the input \u2014 binary digits like 11111111.", Col::dim, false });
            }
            else {
                kvSeg(segs, L"DECIMAL", toW(to_string(v)) + L"\n", Col::green, true);
                if (bits.size() == 64 && bits[0] == '1')
                    kvSeg(segs, L"SIGNED", toW(to_string((long long)v)) + L"\n", Col::yellow, true);
                segs.push_back({ L"\n", Col::dim, false });
                kvSeg(segs, L"HEX", toW("0x" + ullToHex(v)) + L"\n", Col::text, true);
                kvSeg(segs, L"OCTAL", toW("0" + ullToOct(v)) + L"\n", Col::text, true);
                kvSeg(segs, L"BITS", toW(to_string(bits.size())), Col::text, false);
                gStats = toW(to_string(bits.size()) + " BITS");
            }
        }
    }

    renderOut(segs);
    gPulse = 1;
    beginAnim();
    InvalidateRect(gWnd, 0, FALSE);
    updatePlaceholder();
}

static void switchMode(int m) {
    if (m < 0 || m > 4) return;
    gMode = m;
    EnableWindow(gIn, m != 4);
    gIndTX = (float)(gTabR[m].left + S(12));
    gIndTW = (float)(gTabR[m].right - gTabR[m].left - S(24));
    if (m != 4) SetFocus(gIn);
    reconvert();
    InvalidateRect(gWnd, 0, FALSE);
}

static void drawText2(HDC dc, const wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt);
}

static void fillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF line, int lw) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, lw, line);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void labelRow(HDC dc, const RECT& box, const wstring& label, const wstring& rightTxt) {
    RECT r = { box.left + S(2), box.top - S(26), box.right, box.top - S(6) };
    RECT bar = { r.left, r.top + S(5), r.left + S(3), r.bottom - S(5) };
    HBRUSH b = CreateSolidBrush(Col::accent);
    FillRect(dc, &bar, b);
    DeleteObject(b);
    RECT t = r;
    t.left += S(11);
    drawText2(dc, label, t, gFntSmallB, Col::dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (!rightTxt.empty())
        drawText2(dc, rightTxt, r, gFntSmall, Col::dim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

static void paintAll(HDC dc, int W, int H) {
    RECT full = { 0, 0, W, H };
    FillRect(dc, &full, gBrBg);

    fillRound(dc, gLogoR, S(10), Col::accent, Col::accent, 1);
    drawText2(dc, L"01", gLogoR, gFntLogo, Col::logoink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawText2(dc, L"Binary Translator", gTitleR, gFntTitle, Col::text, DT_LEFT | DT_SINGLELINE);
    drawText2(dc, L"text \u2192 binary \u2192 numbers, live", gSubR, gFntSub, Col::dim, DT_LEFT | DT_SINGLELINE);

    for (int i = 0; i < 5; i++) {
        RECT r = gTabR[i];
        float hv = gHov[i];
        if (hv > 0.01f) {
            RECT rr = r;
            InflateRect(&rr, -S(2), S(5));
            COLORREF hb = mix(Col::bg, Col::accent, 0.10f * hv);
            fillRound(dc, rr, S(8), hb, hb, 1);
        }
        COLORREF tc = (i == gMode) ? Col::text : mix(Col::dim, Col::text, hv);
        HFONT f = (i == gMode) ? gFntTabB : gFntTab;
        drawText2(dc, TAB_LBL[i], r, f, tc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    int iy = gTabR[0].bottom - S(5);
    RECT ir = { (int)gIndX, iy, (int)(gIndX + gIndW), iy + S(3) };
    fillRound(dc, ir, S(2), Col::accent, Col::accent, 1);

    COLORREF inBorder = (gMode == 4) ? Col::border
        : (GetFocus() == gIn ? mix(Col::border, Col::accent, 0.85f) : Col::border);
    fillRound(dc, gInBox, S(12), Col::panel, inBorder, 1);
    COLORREF outBorder = mix(Col::border, Col::accent, gPulse * 0.9f);
    fillRound(dc, gOutBox, S(12), Col::panel, outBorder, gPulse > 0.5f ? 2 : 1);

    labelRow(dc, gInBox, gMode == 4 ? L"INPUT (DISABLED)" : L"INPUT", L"");
    labelRow(dc, gOutBox, L"OUTPUT", gStats);

    int rowT = H - S(50);
    int cy = rowT + S(15);
    COLORREF dotc = (gMode == 4) ? Col::dim : Col::accent;
    HBRUSH db = CreateSolidBrush(dotc);
    HGDIOBJ ob = SelectObject(dc, db);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, S(26), cy - S(4), S(34), cy + S(4));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(db);

    RECT lb = { S(44), rowT, S(120), rowT + S(30) };
    drawText2(dc, gMode == 4 ? L"GUIDE" : L"LIVE", lb, gFntSmallB, dotc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT hb2 = { S(96), rowT, W - S(150), rowT + S(30) };
    drawText2(dc, HINTS[gMode], hb2, gFntSmall, Col::dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    bool flash = gCopyFlash > 0.02f;
    COLORREF bf = mix(Col::panel, Col::accent, 0.18f * gCopyHov + (flash ? 0.12f * gCopyFlash : 0));
    COLORREF bl = mix(Col::border, Col::accent, 0.7f * gCopyHov + (flash ? 0.3f : 0));
    fillRound(dc, gCopyR, S(8), bf, bl, 1);
    drawText2(dc, flash ? L"COPIED!" : L"COPY", gCopyR, gFntSmall,
        flash ? Col::accent : mix(Col::dim, Col::text, gCopyHov),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void layout(int W, int H) {
    if (W < 200 || H < 200) return;
    int P = S(24);
    int logo = S(38);
    gLogoR = { P, S(20), P + logo, S(20) + logo };
    gTitleR = { gLogoR.right + S(14), S(20), W - P, S(20) + S(26) };
    gSubR = { gLogoR.right + S(14), S(20) + S(27), W - P, S(20) + S(27) + S(18) };

    int tabH = S(38);
    int tabTop = S(20) + logo + S(12);
    int x = P;
    for (int i = 0; i < 5; i++) {
        gTabR[i] = { x, tabTop, x + gTabW[i], tabTop + tabH };
        x += gTabW[i] + S(6);
    }

    gIndTX = (float)(gTabR[gMode].left + S(12));
    gIndTW = (float)(gTabR[gMode].right - gTabR[gMode].left - S(24));
    if (!gIndSet) { gIndX = gIndTX + S(150); gIndW = gIndTW; gIndSet = true; beginAnim(); }

    int top = tabTop + tabH + S(16) + S(28);
    int bottom = H - S(58);
    int gap = S(22);
    int pw = (W - P * 2 - gap) / 2;
    gInBox = { P, top, P + pw, bottom };
    gOutBox = { P + pw + gap, top, W - P, bottom };

    RECT ei = { gInBox.left + S(16), gInBox.top + S(14), gInBox.right - S(10), gInBox.bottom - S(12) };
    MoveWindow(gIn, ei.left, ei.top, ei.right - ei.left, ei.bottom - ei.top, TRUE);
    RECT eo = { gOutBox.left + S(16), gOutBox.top + S(14), gOutBox.right - S(10), gOutBox.bottom - S(12) };
    MoveWindow(gOut, eo.left, eo.top, eo.right - eo.left, eo.bottom - eo.top, TRUE);
    MoveWindow(gPlace, ei.left + S(12), ei.top + S(1), ei.right - ei.left - S(14), S(22), TRUE);

    gCopyR = { W - P - S(92), H - S(50), W - P, H - S(20) };
    updatePlaceholder();
}

static LRESULT CALLBACK inProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEWHEEL) {
        int d = GET_WHEEL_DELTA_WPARAM(w);
        SendMessageW(h, EM_LINESCROLL, 0, -(d * 3) / 120);
        return 0;
    }
    if (m == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (w >= '1' && w <= '5') { switchMode((int)(w - '1')); return 0; }
    }
    return CallWindowProcW(gInOldProc, h, m, w, l);
}

static void copyOutput() {
    wstring t = getW(gOut);
    if (t.empty()) return;
    if (OpenClipboard(gWnd)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
        if (hg) {
            void* p = GlobalLock(hg);
            memcpy(p, t.c_str(), (t.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(hg);
            SetClipboardData(CF_UNICODETEXT, hg);
        }
        CloseClipboard();
    }
    gCopyFlash = 1;
    beginAnim();
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        gBrBg = CreateSolidBrush(Col::bg);
        gBrPanel = CreateSolidBrush(Col::panel);
        int d = gDpi;
        gFntTitle = CreateFontW(-MulDiv(18, d, 72), 0, 0, 0, 600, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntSub = CreateFontW(-MulDiv(9, d, 72), 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntTab = CreateFontW(-MulDiv(10, d, 72), 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntTabB = CreateFontW(-MulDiv(10, d, 72), 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntMono = CreateFontW(-MulDiv(12, d, 72), 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        gFntSmall = CreateFontW(-MulDiv(9, d, 72), 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntSmallB = CreateFontW(-MulDiv(9, d, 72), 0, 0, 0, 600, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gFntLogo = CreateFontW(-MulDiv(14, d, 72), 0, 0, 0, 700, 0, 0, 0, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HDC dc = GetDC(h);
        for (int i = 0; i < 5; i++) {
            SIZE s1, s2;
            HGDIOBJ of = SelectObject(dc, gFntTab);
            GetTextExtentPoint32W(dc, TAB_LBL[i], lstrlenW(TAB_LBL[i]), &s1);
            SelectObject(dc, gFntTabB);
            GetTextExtentPoint32W(dc, TAB_LBL[i], lstrlenW(TAB_LBL[i]), &s2);
            SelectObject(dc, of);
            gTabW[i] = (s1.cx > s2.cx ? s1.cx : s2.cx) + S(34);
        }
        ReleaseDC(h, dc);

        LoadLibraryW(L"msftedit.dll");
        gIn = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_NOHIDESEL,
            0, 0, 10, 10, h, (HMENU)100, gInst, 0);
        gOut = CreateWindowExW(0, L"RICHEDIT50W", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL,
            0, 0, 10, 10, h, (HMENU)101, gInst, 0);
        gOutRich = (gOut != 0);
        if (!gOut)
            gOut = CreateWindowExW(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 10, 10, h, (HMENU)101, gInst, 0);

        gPlace = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY | SS_LEFTNOWORDWRAP,
            0, 0, 10, 10, h, (HMENU)102, gInst, 0);

        SendMessageW(gIn, WM_SETFONT, (WPARAM)gFntMono, TRUE);
        SendMessageW(gIn, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(S(10), S(10)));
        SendMessageW(gPlace, WM_SETFONT, (WPARAM)gFntMono, TRUE);

        if (gOutRich) {
            SendMessageW(gOut, MSG_SETBKGND, 0, Col::panel);
            SendMessageW(gOut, EM_SETREADONLY, TRUE, 0);
            SendMessageW(gOut, WM_SETFONT, (WPARAM)gFntMono, TRUE);
            SendMessageW(gOut, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(S(10), S(10)));
        }

        gInOldProc = (WNDPROC)SetWindowLongPtrW(gIn, GWLP_WNDPROC, (LONG_PTR)inProc);
        break;
    }

    case WM_SIZE:
        layout(LOWORD(l), HIWORD(l));
        InvalidateRect(h, 0, FALSE);
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l;
        mmi->ptMinTrackSize.x = S(820);
        mmi->ptMinTrackSize.y = S(520);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bm);
        paintAll(mem, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(h, &ps);
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (w == 1) {
            bool active = false;
            float d;
            d = gIndTX - gIndX; if (fabs(d) > 0.5f) { gIndX += d * 0.22f; active = true; } else gIndX = gIndTX;
            d = gIndTW - gIndW; if (fabs(d) > 0.5f) { gIndW += d * 0.22f; active = true; } else gIndW = gIndTW;
            for (int i = 0; i < 5; i++) {
                float tgt = (i == gHovIdx) ? 1.0f : 0.0f;
                if (fabs(tgt - gHov[i]) > 0.02f) { gHov[i] += (tgt - gHov[i]) * 0.25f; active = true; }
                else gHov[i] = tgt;
            }
            float ct = (gCopyDown || gCopyOver) ? 1.0f : 0.0f;
            if (fabs(ct - gCopyHov) > 0.02f) { gCopyHov += (ct - gCopyHov) * 0.25f; active = true; }
            if (gPulse > 0) { gPulse -= 0.055f; if (gPulse < 0) gPulse = 0; active = true; }
            if (gCopyFlash > 0) { gCopyFlash -= 0.02f; if (gCopyFlash < 0) gCopyFlash = 0; active = true; }
            InvalidateRect(h, 0, FALSE);
            if (!active) { KillTimer(h, 1); gTmrOn = false; }
        }
        break;

    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        int nh = -1;
        for (int i = 0; i < 5; i++)
            if (PtInRect(&gTabR[i], p)) { nh = i; break; }
        gHovIdx = nh;
        gCopyOver = (PtInRect(&gCopyR, p) != 0);
        beginAnim();
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        gHovIdx = -1;
        gCopyOver = false;
        beginAnim();
        break;

    case WM_LBUTTONDOWN: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (PtInRect(&gCopyR, p)) gCopyDown = true;
        gDownTab = -1;
        for (int i = 0; i < 5; i++)
            if (PtInRect(&gTabR[i], p)) { gDownTab = i; break; }
        SetCapture(h);
        beginAnim();
        break;
    }

    case WM_LBUTTONUP: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ReleaseCapture();
        if (gCopyDown && PtInRect(&gCopyR, p)) copyOutput();
        gCopyDown = false;
        if (gDownTab >= 0 && PtInRect(&gTabR[gDownTab], p)) switchMode(gDownTab);
        gDownTab = -1;
        beginAnim();
        break;
    }

    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        bool hand = false;
        for (int i = 0; i < 5; i++)
            if (PtInRect(&gTabR[i], p)) hand = true;
        if (PtInRect(&gCopyR, p)) hand = true;
        if (hand) { SetCursor(LoadCursor(0, IDC_HAND)); return TRUE; }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HWND ctl = (HWND)l;
        HDC dc = (HDC)w;
        if (ctl == gIn) {
            SetBkColor(dc, Col::panel);
            SetTextColor(dc, IsWindowEnabled(gIn) ? Col::text : Col::dim);
            return (LRESULT)gBrPanel;
        }
        if (ctl == gPlace) {
            SetBkColor(dc, Col::panel);
            SetTextColor(dc, Col::dim);
            return (LRESULT)gBrPanel;
        }
        if (ctl == gOut && !gOutRich) {
            SetBkColor(dc, Col::panel);
            SetTextColor(dc, Col::text);
            return (LRESULT)gBrPanel;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        int code = HIWORD(w);
        if (id == 100) {
            if (code == EN_CHANGE) reconvert();
            else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) updatePlaceholder();
        }
        if (id == 102 && code == STN_CLICKED) SetFocus(gIn);
        break;
    }

    case WM_SETFOCUS:
        if (gMode != 4) SetFocus(gIn);
        break;

    case WM_DESTROY:
        KillTimer(h, 1);
        DeleteObject(gFntTitle);
        DeleteObject(gFntSub);
        DeleteObject(gFntTab);
        DeleteObject(gFntTabB);
        DeleteObject(gFntMono);
        DeleteObject(gFntSmall);
        DeleteObject(gFntSmallB);
        DeleteObject(gFntLogo);
        DeleteObject(gBrBg);
        DeleteObject(gBrPanel);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

static void darkTitleBar(HWND h) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    PFNDwmAttr f = (PFNDwmAttr)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!f) return;
    BOOL y = TRUE;
    if (FAILED(f(h, 20, &y, sizeof y))) f(h, 19, &y, sizeof y);
}

static void initDarkMode() {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    PFNAppMode f = (PFNAppMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (f) f(2);
}

static void dpiAware() {
    HMODULE u = LoadLibraryA("user32.dll");
    if (!u) return;
    PFNDpiAware f = (PFNDpiAware)GetProcAddress(u, "SetProcessDPIAware");
    if (f) f();
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    dpiAware();
    initDarkMode();
    gInst = inst;
    HDC scr = GetDC(0);
    gDpi = GetDeviceCaps(scr, LOGPIXELSX);
    ReleaseDC(0, scr);

    const wchar_t* cls = L"BinXlateWnd";
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    RegisterClassW(&wc);

    RECT wr = { 0, 0, S(980), S(640) };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int ww = wr.right - wr.left;
    int wh = wr.bottom - wr.top;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;
    if (ww > workW) ww = workW;
    if (wh > workH) wh = workH;
    int wx = work.left + (workW - ww) / 2;
    int wy = work.top + (workH - wh) / 2;

    gWnd = CreateWindowExW(0, cls, L"Binary Translator",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, wx, wy, ww, wh, 0, 0, inst, 0);
    darkTitleBar(gWnd);
    gPulse = 1;
    reconvert();
    ShowWindow(gWnd, SW_SHOW);
    UpdateWindow(gWnd);
    SetFocus(gIn);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
