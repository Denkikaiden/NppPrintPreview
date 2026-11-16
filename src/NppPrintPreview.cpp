// NppPrintPreview for Notepad++（短縮名 NppPriPre）
//
// Notepad++ の「印刷」（Ctrl+P）と同じ組み方で、印刷する前にページを画面で確かめるプラグイン。
// - 画面に出さない Scintilla を作って編集中の文書をつなぎ（SCI_GETDOCPOINTER／SCI_SETDOCPOINTER）、
//   画面のスタイル（色・フォント）を写したうえで、印刷用のフォント・大きさだけをそちらに設定する。
//   Notepad++ の画面表示と設定は変えない。
// - ページは Scintilla のページ描画（SCI_FORMATRANGEFULL）で組む。プレビューはプリンターを参照にした
//   拡張メタファイル（EMF）に描いて縮小表示し、印刷は同じ処理をプリンターに描く。
// - 余白・ヘッダー・フッター・行番号・色の扱いは Notepad++ の印刷設定（config.xml）を読む。
// - MultiColumnPreview（MulCoPre）が同じ PC にあれば、メニューから文書を渡して段組み印刷もできる（控えめな連動）。
//
// Notepad++ のヘッダー・印刷部分のソース（GPL）は使わず、必要な定義だけをここに書いている。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winspool.h>
#include <winhttp.h>

// リンクを開く、登録の読み取りに使う（TaskDialog は comctl32 の版によってはないので、使うときに探す）
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
// 最新版の確認（GitHub への問い合わせ。押したときだけ）
#pragma comment(lib, "winhttp.lib")
// 数値欄のホイール（SetWindowSubclass）
#pragma comment(lib, "comctl32.lib")

#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// ---- Notepad++ プラグインの取り決め（必要な分だけ） ----

struct NppData {
    HWND _nppHandle;
    HWND _scintillaMainHandle;
    HWND _scintillaSecondHandle;
};

struct ShortcutKey {
    bool _isCtrl;
    bool _isAlt;
    bool _isShift;
    UCHAR _key;
};

typedef void(__cdecl *PFUNCPLUGINCMD)();

struct FuncItem {
    wchar_t _itemName[64];
    PFUNCPLUGINCMD _pFunc;
    int _cmdID;
    bool _init2Check;
    ShortcutKey *_pShKey;
};

struct NotifyHeader {
    HWND hwndFrom;
    UINT_PTR idFrom;
    UINT code;
};
struct SCNotification {
    NotifyHeader nmhdr;  // 先頭だけ使う
};

constexpr UINT NPPMSG = WM_USER + 1000;
constexpr UINT NPPM_GETCURRENTSCINTILLA = NPPMSG + 4;
constexpr UINT NPPM_GETPLUGINSCONFIGDIR = NPPMSG + 46;
constexpr UINT NPPM_ISDARKMODEENABLED = NPPMSG + 107;
constexpr UINT NPPM_GETDARKMODECOLORS = NPPMSG + 108;
constexpr UINT NPPM_DARKMODESUBCLASSANDTHEME = NPPMSG + 112;
constexpr WPARAM DMF_INIT = 0x0000000B;
constexpr UINT RUNCOMMAND_USER = WM_USER + 3000;
constexpr UINT NPPM_GETFULLCURRENTPATH = RUNCOMMAND_USER + 1;
constexpr UINT NPPM_GETFILENAME = RUNCOMMAND_USER + 3;
constexpr UINT NPPN_READY = 1000 + 1;
constexpr UINT NPPN_BUFFERACTIVATED = 1000 + 10;
static bool g_ready = false; // Notepad++ の起動が終わった（それまでの文書の切り替えは数えない）

struct NppDarkColors {
    COLORREF background, softerBackground, hotBackground, pureBackground, errorBackground;
    COLORREF text, darkerText, disabledText, linkText, edge, hotEdge, disabledEdge;
};

// ---- Scintilla（必要な分だけ） ----

constexpr UINT SCI_GETLENGTH = 2006;
constexpr UINT SCI_GETVIEWWS = 2020;
constexpr UINT SCI_SETVIEWWS = 2021;
constexpr UINT SCI_STYLESETFORE = 2051;
constexpr UINT SCI_STYLESETBACK = 2052;
constexpr UINT SCI_STYLESETITALIC = 2054;
constexpr UINT SCI_STYLESETFONT = 2056;
constexpr UINT SCI_STYLESETEOLFILLED = 2057;
constexpr UINT SCI_STYLESETUNDERLINE = 2059;
constexpr UINT SCI_STYLESETCASE = 2060;
constexpr UINT SCI_STYLESETSIZEFRACTIONAL = 2061;
constexpr UINT SCI_STYLEGETSIZEFRACTIONAL = 2062;
constexpr UINT SCI_STYLESETWEIGHT = 2063;
constexpr UINT SCI_STYLEGETWEIGHT = 2064;
constexpr UINT SCI_STYLESETCHARACTERSET = 2066;
constexpr UINT SCI_STYLESETVISIBLE = 2074;
constexpr UINT SCI_INDICSETSTYLE = 2080;
constexpr UINT SCI_INDICGETSTYLE = 2081;
constexpr UINT SCI_INDICSETFORE = 2082;
constexpr UINT SCI_INDICGETFORE = 2083;
constexpr UINT SCI_SETWHITESPACESIZE = 2086;
constexpr UINT SCI_GETWHITESPACESIZE = 2087;
constexpr UINT SCI_SETINDENTATIONGUIDES = 2132;
constexpr UINT SCI_GETINDENTATIONGUIDES = 2133;
constexpr UINT SCI_GETCODEPAGE = 2137;
constexpr UINT SCI_GETSELECTIONSTART = 2143;
constexpr UINT SCI_GETSELECTIONEND = 2145;
constexpr UINT SCI_SETPRINTMAGNIFICATION = 2146;
constexpr UINT SCI_SETPRINTCOLOURMODE = 2148;
constexpr UINT SCI_FORMATRANGE = 2151;
constexpr UINT SCI_SETMARGINLEFT = 2155;
constexpr UINT SCI_GETMARGINLEFT = 2156;
constexpr UINT SCI_SETMARGINRIGHT = 2157;
constexpr UINT SCI_GETMARGINRIGHT = 2158;
constexpr UINT SCI_GETSELTEXT = 2161;
constexpr UINT SCI_GETTEXT = 2182;
constexpr UINT SCI_SETMARGINTYPEN = 2240;
constexpr UINT SCI_GETMARGINTYPEN = 2241;
constexpr UINT SCI_SETMARGINWIDTHN = 2242;
constexpr UINT SCI_GETMARGINWIDTHN = 2243;
constexpr UINT SCI_SETMARGINS = 2252;
constexpr UINT SCI_GETMARGINS = 2253;
constexpr UINT SCI_GETVIEWEOL = 2355;
constexpr UINT SCI_SETVIEWEOL = 2356;
constexpr UINT SCI_GETDOCPOINTER = 2357;
constexpr UINT SCI_SETDOCPOINTER = 2358;
constexpr UINT SCI_SETCONTROLCHARSYMBOL = 2388;
constexpr UINT SCI_GETCONTROLCHARSYMBOL = 2389;
constexpr UINT SCI_SETWRAPVISUALFLAGS = 2460;
constexpr UINT SCI_GETWRAPVISUALFLAGS = 2461;
constexpr UINT SCI_SETWRAPVISUALFLAGSLOCATION = 2462;
constexpr UINT SCI_GETWRAPVISUALFLAGSLOCATION = 2463;
constexpr UINT SCI_SETWRAPSTARTINDENT = 2464;
constexpr UINT SCI_GETWRAPSTARTINDENT = 2465;
constexpr UINT SCI_SETWRAPINDENTMODE = 2472;
constexpr UINT SCI_GETWRAPINDENTMODE = 2473;
constexpr UINT SCI_STYLEGETFORE = 2481;
constexpr UINT SCI_STYLEGETBACK = 2482;
constexpr UINT SCI_STYLEGETITALIC = 2484;
constexpr UINT SCI_STYLEGETFONT = 2486;
constexpr UINT SCI_STYLEGETEOLFILLED = 2487;
constexpr UINT SCI_STYLEGETUNDERLINE = 2488;
constexpr UINT SCI_STYLEGETCASE = 2489;
constexpr UINT SCI_STYLEGETCHARACTERSET = 2490;
constexpr UINT SCI_STYLEGETVISIBLE = 2491;
constexpr UINT SCI_INDICSETUNDER = 2510;
constexpr UINT SCI_INDICGETUNDER = 2511;
constexpr UINT SCI_INDICSETALPHA = 2523;
constexpr UINT SCI_INDICGETALPHA = 2524;
constexpr UINT SCI_SETEXTRAASCENT = 2525;
constexpr UINT SCI_GETEXTRAASCENT = 2526;
constexpr UINT SCI_SETEXTRADESCENT = 2527;
constexpr UINT SCI_GETEXTRADESCENT = 2528;
constexpr UINT SCI_INDICSETOUTLINEALPHA = 2558;
constexpr UINT SCI_INDICGETOUTLINEALPHA = 2559;
constexpr UINT SCI_SETFONTQUALITY = 2611;
constexpr UINT SCI_GETFONTQUALITY = 2612;
constexpr UINT SCI_GETSELECTIONEMPTY = 2650;
constexpr UINT SCI_FORMATRANGEFULL = 2777;
constexpr UINT SCI_GETCHARACTERPOINTER = 2520;
constexpr int SC_CP_UTF8 = 65001;
constexpr int SC_MARGIN_NUMBER = 1;
constexpr int STYLE_DEFAULT = 32;
constexpr int STYLE_LINENUMBER = 33;
constexpr int INDIC_MAX = 35;

struct SciRect {
    int left, top, right, bottom;
};
struct SciRangeToFormatFull {
    void *hdc;
    void *hdcTarget;
    SciRect rc;
    SciRect rcPage;
    intptr_t cpMin, cpMax;
};
struct SciRangeToFormat {
    void *hdc;
    void *hdcTarget;
    SciRect rc;
    SciRect rcPage;
    long cpMin, cpMax;
};

// ---- プラグインの状態 ----

static const wchar_t PLUGIN_NAME[] = L"NppPrintPreview";
static const wchar_t PLUGIN_VERSION[] = L"1.0.0"; // NppPrintPreview.rc の FILEVERSION と合わせる
static const wchar_t VIEW_CLASS[] = L"NppPrintPreviewPage";

static NppData g_npp{};
static HINSTANCE g_module = nullptr;

// 表示言語：日本語か英語。ini の [General] Language（ja／en／空＝自動）。自動は Notepad++ の表示言語に合わせる
static bool g_en = false;
static const wchar_t *TR(const wchar_t *ja, const wchar_t *en) { return g_en ? en : ja; }

static void cmdPrintPreview();
static void cmdMulCoPre();
static void cmdSetLocation();
static void cmdAbout();
static void updateMenuTexts();

constexpr int FUNC_MULCOPRE = 2;  // g_funcs の中の「MulCoPre で段組みプレビュー」の位置

static FuncItem g_funcs[] = {
    {L"印刷プレビュー…", cmdPrintPreview, 0, false, nullptr},
    {L"", nullptr, 0, false, nullptr},  // 区切り線
    {L"MulCoPre で段組みプレビュー", cmdMulCoPre, 0, false, nullptr},
    {L"MulCoPre の場所を設定…", cmdSetLocation, 0, false, nullptr},
    {L"", nullptr, 0, false, nullptr},  // 区切り線
    {L"バージョン情報", cmdAbout, 0, false, nullptr},
};

// ---- 小さな道具 ----

static LRESULT sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessageW(h, m, w, l); }

static void showMessage(HWND owner, const std::wstring &text, UINT icon = MB_ICONINFORMATION) {
    MessageBoxW(owner ? owner : g_npp._nppHandle, text.c_str(), PLUGIN_NAME, MB_OK | icon);
}

static HWND currentScintilla() {
    int which = 0;
    SendMessageW(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 0 ? g_npp._scintillaMainHandle : g_npp._scintillaSecondHandle;
}

static bool fileExists(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring envPath(const wchar_t *name, const wchar_t *rest) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(buf) + rest;
}

static std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::string wideToUtf8(const std::wstring &w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

/// 文書の文字コード（Scintilla のコードページ）のバイト列を UTF-8 にする
static std::string toUtf8(const std::string &bytes, int codepage) {
    if (codepage == SC_CP_UTF8 || bytes.empty()) return bytes;
    UINT cp = codepage == 0 ? CP_ACP : static_cast<UINT>(codepage);
    int wlen = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wlen <= 0) return bytes;
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), wlen);
    return wideToUtf8(w);
}

/// 数値を「10」「10.5」のように書く（小数は 1 桁まで。それより細かい値は 2 桁）
static std::wstring fmtNum(double v) {
    wchar_t buf[32];
    double r1 = std::round(v * 10) / 10;
    if (std::fabs(v - std::round(v)) < 0.005) swprintf_s(buf, L"%.0f", v);
    else if (std::fabs(v - r1) < 0.005) swprintf_s(buf, L"%.1f", v);
    else swprintf_s(buf, L"%.2f", v);
    return buf;
}

/// 入力欄の数値を読む（全角の数字・小数点も受け付ける）
static bool parseNum(const std::wstring &text, double lo, double hi, double &out) {
    std::wstring t;
    for (wchar_t c : text) {
        if (c >= L'０' && c <= L'９') c = static_cast<wchar_t>(L'0' + (c - L'０'));
        else if (c == L'．') c = L'.';
        if (c == L' ' || c == L'　') continue;
        t += c;
    }
    if (t.empty()) return false;
    wchar_t *end = nullptr;
    double v = wcstod(t.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(v) || v < lo || v > hi) return false;
    out = v;
    return true;
}

static std::wstring windowText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

// ---- 設定ファイル（plugins\config\NppPrintPreview.ini） ----

static std::wstring pluginsConfigDir() {
    wchar_t dir[MAX_PATH] = {};
    SendMessageW(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(dir));
    return dir;
}

static std::wstring iniPath() {
    std::wstring dir = pluginsConfigDir();
    return dir.empty() ? L"" : dir + L"\\NppPrintPreview.ini";
}

static std::wstring iniGet(const wchar_t *section, const wchar_t *key) {
    std::wstring ini = iniPath();
    if (ini.empty()) return L"";
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 1024, ini.c_str());
    return buf;
}

/// 空の値はキーを消す
static void iniSet(const wchar_t *section, const wchar_t *key, const std::wstring &value) {
    std::wstring ini = iniPath();
    if (ini.empty()) return;
    WritePrivateProfileStringW(section, key, value.empty() ? nullptr : value.c_str(), ini.c_str());
}

// ---- Notepad++ の印刷設定（config.xml の <GUIConfig name="Print" .../>） ----

struct PrintCfg {
    bool lineNumber = true;
    bool formFeed = false;             // formFeedPageBreak：改ページ文字で改ページする
    int printOption = 3;              // SC_PRINT_COLOURONWHITE
    double margin[4] = {0, 0, 0, 0};   // 上・下・左・右（mm）
    std::wstring header[3], footer[3];  // 左・中央・右
    std::wstring headerFont, footerFont;
    int headerStyle = 0, footerStyle = 0, headerSize = 0, footerSize = 0;
    bool hasHeader() const { return !header[0].empty() || !header[1].empty() || !header[2].empty(); }
    bool hasFooter() const { return !footer[0].empty() || !footer[1].empty() || !footer[2].empty(); }
};

static std::wstring xmlDecode(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) {
            o += s[i];
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") o += '&';
        else if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                 : strtoul(ent.c_str() + 1, nullptr, 10);
            std::wstring w;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                w += static_cast<wchar_t>(0xD800 + (cp >> 10));
                w += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            } else {
                w += static_cast<wchar_t>(cp);
            }
            o += wideToUtf8(w);
        } else {
            o += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return utf8ToWide(o);
}

static bool xmlAttr(const std::string &tag, const char *name, std::wstring &out) {
    std::string key = std::string(" ") + name + "=\"";
    size_t p = tag.find(key);
    if (p == std::string::npos) return false;
    p += key.size();
    size_t e = tag.find('"', p);
    if (e == std::string::npos) return false;
    out = xmlDecode(tag.substr(p, e - p));
    return true;
}

static std::wstring nppConfigXmlPath() {
    std::wstring d = pluginsConfigDir();  // ...\plugins\config
    for (int i = 0; i < 2; ++i) {
        size_t p = d.find_last_of(L"\\/");
        if (p == std::wstring::npos) return L"";
        d.resize(p);
    }
    return d + L"\\config.xml";
}

static PrintCfg loadPrintCfg() {
    PrintCfg c;
    std::wstring path = nppConfigXmlPath();
    if (path.empty()) return c;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return c;
    std::string data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) read = 0;
        data.resize(read);
    }
    CloseHandle(h);
    size_t p = data.find("<GUIConfig name=\"Print\"");
    if (p == std::string::npos) return c;
    size_t e = data.find('>', p);
    if (e == std::string::npos) return c;
    std::string tag = data.substr(p, e - p);

    std::wstring v;
    if (xmlAttr(tag, "lineNumber", v)) c.lineNumber = v == L"yes";
    if (xmlAttr(tag, "formFeedPageBreak", v)) c.formFeed = v == L"yes";
    if (xmlAttr(tag, "printOption", v)) c.printOption = _wtoi(v.c_str());
    const char *mk[4] = {"margeTop", "margeBottom", "margeLeft", "margeRight"};
    for (int i = 0; i < 4; ++i)
        if (xmlAttr(tag, mk[i], v)) c.margin[i] = std::max(0, _wtoi(v.c_str()));
    const char *hk[3] = {"headerLeft", "headerMiddle", "headerRight"};
    const char *fk[3] = {"footerLeft", "footerMiddle", "footerRight"};
    for (int i = 0; i < 3; ++i) {
        xmlAttr(tag, hk[i], c.header[i]);
        xmlAttr(tag, fk[i], c.footer[i]);
    }
    xmlAttr(tag, "headerFontName", c.headerFont);
    xmlAttr(tag, "footerFontName", c.footerFont);
    if (xmlAttr(tag, "headerFontStyle", v)) c.headerStyle = _wtoi(v.c_str());
    if (xmlAttr(tag, "footerFontStyle", v)) c.footerStyle = _wtoi(v.c_str());
    if (xmlAttr(tag, "headerFontSize", v)) c.headerSize = _wtoi(v.c_str());
    if (xmlAttr(tag, "footerFontSize", v)) c.footerSize = _wtoi(v.c_str());
    return c;
}

// ---- 表示言語 ----

/// Notepad++ が日本語表示か（設定フォルダの nativeLang.xml が japanese.xml なら日本語。ファイルがなければ英語）
static bool nppIsJapanese() {
    std::wstring xml = nppConfigXmlPath();
    if (xml.empty()) return false;
    xml = xml.substr(0, xml.size() - wcslen(L"config.xml")) + L"nativeLang.xml";
    HANDLE h = CreateFileW(xml.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string head(4096, '\0');
    DWORD read = 0;
    if (!ReadFile(h, head.data(), static_cast<DWORD>(head.size()), &read, nullptr)) read = 0;
    CloseHandle(h);
    head.resize(read);
    for (auto &c : head) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return head.find("japanese.xml") != std::string::npos;
}

/// ini の [General] Language（ja／en／空＝自動）から表示言語を決める
static void decideLanguage() {
    std::wstring v = iniGet(L"General", L"Language");
    if (v == L"ja") g_en = false;
    else if (v == L"en") g_en = true;
    else g_en = !nppIsJapanese();
}

static const wchar_t *funcName(int i) {
    switch (i) {
    case 0: return TR(L"印刷プレビュー…", L"Print Preview...");
    case 2: return TR(L"MulCoPre で段組みプレビュー", L"Multi-column Preview in MulCoPre");
    case 3: return TR(L"MulCoPre の場所を設定…", L"Set MulCoPre Location...");
    case 5: return TR(L"バージョン情報", L"About");
    }
    return L"";
}

// ---- プリンター ----

struct PaperInfo {
    WORD id;
    std::wstring name;
    POINT size;  // 0.1mm（縦置き）
};

static std::vector<std::wstring> listPrinters() {
    std::vector<std::wstring> out;
    DWORD need = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4, nullptr, 0, &need, &count);
    if (need == 0) return out;
    std::vector<BYTE> buf(need);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4, buf.data(), need, &need, &count)) return out;
    auto *info = reinterpret_cast<PRINTER_INFO_4W *>(buf.data());
    for (DWORD i = 0; i < count; ++i)
        if (info[i].pPrinterName) out.emplace_back(info[i].pPrinterName);
    return out;
}

static std::wstring defaultPrinter() {
    DWORD n = 0;
    GetDefaultPrinterW(nullptr, &n);
    if (n == 0) return L"";
    std::wstring s(n, L'\0');
    if (!GetDefaultPrinterW(s.data(), &n)) return L"";
    s.resize(wcslen(s.c_str()));
    return s;
}

static std::vector<PaperInfo> listPapers(const std::wstring &printer) {
    std::vector<PaperInfo> out;
    int n = DeviceCapabilitiesW(printer.c_str(), nullptr, DC_PAPERS, nullptr, nullptr);
    if (n <= 0) return out;
    std::vector<WORD> ids(n);
    std::vector<wchar_t> names(static_cast<size_t>(n) * 64 + 1, L'\0');
    std::vector<POINT> sizes(n);
    DeviceCapabilitiesW(printer.c_str(), nullptr, DC_PAPERS, reinterpret_cast<LPWSTR>(ids.data()), nullptr);
    DeviceCapabilitiesW(printer.c_str(), nullptr, DC_PAPERNAMES, names.data(), nullptr);
    DeviceCapabilitiesW(printer.c_str(), nullptr, DC_PAPERSIZE, reinterpret_cast<LPWSTR>(sizes.data()), nullptr);
    for (int i = 0; i < n; ++i) {
        const wchar_t *nm = names.data() + static_cast<size_t>(i) * 64;
        out.push_back({ids[i], std::wstring(nm, wcsnlen(nm, 64)), sizes[i]});
    }
    return out;
}

/// プリンターの DEVMODE を作る。paper／orient が 0 ならプリンターの既定のまま。既定値も返す
static bool makeDevMode(const std::wstring &printer, short paper, short orient, std::vector<BYTE> &out, short *defPaper,
                        short *defOrient) {
    HANDLE hp = nullptr;
    if (!OpenPrinterW(const_cast<LPWSTR>(printer.c_str()), &hp, nullptr)) return false;
    LONG size = DocumentPropertiesW(nullptr, hp, const_cast<LPWSTR>(printer.c_str()), nullptr, nullptr, 0);
    bool ok = false;
    if (size > 0) {
        out.assign(static_cast<size_t>(size), 0);
        auto *dm = reinterpret_cast<DEVMODEW *>(out.data());
        if (DocumentPropertiesW(nullptr, hp, const_cast<LPWSTR>(printer.c_str()), dm, nullptr, DM_OUT_BUFFER) == IDOK) {
            if (defPaper) *defPaper = (dm->dmFields & DM_PAPERSIZE) ? dm->dmPaperSize : 0;
            if (defOrient) *defOrient = (dm->dmFields & DM_ORIENTATION) ? dm->dmOrientation : DMORIENT_PORTRAIT;
            if (paper > 0) {
                dm->dmPaperSize = paper;
                dm->dmFields |= DM_PAPERSIZE;
                dm->dmFields &= ~(DM_PAPERLENGTH | DM_PAPERWIDTH | DM_FORMNAME);
            }
            if (orient > 0) {
                dm->dmOrientation = orient;
                dm->dmFields |= DM_ORIENTATION;
            }
            ok = DocumentPropertiesW(nullptr, hp, const_cast<LPWSTR>(printer.c_str()), dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) ==
                 IDOK;
        }
    }
    ClosePrinter(hp);
    return ok;
}

// ---- 印刷プレビューの窓 ----

enum {
    ID_PRINTER = 101,
    ID_PAPER,
    ID_ORIENT,
    ID_FONT,
    ID_SIZE,
    ID_MTOP,
    ID_MBOTTOM,
    ID_MLEFT,
    ID_MRIGHT,
    ID_RESET,
    ID_SEL,
    ID_LINENUM,
    ID_COLOR,
    ID_LANG,
    ID_INFO,
    ID_STATUS,
    ID_COPIES,
    ID_PRINT,
    ID_PREV,
    ID_NEXT,
    ID_PAGE,
    ID_ZOOM,
    ID_VIEW,
    ID_SCI = 999,
    ID_LABEL = 200,
};
enum { LB_PRINTER, LB_PAPER, LB_ORIENT, LB_FONT, LB_SIZE, LB_PT, LB_COLOR, LB_MARGIN, LB_MT, LB_MB, LB_ML, LB_MR, LB_COPIES, LB_LANG, LB_COUNT };

// 色の扱い（Scintilla の SC_PRINT_*。Notepad++ の印刷設定 printOption と同じ番号）
struct ColorChoice {
    int mode;
    const wchar_t *name;
};
struct ColorChoiceText {
    const wchar_t *ja, *en;
};
static const ColorChoice COLOR_CHOICES[] = {
    {2, nullptr},
    {3, nullptr},
    {0, nullptr},
    {1, nullptr},
};
static const ColorChoiceText COLOR_TEXTS[] = {
    {L"白地に黒", L"Black on white"},
    {L"白地にカラー", L"Color on white"},
    {L"画面と同じ色", L"Same as screen"},
    {L"明暗を反転", L"Inverted"},
};
constexpr UINT_PTR TIMER_APPLY = 1;

struct Preview {
    HWND dlg = nullptr, view = nullptr, sci = nullptr, mainSci = nullptr;
    HWND cPrinter = nullptr, cPaper = nullptr, cOrient = nullptr, cFont = nullptr, eSize = nullptr;
    HWND eMargin[4] = {};
    HWND bReset = nullptr, chkSel = nullptr, sInfo = nullptr, sStatus = nullptr, eCopies = nullptr, bPrint = nullptr,
         bClose = nullptr, bPrev = nullptr, bNext = nullptr, sPage = nullptr, cZoom = nullptr;
    HWND lbl[LB_COUNT] = {};
    HFONT uiFont = nullptr;
    int dpi = 96;
    bool loading = false;
    COLORREF viewBg = RGB(0xC8, 0xC8, 0xC8), viewText = RGB(0, 0, 0);

    PrintCfg cfg;
    std::string nppFont;  // Notepad++ の今の既定スタイル（UTF-8）
    int nppSizeFrac = 1000;
    std::string font;  // 印刷に使うフォント
    int sizeFrac = 1000;  // 印刷に使う大きさ（pt×100）
    int colorMode = 3;    // 印刷に使う色の扱い（SC_PRINT_*）
    HWND cColor = nullptr, cLang = nullptr;
    // 行番号を印刷するか（2026-11-16 ユーザー依頼で切り替えを追加）。画面に行番号が出ているときだけ選べる
    HWND chkLn = nullptr;
    bool lnWanted = true;     // 印刷したい（初めは Notepad++ の印刷設定。変えたら ini に覚える）
    bool lnAvailable = false; // 画面に行番号が出ている
    double margin[4] = {};  // 上・下・左・右（mm）

    std::vector<std::wstring> printers;
    std::wstring printer;
    std::vector<PaperInfo> papers;
    short paper = 0, orient = DMORIENT_PORTRAIT, defPaper = 0, defOrient = DMORIENT_PORTRAIT;
    std::vector<BYTE> devmode;
    HDC target = nullptr;
    int physW = 0, physH = 0, offX = 0, offY = 0, resX = 0, resY = 0, dpiX = 600, dpiY = 600;
    RECT marginRect{}, body{};  // 用紙の左上を原点にしたプリンターの座標
    int headerH = 0, footerH = 0, inset = 0;
    bool clamped = false;

    bool useFull = true, lineNumbers = false;
    bool hasSel = false;
    intptr_t selStart = 0, selEnd = 0, docLen = 0, rangeStart = 0, rangeEnd = 0;
    std::vector<intptr_t> starts;  // 各ページの先頭の位置
    std::vector<intptr_t> ends;    // 各ページを組むときの終わりの位置（改ページ文字があればその位置）
    int linesPerPage = 0, halfChars = 0, fullChars = 0;

    int cur = 0, zoom = 0, scrollX = 0, scrollY = 0, wheelAcc = 0;
    HENHMETAFILE emf = nullptr;
    int emfPage = -1;
    HBITMAP bmp = nullptr;
    int bmpPage = -1, bmpW = 0, bmpH = 0;
    std::wstring docPath, docName, error;
};

static Preview *g_pv = nullptr;

static int S(const Preview &p, int v) { return MulDiv(v, p.dpi, 96); }

static int mmToPx(double mm, int dpi) { return static_cast<int>(std::lround(mm * dpi / 25.4)); }

static void dropCache(Preview &p) {
    if (p.emf) DeleteEnhMetaFile(p.emf);
    p.emf = nullptr;
    p.emfPage = -1;
    if (p.bmp) DeleteObject(p.bmp);
    p.bmp = nullptr;
    p.bmpPage = -1;
}

// -- 画面に出さない Scintilla の準備 --

static std::string styleFont(HWND h, int style) {
    LRESULT n = sci(h, SCI_STYLEGETFONT, style, 0);
    std::string f(static_cast<size_t>(std::max<LRESULT>(n, 0)) + 64, '\0');
    sci(h, SCI_STYLEGETFONT, style, reinterpret_cast<LPARAM>(f.data()));
    f.resize(strlen(f.c_str()));
    return f;
}

/// 画面のビューの見た目（スタイル・余白・表示の設定）を、画面に出さないビューに写す
static void copyView(HWND from, HWND to) {
    for (int s = 0; s < 256; ++s) {
        sci(to, SCI_STYLESETFORE, s, sci(from, SCI_STYLEGETFORE, s));
        sci(to, SCI_STYLESETBACK, s, sci(from, SCI_STYLEGETBACK, s));
        std::string f = styleFont(from, s);
        sci(to, SCI_STYLESETFONT, s, reinterpret_cast<LPARAM>(f.c_str()));
        sci(to, SCI_STYLESETSIZEFRACTIONAL, s, sci(from, SCI_STYLEGETSIZEFRACTIONAL, s));
        sci(to, SCI_STYLESETWEIGHT, s, sci(from, SCI_STYLEGETWEIGHT, s));
        sci(to, SCI_STYLESETITALIC, s, sci(from, SCI_STYLEGETITALIC, s));
        sci(to, SCI_STYLESETUNDERLINE, s, sci(from, SCI_STYLEGETUNDERLINE, s));
        sci(to, SCI_STYLESETEOLFILLED, s, sci(from, SCI_STYLEGETEOLFILLED, s));
        sci(to, SCI_STYLESETCASE, s, sci(from, SCI_STYLEGETCASE, s));
        sci(to, SCI_STYLESETCHARACTERSET, s, sci(from, SCI_STYLEGETCHARACTERSET, s));
        sci(to, SCI_STYLESETVISIBLE, s, sci(from, SCI_STYLEGETVISIBLE, s));
    }
    int margins = static_cast<int>(sci(from, SCI_GETMARGINS));
    sci(to, SCI_SETMARGINS, margins);
    for (int m = 0; m < margins; ++m) {
        sci(to, SCI_SETMARGINTYPEN, m, sci(from, SCI_GETMARGINTYPEN, m));
        sci(to, SCI_SETMARGINWIDTHN, m, sci(from, SCI_GETMARGINWIDTHN, m));
    }
    sci(to, SCI_SETMARGINLEFT, 0, sci(from, SCI_GETMARGINLEFT));
    sci(to, SCI_SETMARGINRIGHT, 0, sci(from, SCI_GETMARGINRIGHT));
    for (int i = 0; i <= INDIC_MAX; ++i) {
        sci(to, SCI_INDICSETSTYLE, i, sci(from, SCI_INDICGETSTYLE, i));
        sci(to, SCI_INDICSETFORE, i, sci(from, SCI_INDICGETFORE, i));
        sci(to, SCI_INDICSETUNDER, i, sci(from, SCI_INDICGETUNDER, i));
        sci(to, SCI_INDICSETALPHA, i, sci(from, SCI_INDICGETALPHA, i));
        sci(to, SCI_INDICSETOUTLINEALPHA, i, sci(from, SCI_INDICGETOUTLINEALPHA, i));
    }
    sci(to, SCI_SETVIEWWS, sci(from, SCI_GETVIEWWS));
    sci(to, SCI_SETWHITESPACESIZE, sci(from, SCI_GETWHITESPACESIZE));
    sci(to, SCI_SETVIEWEOL, sci(from, SCI_GETVIEWEOL));
    sci(to, SCI_SETINDENTATIONGUIDES, sci(from, SCI_GETINDENTATIONGUIDES));
    sci(to, SCI_SETEXTRAASCENT, sci(from, SCI_GETEXTRAASCENT));
    sci(to, SCI_SETEXTRADESCENT, sci(from, SCI_GETEXTRADESCENT));
    sci(to, SCI_SETFONTQUALITY, sci(from, SCI_GETFONTQUALITY));
    sci(to, SCI_SETCONTROLCHARSYMBOL, sci(from, SCI_GETCONTROLCHARSYMBOL));
    sci(to, SCI_SETWRAPVISUALFLAGS, sci(from, SCI_GETWRAPVISUALFLAGS));
    sci(to, SCI_SETWRAPVISUALFLAGSLOCATION, sci(from, SCI_GETWRAPVISUALFLAGSLOCATION));
    sci(to, SCI_SETWRAPSTARTINDENT, sci(from, SCI_GETWRAPSTARTINDENT));
    sci(to, SCI_SETWRAPINDENTMODE, sci(from, SCI_GETWRAPINDENTMODE));
}

/// 印刷用の見た目にする：画面のビューを写し、フォント・大きさだけ差し替え、Notepad++ の印刷設定を当てる
static void restyle(Preview &p) {
    copyView(p.mainSci, p.sci);
    if (p.font != p.nppFont) {
        for (int s = 0; s < 256; ++s) sci(p.sci, SCI_STYLESETFONT, s, reinterpret_cast<LPARAM>(p.font.c_str()));
    }
    if (p.sizeFrac != p.nppSizeFrac && p.nppSizeFrac > 0) {
        for (int s = 0; s < 256; ++s) {
            double f = static_cast<double>(sci(p.mainSci, SCI_STYLEGETSIZEFRACTIONAL, s));
            int nf = static_cast<int>(std::lround(f * p.sizeFrac / p.nppSizeFrac));
            sci(p.sci, SCI_STYLESETSIZEFRACTIONAL, s, std::max(100, nf));
        }
    }
    // 行番号：プレビュー窓の「行番号を印刷する」（初めは Notepad++ の印刷設定）が外れていれば消す。
    // 画面に出ていない行番号は印刷できない（Notepad++ と同じ）
    p.lineNumbers = false;
    p.lnAvailable = false;
    int margins = static_cast<int>(sci(p.sci, SCI_GETMARGINS));
    for (int m = 0; m < margins; ++m) {
        if (sci(p.sci, SCI_GETMARGINTYPEN, m) == SC_MARGIN_NUMBER && sci(p.sci, SCI_GETMARGINWIDTHN, m) > 0) {
            p.lnAvailable = true;
            if (p.lnWanted) p.lineNumbers = true;
            else sci(p.sci, SCI_SETMARGINWIDTHN, m, 0);
        }
    }
    if (p.chkLn) {
        SendMessageW(p.chkLn, BM_SETCHECK, p.lineNumbers ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(p.chkLn, p.lnAvailable);
    }
    sci(p.sci, SCI_SETPRINTCOLOURMODE, p.colorMode);
    sci(p.sci, SCI_SETPRINTMAGNIFICATION, 0);
}

// -- プリンター --

static void loadPapers(Preview &p) {
    p.papers = listPapers(p.printer);
    std::vector<BYTE> dm;
    p.defPaper = 0;
    p.defOrient = DMORIENT_PORTRAIT;
    makeDevMode(p.printer, 0, 0, dm, &p.defPaper, &p.defOrient);
}

static bool paperSupported(const Preview &p, short id) {
    for (const auto &pp : p.papers)
        if (pp.id == static_cast<WORD>(id)) return true;
    return false;
}

static void openTarget(Preview &p) {
    if (p.target) DeleteDC(p.target);
    p.target = nullptr;
    if (p.printer.empty()) {
        p.error = TR(L"プリンターがありません。", L"No printer is installed.");
        return;
    }
    if (!makeDevMode(p.printer, p.paper, p.orient, p.devmode, nullptr, nullptr)) {
        p.error = std::wstring(TR(L"プリンターの設定を読めませんでした：\r\n", L"Could not read the printer settings:\r\n")) + p.printer;
        return;
    }
    p.target = CreateDCW(L"WINSPOOL", p.printer.c_str(), nullptr, reinterpret_cast<DEVMODEW *>(p.devmode.data()));
    if (!p.target) {
        p.error = std::wstring(TR(L"プリンターを開けませんでした：\r\n", L"Could not open the printer:\r\n")) + p.printer;
        return;
    }
    p.physW = GetDeviceCaps(p.target, PHYSICALWIDTH);
    p.physH = GetDeviceCaps(p.target, PHYSICALHEIGHT);
    p.offX = GetDeviceCaps(p.target, PHYSICALOFFSETX);
    p.offY = GetDeviceCaps(p.target, PHYSICALOFFSETY);
    p.resX = GetDeviceCaps(p.target, HORZRES);
    p.resY = GetDeviceCaps(p.target, VERTRES);
    p.dpiX = GetDeviceCaps(p.target, LOGPIXELSX);
    p.dpiY = GetDeviceCaps(p.target, LOGPIXELSY);
}

// -- ヘッダー・フッター --

static HFONT makeHFFont(const Preview &p, const std::wstring &name, int size, int style) {
    // Notepad++ の印刷結果に合わせる（2026-11-16 に PDF で測定）：大きさは「文字の高さ」ではなく「セルの高さ」（正の lfHeight）で、
    // 既定は 9。たとえば 10pt の Times New Roman は画面の 10pt より小さく印刷される
    LOGFONTW lf{};
    lf.lfHeight = MulDiv(size > 0 ? size : 9, p.dpiY, 72);
    lf.lfWeight = (style & 1) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (style & 2) ? TRUE : FALSE;
    lf.lfUnderline = (style & 4) ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, name.empty() ? L"Arial" : name.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

static int fontLineHeight(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight + tm.tmExternalLeading;
}

static std::wstring expandVars(const Preview &p, const std::wstring &s, int page) {
    auto rep = [](std::wstring &t, const std::wstring &k, const std::wstring &v) {
        size_t pos = 0;
        while ((pos = t.find(k, pos)) != std::wstring::npos) {
            t.replace(pos, k.size(), v);
            pos += v.size();
        }
    };
    std::wstring t = s;
    if (t.find(L"$(") == std::wstring::npos) return t;
    std::wstring dir = p.docPath, name = p.docName, namePart = p.docName, ext;
    size_t sl = dir.find_last_of(L"\\/");
    dir = sl == std::wstring::npos ? L"" : dir.substr(0, sl);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        namePart = name.substr(0, dot);
        ext = name.substr(dot);
    }
    wchar_t sd[128] = {}, ld[128] = {}, tm[128] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, sd, 128, nullptr);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, nullptr, nullptr, ld, 128, nullptr);
    // 時刻は秒なし（Notepad++ の印刷結果で確かめた。例「6:49」）
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, tm, 128);
    rep(t, L"$(FULL_CURRENT_PATH)", p.docPath);
    rep(t, L"$(CURRENT_DIRECTORY)", dir);
    rep(t, L"$(FILE_NAME)", name);
    rep(t, L"$(NAME_PART)", namePart);
    rep(t, L"$(EXT_PART)", ext);
    rep(t, L"$(CURRENT_PRINTING_PAGE)", std::to_wstring(page + 1));
    rep(t, L"$(SHORT_DATE)", sd);
    rep(t, L"$(LONG_DATE)", ld);
    rep(t, L"$(TIME)", tm);
    return t;
}

static void drawBand(const Preview &p, HDC dc, const std::wstring *parts, const std::wstring &fontName, int size, int style,
                     int textTop, int lineY, bool isHeader, int page, int ox, int oy) {
    HFONT f = makeHFFont(p, fontName, size, style);
    HGDIOBJ oldF = SelectObject(dc, f);
    // 文字の背景を白で塗る（Notepad++ と同じ。右の文字が長く左へはみ出すと、先に描いた左・中央の文字は隠れる）
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 255));
    SetTextColor(dc, RGB(0, 0, 0));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    int h = tm.tmHeight + tm.tmExternalLeading;
    // ヘッダーの文字は行の上に tmExternalLeading 空けて置かれる（フッターは行の上端から。Notepad++ の印刷結果で確かめた）
    if (isHeader) textTop += tm.tmExternalLeading;
    RECT r{p.marginRect.left + ox, textTop + oy, p.marginRect.right + ox, textTop + oy + h};
    const UINT align[3] = {DT_LEFT, DT_CENTER, DT_RIGHT};
    for (int i = 0; i < 3; ++i) {
        if (parts[i].empty()) continue;
        std::wstring t = expandVars(p, parts[i], page);
        RECT ri = r;
        // 左寄せの文字だけ、Notepad++ の印刷では少し右から始まる（600dpi で 5 ドット。測った値）
        if (i == 0) ri.left += MulDiv(5, p.dpiX, 600);
        DrawTextW(dc, t.c_str(), static_cast<int>(t.size()), &ri, align[i] | DT_SINGLELINE | DT_NOPREFIX | DT_TOP);
    }
    // 線は既定の黒いペン（細い線）。Notepad++ の PDF では約 0.37pt の線になる
    HGDIOBJ oldP = SelectObject(dc, GetStockObject(BLACK_PEN));
    MoveToEx(dc, p.marginRect.left + ox, lineY + oy, nullptr);
    LineTo(dc, p.marginRect.right + ox, lineY + oy);
    SelectObject(dc, oldP);
    SelectObject(dc, oldF);
    DeleteObject(f);
}

static void drawHeaderFooter(const Preview &p, HDC dc, int page, int ox, int oy) {
    if (p.cfg.hasHeader()) {
        int h = p.headerH;
        // 線は文字の行から 行の高さ/4（四捨五入）離す（Notepad++ の印刷結果で確かめた）
        drawBand(p, dc, p.cfg.header, p.cfg.headerFont, p.cfg.headerSize, p.cfg.headerStyle, p.marginRect.top,
                 p.marginRect.top + h + (h + 2) / 4, true, page, ox, oy);
    }
    if (p.cfg.hasFooter()) {
        int h = p.footerH;
        drawBand(p, dc, p.cfg.footer, p.cfg.footerFont, p.cfg.footerSize, p.cfg.footerStyle, p.marginRect.bottom - h,
                 p.marginRect.bottom - h - (h + 2) / 4, false, page, ox, oy);
    }
}

// -- ページの組み立て --

/// Scintilla にページを組ませる。rc は hdc の座標。戻り値は次のページの先頭
static intptr_t formatRange(Preview &p, bool draw, HDC hdc, HDC target, int ox, int oy, intptr_t from, intptr_t to) {
    SciRect rc{p.body.left + ox, p.body.top + oy, p.body.right + ox, p.body.bottom + oy};
    SciRect pg{ox, oy, p.physW + ox, p.physH + oy};
    if (p.useFull) {
        SciRangeToFormatFull fr{hdc, target, rc, pg, from, to};
        return sci(p.sci, SCI_FORMATRANGEFULL, draw ? TRUE : FALSE, reinterpret_cast<LPARAM>(&fr));
    }
    SciRangeToFormat fr{hdc, target, rc, pg, static_cast<long>(from), static_cast<long>(to)};
    return sci(p.sci, SCI_FORMATRANGE, draw ? TRUE : FALSE, reinterpret_cast<LPARAM>(&fr));
}

/// ページを組むときの終わりの位置。formFeedPageBreak が有効なら、pos より後ろの最初の改ページ文字（FF）の直前まで
/// （Notepad++ の印刷結果で確かめた振る舞い：FF のある行で改ページ。行頭の FF ならその行から次のページ、
///   行の途中・行末の FF ならその行までで改ページ。続けて並んだ FF で白紙のページはできない）
static intptr_t pageEnd(const Preview &p, intptr_t pos) {
    if (!p.cfg.formFeed) return p.rangeEnd;
    const char *text = reinterpret_cast<const char *>(sci(p.sci, SCI_GETCHARACTERPOINTER));
    if (!text) return p.rangeEnd;
    for (intptr_t i = pos + 1; i < p.rangeEnd; ++i)
        if (text[i] == '\f') return i - 1;
    return p.rangeEnd;
}

static void drawPage(Preview &p, HDC hdc, HDC target, int page, int ox, int oy) {
    // Scintilla は描いたあと DC に切り抜き範囲と TA_BASELINE を残すので、ヘッダー・フッターの前に元に戻す
    int saved = SaveDC(hdc);
    formatRange(p, true, hdc, target, ox, oy, p.starts[page], p.ends[page]);
    RestoreDC(hdc, saved);
    SetTextAlign(hdc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    drawHeaderFooter(p, hdc, page, ox, oy);
}

static void paginate(Preview &p) {
    p.starts.clear();
    p.ends.clear();
    if (!p.target || p.rangeEnd <= p.rangeStart) return;
    if (p.body.right - p.body.left < 10 || p.body.bottom - p.body.top < 10) {
        p.error = TR(L"余白が大きすぎて、本文を置く場所がありません。", L"The margins are too large; there is no room for the text.");
        return;
    }
    intptr_t pos = p.rangeStart;
    while (pos < p.rangeEnd && p.starts.size() < 100000) {
        intptr_t end = pageEnd(p, pos);
        intptr_t next = formatRange(p, false, p.target, p.target, -p.offX, -p.offY, pos, end);
        if (next == 0 && p.useFull && p.starts.empty()) {
            p.useFull = false;  // 古い Scintilla（SCI_FORMATRANGEFULL がない）
            next = formatRange(p, false, p.target, p.target, -p.offX, -p.offY, pos, end);
        }
        p.starts.push_back(pos);
        p.ends.push_back(end);
        if (next <= pos) break;
        pos = next;
    }
}

/// 1ページの行数・1行の文字数（Scintilla の印刷と同じ考え方で数える）
static void measure(Preview &p) {
    p.linesPerPage = p.halfChars = p.fullChars = 0;
    if (!p.target) return;
    int maxA = 1, maxD = 1;
    int quality = static_cast<int>(sci(p.sci, SCI_GETFONTQUALITY));
    auto makeFont = [&](int s) {
        LOGFONTW lf{};
        int frac = static_cast<int>(sci(p.sci, SCI_STYLEGETSIZEFRACTIONAL, s));
        lf.lfHeight = -std::abs(static_cast<LONG>(std::lround(MulDiv(frac, p.dpiY, 72) / 100.0)));
        lf.lfWeight = static_cast<LONG>(sci(p.sci, SCI_STYLEGETWEIGHT, s));
        lf.lfItalic = sci(p.sci, SCI_STYLEGETITALIC, s) ? TRUE : FALSE;
        lf.lfCharSet = static_cast<BYTE>(sci(p.sci, SCI_STYLEGETCHARACTERSET, s));
        lf.lfQuality = quality == 0 ? DEFAULT_QUALITY : quality == 1 ? NONANTIALIASED_QUALITY : quality == 2 ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
        wcsncpy_s(lf.lfFaceName, utf8ToWide(styleFont(p.sci, s)).c_str(), _TRUNCATE);
        return CreateFontIndirectW(&lf);
    };
    std::vector<std::wstring> seen;
    for (int s = 0; s < 256; ++s) {
        std::wstring key = utf8ToWide(styleFont(p.sci, s)) + L"|" +
                           std::to_wstring(sci(p.sci, SCI_STYLEGETSIZEFRACTIONAL, s)) + L"|" +
                           std::to_wstring(sci(p.sci, SCI_STYLEGETWEIGHT, s)) + L"|" +
                           std::to_wstring(sci(p.sci, SCI_STYLEGETITALIC, s)) + L"|" +
                           std::to_wstring(sci(p.sci, SCI_STYLEGETCHARACTERSET, s));
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        HFONT f = makeFont(s);
        HGDIOBJ old = SelectObject(p.target, f);
        TEXTMETRICW tm{};
        GetTextMetricsW(p.target, &tm);
        SelectObject(p.target, old);
        DeleteObject(f);
        maxA = std::max<int>(maxA, tm.tmAscent);
        maxD = std::max<int>(maxD, tm.tmDescent);
    }
    int lineH = maxA + maxD + static_cast<int>(sci(p.sci, SCI_GETEXTRAASCENT)) + static_cast<int>(sci(p.sci, SCI_GETEXTRADESCENT));
    if (lineH > 0) p.linesPerPage = (p.body.bottom - p.body.top) / lineH;

    int fixed = static_cast<int>(sci(p.sci, SCI_GETMARGINLEFT));
    if (p.lineNumbers) {
        HFONT f = makeFont(STYLE_LINENUMBER);
        HGDIOBJ old = SelectObject(p.target, f);
        SIZE sz{};
        GetTextExtentPoint32W(p.target, L"99999   ", 8, &sz);
        SelectObject(p.target, old);
        DeleteObject(f);
        fixed += sz.cx;
    }
    int textW = p.body.right - p.body.left - fixed - static_cast<int>(sci(p.sci, SCI_GETMARGINRIGHT));
    HFONT f = makeFont(STYLE_DEFAULT);
    HGDIOBJ old = SelectObject(p.target, f);
    SIZE half{}, full{};
    GetTextExtentPoint32W(p.target, L"0", 1, &half);
    GetTextExtentPoint32W(p.target, L"あ", 1, &full);
    SelectObject(p.target, old);
    DeleteObject(f);
    if (full.cx < half.cx * 3 / 2) full.cx = half.cx * 2;  // 全角の字がないフォント
    if (half.cx > 0 && textW > 0) p.halfChars = textW / half.cx;
    if (full.cx > 0 && textW > 0) p.fullChars = textW / full.cx;
}

static void layoutPage(Preview &p) {
    p.clamped = false;
    if (!p.target) return;
    int user[4] = {mmToPx(p.margin[0], p.dpiY), mmToPx(p.margin[1], p.dpiY), mmToPx(p.margin[2], p.dpiX),
                   mmToPx(p.margin[3], p.dpiX)};
    int phys[4] = {p.offY, p.physH - p.offY - p.resY, p.offX, p.physW - p.offX - p.resX};
    int m[4];
    for (int i = 0; i < 4; ++i) {
        m[i] = std::max(user[i], phys[i]);
        if (phys[i] > user[i]) p.clamped = true;
    }
    p.marginRect = {m[2], m[0], p.physW - m[3], p.physH - m[1]};
    // Notepad++ の印刷は、余白のさらに内側に「フッターのフォントの行の高さ×1.5」を四辺とも空ける。
    // フッターの文字が空でも同じ（Microsoft Print to PDF で Notepad++ の印刷結果を測って確かめた：
    // 既定の Arial 9 で 600dpi 115 ドット＝約 4.9mm、フッターを MS Gothic 12 にすると 150 ドット）
    {
        HFONT f = makeHFFont(p, p.cfg.footerFont, p.cfg.footerSize, p.cfg.footerStyle);
        int lh = fontLineHeight(p.target, f);
        DeleteObject(f);
        p.inset = lh + lh / 2;
    }
    p.marginRect = {p.marginRect.left + p.inset, p.marginRect.top + p.inset, p.marginRect.right - p.inset,
                    p.marginRect.bottom - p.inset};
    p.body = p.marginRect;
    p.headerH = p.footerH = 0;
    if (p.cfg.hasHeader()) {
        HFONT f = makeHFFont(p, p.cfg.headerFont, p.cfg.headerSize, p.cfg.headerStyle);
        p.headerH = fontLineHeight(p.target, f);
        DeleteObject(f);
        p.body.top += p.headerH + p.headerH / 2;
    }
    if (p.cfg.hasFooter()) {
        HFONT f = makeHFFont(p, p.cfg.footerFont, p.cfg.footerSize, p.cfg.footerStyle);
        p.footerH = fontLineHeight(p.target, f);
        DeleteObject(f);
        p.body.bottom -= p.footerH + p.footerH / 2;
    }
}

// -- 表示の更新 --

static void updateInfo(Preview &p) {
    std::wstring t;
    if (!p.error.empty()) {
        t = p.error;
    } else {
        auto mm = [](int px, int dpi) { return fmtNum(std::round(px * 254.0 / dpi) / 10.0); };
        std::wstring pages = std::to_wstring(p.starts.size());
        bool sel = p.rangeStart != 0 || p.rangeEnd != p.docLen;
        std::wstring mt = mm(p.marginRect.top - p.inset, p.dpiY), mb = mm(p.physH - p.marginRect.bottom - p.inset, p.dpiY),
                     ml = mm(p.marginRect.left - p.inset, p.dpiX), mr = mm(p.physW - p.marginRect.right - p.inset, p.dpiX);
        if (!g_en) {
            t = L"本文 " + fmtNum(p.sizeFrac / 100.0) + L"pt・" + utf8ToWide(p.font);
            t += L"\r\n1ページ " + std::to_wstring(p.linesPerPage) + L"行・1行 半角 " + std::to_wstring(p.halfChars) + L"字（全角 " +
                 std::to_wstring(p.fullChars) + L"字）";
            t += p.starts.empty() ? L"\r\n印刷するものがありません" : L"\r\n全 " + pages + L" ページ";
            if (sel) t += L"（選択範囲）";
            if (p.lineNumbers) t += L"・行番号あり";
            if (p.cfg.formFeed) t += L"・改ページ文字で改ページ";
            if (p.clamped)
                t += L"\r\n\r\nプリンターの印刷できない端に合わせて、余白は 上 " + mt + L"・下 " + mb + L"・左 " + ml + L"・右 " + mr +
                     L" mm になります";
            t += L"\r\n\r\n本文は余白のさらに内側に " + mm(p.inset, p.dpiX) + L" mm 空けて置かれます（Notepad++ の印刷と同じ）";
        } else {
            t = L"Text " + fmtNum(p.sizeFrac / 100.0) + L" pt, " + utf8ToWide(p.font);
            t += L"\r\n" + std::to_wstring(p.linesPerPage) + L" lines per page, " + std::to_wstring(p.halfChars) +
                 L" characters per line (" + std::to_wstring(p.fullChars) + L" full-width)";
            t += p.starts.empty() ? L"\r\nNothing to print" : L"\r\n" + pages + (p.starts.size() == 1 ? L" page" : L" pages");
            if (sel) t += L" (selection)";
            if (p.lineNumbers) t += L", line numbers";
            if (p.cfg.formFeed) t += L", page breaks at form feeds";
            if (p.clamped)
                t += L"\r\n\r\nThe printer cannot print near the paper edge, so the margins will be: top " + mt + L", bottom " +
                     mb + L", left " + ml + L", right " + mr + L" mm";
            t += L"\r\n\r\nThe text is placed a further " + mm(p.inset, p.dpiX) +
                 L" mm inside the margins (same as Notepad++ printing)";
        }
    }
    SetWindowTextW(p.sInfo, t.c_str());
    EnableWindow(p.bPrint, !p.starts.empty() && p.error.empty());
}

static void updatePageLabel(Preview &p) {
    int n = static_cast<int>(p.starts.size());
    std::wstring t = (n == 0 ? std::wstring(L"0 / 0") : std::to_wstring(p.cur + 1) + L" / " + std::to_wstring(n)) +
                     TR(L" ページ", L"");
    if (g_en) t = L"Page " + t;
    SetWindowTextW(p.sPage, t.c_str());
    EnableWindow(p.bPrev, p.cur > 0);
    EnableWindow(p.bNext, p.cur + 1 < n);
}

struct Geo {
    int pageW, pageH, contentW, contentH, cw, ch, m;
};

static Geo geo(const Preview &p) {
    RECT cr{};
    GetClientRect(p.view, &cr);
    Geo g{};
    g.cw = cr.right;
    g.ch = cr.bottom;
    g.m = S(p, 16);
    if (p.physW <= 0 || p.physH <= 0) return g;
    double wmm = p.physW * 25.4 / p.dpiX, hmm = p.physH * 25.4 / p.dpiY;
    double ppm;
    if (p.zoom == 0) ppm = std::min((g.cw - 2.0 * g.m) / wmm, (g.ch - 2.0 * g.m) / hmm);
    else if (p.zoom == 1) ppm = (g.cw - 2.0 * g.m) / wmm;
    else ppm = p.dpi / 25.4;
    ppm = std::max(ppm, 0.05);
    g.pageW = std::max(1, static_cast<int>(std::lround(wmm * ppm)));
    g.pageH = std::max(1, static_cast<int>(std::lround(hmm * ppm)));
    g.contentW = g.pageW + 2 * g.m;
    g.contentH = g.pageH + 2 * g.m;
    return g;
}

static void updateView(Preview &p) {
    if (!p.view) return;
    ShowScrollBar(p.view, SB_VERT, p.zoom != 0);
    ShowScrollBar(p.view, SB_HORZ, p.zoom == 2);
    Geo g = geo(p);
    int maxY = std::max(0, g.contentH - g.ch), maxX = std::max(0, g.contentW - g.cw);
    p.scrollY = std::clamp(p.scrollY, 0, maxY);
    p.scrollX = std::clamp(p.scrollX, 0, maxX);
    if (p.zoom != 0) {
        SCROLLINFO si{sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL, 0, std::max(0, g.contentH - 1), static_cast<UINT>(g.ch), p.scrollY, 0};
        SetScrollInfo(p.view, SB_VERT, &si, TRUE);
    }
    if (p.zoom == 2) {
        SCROLLINFO si{sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL, 0, std::max(0, g.contentW - 1), static_cast<UINT>(g.cw), p.scrollX, 0};
        SetScrollInfo(p.view, SB_HORZ, &si, TRUE);
    }
    InvalidateRect(p.view, nullptr, FALSE);
}

static void setPage(Preview &p, int n, bool toBottom = false) {
    int count = static_cast<int>(p.starts.size());
    if (count == 0) return;
    n = std::clamp(n, 0, count - 1);
    if (n == p.cur) return;
    p.cur = n;
    p.scrollY = toBottom ? INT_MAX / 2 : 0;
    updatePageLabel(p);
    updateView(p);
}

/// 設定を変えたあとに組み直す
static void rebuild(Preview &p, bool newTarget) {
    p.error.clear();
    if (newTarget) openTarget(p);
    restyle(p);
    layoutPage(p);
    paginate(p);
    measure(p);
    dropCache(p);
    if (p.cur >= static_cast<int>(p.starts.size())) p.cur = std::max(0, static_cast<int>(p.starts.size()) - 1);
    updateInfo(p);
    updatePageLabel(p);
    updateView(p);
}

// -- 描く --

static void ensureEmf(Preview &p) {
    if (p.emf && p.emfPage == p.cur) return;
    if (p.emf) DeleteEnhMetaFile(p.emf);
    p.emf = nullptr;
    if (p.starts.empty() || !p.target) return;
    // 枠は 0.01mm。プリンターの HORZSIZE／HORZRES と同じ比で換算し、EMF の座標＝用紙の左上からのプリンターの座標にする
    int hs = GetDeviceCaps(p.target, HORZSIZE), vs = GetDeviceCaps(p.target, VERTSIZE);
    RECT frame{0, 0, MulDiv(p.physW, hs * 100, p.resX), MulDiv(p.physH, vs * 100, p.resY)};
    HDC dc = CreateEnhMetaFileW(p.target, nullptr, &frame, nullptr);
    if (!dc) return;
    drawPage(p, dc, p.target, p.cur, 0, 0);
    p.emf = CloseEnhMetaFile(dc);
    p.emfPage = p.cur;
}

static void ensureBitmap(Preview &p, HDC ref, int w, int h) {
    if (p.bmp && p.bmpPage == p.cur && p.bmpW == w && p.bmpH == h) return;
    if (p.bmp) DeleteObject(p.bmp);
    p.bmp = CreateCompatibleBitmap(ref, w, h);
    p.bmpPage = p.cur;
    p.bmpW = w;
    p.bmpH = h;
    if (!p.bmp) return;
    ensureEmf(p);
    HDC mem = CreateCompatibleDC(ref);
    HGDIOBJ old = SelectObject(mem, p.bmp);
    RECT r{0, 0, w, h};
    FillRect(mem, &r, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if (p.emf) PlayEnhMetaFile(mem, p.emf, &r);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void paintView(Preview &p, HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (cr.right > 0 && cr.bottom > 0) {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP mb = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ oldB = SelectObject(mem, mb);
        HBRUSH bg = CreateSolidBrush(p.viewBg);
        FillRect(mem, &cr, bg);
        DeleteObject(bg);
        if (!p.starts.empty() && p.error.empty()) {
            Geo g = geo(p);
            int x = g.contentW <= g.cw ? (g.cw - g.pageW) / 2 : g.m - p.scrollX;
            int y = p.zoom == 0 ? (g.ch - g.pageH) / 2 : (g.contentH <= g.ch ? (g.ch - g.pageH) / 2 : g.m - p.scrollY);
            ensureBitmap(p, dc, g.pageW, g.pageH);
            RECT sh{x + S(p, 3), y + S(p, 3), x + g.pageW + S(p, 3), y + g.pageH + S(p, 3)};
            HBRUSH sb = CreateSolidBrush(RGB(0x60, 0x60, 0x60));
            FillRect(mem, &sh, sb);
            DeleteObject(sb);
            if (p.bmp) {
                HDC bdc = CreateCompatibleDC(dc);
                HGDIOBJ ob = SelectObject(bdc, p.bmp);
                BitBlt(mem, x, y, g.pageW, g.pageH, bdc, 0, 0, SRCCOPY);
                SelectObject(bdc, ob);
                DeleteDC(bdc);
            }
        } else {
            const wchar_t *msg = p.error.empty() ? TR(L"印刷するものがありません", L"Nothing to print")
                                                 : TR(L"プレビューを表示できません", L"Cannot show the preview");
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, p.viewText);
            HGDIOBJ of = SelectObject(mem, p.uiFont);
            DrawTextW(mem, msg, -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, of);
        }
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldB);
        DeleteObject(mb);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

static void scrollBy(Preview &p, int dx, int dy) {
    p.scrollX += dx;
    p.scrollY += dy;
    updateView(p);
}

static void onWheel(Preview &p, int delta) {
    p.wheelAcc += delta;
    while (std::abs(p.wheelAcc) >= WHEEL_DELTA) {
        int dir = p.wheelAcc > 0 ? -1 : 1;  // 下へ回すと +1
        p.wheelAcc += dir * WHEEL_DELTA;
        if (p.zoom == 0) {
            setPage(p, p.cur + dir);
            continue;
        }
        Geo g = geo(p);
        int maxY = std::max(0, g.contentH - g.ch);
        if (dir > 0 && p.scrollY >= maxY) {
            if (p.cur + 1 < static_cast<int>(p.starts.size())) setPage(p, p.cur + 1);
        } else if (dir < 0 && p.scrollY <= 0) {
            if (p.cur > 0) setPage(p, p.cur - 1, true);
        } else {
            scrollBy(p, 0, dir * S(p, 120));
        }
    }
}

static void onScroll(Preview &p, int bar, int code) {
    Geo g = geo(p);
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(p.view, bar, &si);
    int &pos = bar == SB_VERT ? p.scrollY : p.scrollX;
    int pageSz = bar == SB_VERT ? g.ch : g.cw;
    switch (code) {
    case SB_LINEUP: pos -= S(p, 40); break;
    case SB_LINEDOWN: pos += S(p, 40); break;
    case SB_PAGEUP: pos -= pageSz * 9 / 10; break;
    case SB_PAGEDOWN: pos += pageSz * 9 / 10; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    case SB_TOP: pos = 0; break;
    case SB_BOTTOM: pos = INT_MAX / 2; break;
    default: return;
    }
    updateView(p);
}

static LRESULT CALLBACK viewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Preview *p = g_pv;
    if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintView(*p, hwnd); return 0;
    case WM_SIZE: updateView(*p); return 0;
    case WM_MOUSEWHEEL: onWheel(*p, GET_WHEEL_DELTA_WPARAM(wp)); return 0;
    case WM_VSCROLL: onScroll(*p, SB_VERT, LOWORD(wp)); return 0;
    case WM_HSCROLL: onScroll(*p, SB_HORZ, LOWORD(wp)); return 0;
    case WM_LBUTTONDOWN: SetFocus(hwnd); return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_PRIOR: setPage(*p, p->cur - 1); return 0;
        case VK_NEXT: setPage(*p, p->cur + 1); return 0;
        case VK_HOME: setPage(*p, 0); return 0;
        case VK_END: setPage(*p, static_cast<int>(p->starts.size()) - 1); return 0;
        case VK_UP: if (p->zoom != 0) scrollBy(*p, 0, -S(*p, 40)); else setPage(*p, p->cur - 1); return 0;
        case VK_DOWN: if (p->zoom != 0) scrollBy(*p, 0, S(*p, 40)); else setPage(*p, p->cur + 1); return 0;
        case VK_LEFT: if (p->zoom == 2) scrollBy(*p, -S(*p, 40), 0); else setPage(*p, p->cur - 1); return 0;
        case VK_RIGHT: if (p->zoom == 2) scrollBy(*p, S(*p, 40), 0); else setPage(*p, p->cur + 1); return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void registerViewClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = viewProc;
    wc.hInstance = g_module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = VIEW_CLASS;
    done = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// -- 窓の部品 --

static HWND mk(Preview &p, const wchar_t *cls, const wchar_t *text, DWORD style, int id, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, p.dlg,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
    return h;
}

static void setUiFont(Preview &p) {
    if (p.uiFont) DeleteObject(p.uiFont);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, p.dpi);
    p.uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    EnumChildWindows(
        p.dlg,
        [](HWND h, LPARAM lp) -> BOOL {
            SendMessageW(h, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(p.uiFont));
}

static void layout(Preview &p) {
    RECT rc{};
    GetClientRect(p.dlg, &rc);
    const int pad = S(p, 12), gap = S(p, 6), rowH = S(p, 24), lblW = S(p, 80), panelW = S(p, 320), listH = S(p, 320);
    const int x = pad, w = panelW - 2 * pad;
    int y = pad;
    auto row = [&](HWND label, HWND ctrl, int ctrlW, bool combo) {
        MoveWindow(label, x, y + S(p, 3), lblW, rowH, TRUE);
        MoveWindow(ctrl, x + lblW, y, ctrlW > 0 ? ctrlW : w - lblW, combo ? listH : rowH, TRUE);
        y += rowH + gap;
    };
    row(p.lbl[LB_PRINTER], p.cPrinter, 0, true);
    row(p.lbl[LB_PAPER], p.cPaper, 0, true);
    row(p.lbl[LB_ORIENT], p.cOrient, S(p, 90), true);
    y += S(p, 10);
    row(p.lbl[LB_FONT], p.cFont, 0, true);
    MoveWindow(p.lbl[LB_PT], x + lblW + S(p, 66), y + S(p, 3), S(p, 30), rowH, TRUE);
    row(p.lbl[LB_SIZE], p.eSize, S(p, 60), false);
    row(p.lbl[LB_COLOR], p.cColor, 0, true);
    y += S(p, 10);
    // 見出しの高さは行より 3 ドット低くする（下の入力欄の上の線に重ねて隠さないように）
    MoveWindow(p.lbl[LB_MARGIN], x, y + S(p, 3), w, rowH - S(p, 3), TRUE);
    y += rowH;
    const int mLbl = S(p, 50), mEdit = S(p, 60), col2 = S(p, 145);  // 見出しは英語（Bottom など）が入る幅
    MoveWindow(p.lbl[LB_MT], x + S(p, 8), y + S(p, 3), mLbl, rowH, TRUE);
    MoveWindow(p.eMargin[0], x + S(p, 8) + mLbl, y, mEdit, rowH, TRUE);
    MoveWindow(p.lbl[LB_MB], x + col2, y + S(p, 3), mLbl, rowH, TRUE);
    MoveWindow(p.eMargin[1], x + col2 + mLbl, y, mEdit, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(p.lbl[LB_ML], x + S(p, 8), y + S(p, 3), mLbl, rowH, TRUE);
    MoveWindow(p.eMargin[2], x + S(p, 8) + mLbl, y, mEdit, rowH, TRUE);
    MoveWindow(p.lbl[LB_MR], x + col2, y + S(p, 3), mLbl, rowH, TRUE);
    MoveWindow(p.eMargin[3], x + col2 + mLbl, y, mEdit, rowH, TRUE);
    y += rowH + S(p, 10);
    MoveWindow(p.bReset, x, y, S(p, 220), S(p, 28), TRUE);
    y += S(p, 28) + S(p, 14);
    MoveWindow(p.chkSel, x, y, w, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(p.chkLn, x, y, w, rowH, TRUE);
    y += rowH + gap;
    const int bottomH = S(p, 30);
    const int bottomY = rc.bottom - pad - bottomH;
    MoveWindow(p.sStatus, x, bottomY - S(p, 26), w, S(p, 22), TRUE);
    const int langY = bottomY - S(p, 26) - rowH - gap;
    MoveWindow(p.lbl[LB_LANG], x, langY + S(p, 3), lblW, rowH, TRUE);
    MoveWindow(p.cLang, x + lblW, langY, w - lblW, listH, TRUE);
    MoveWindow(p.sInfo, x, y, w, std::max(S(p, 40), langY - S(p, 8) - y), TRUE);
    MoveWindow(p.lbl[LB_COPIES], x, bottomY + S(p, 6), S(p, 40), rowH, TRUE);
    MoveWindow(p.eCopies, x + S(p, 40), bottomY + S(p, 3), S(p, 44), rowH, TRUE);
    MoveWindow(p.bPrint, x + w - S(p, 186), bottomY, S(p, 90), bottomH, TRUE);
    MoveWindow(p.bClose, x + w - S(p, 90), bottomY, S(p, 90), bottomH, TRUE);

    // 右：ページ送りと表示の大きさ、プレビュー
    const int rx = panelW, rw = rc.right - pad - rx;
    const int barH = S(p, 28);
    MoveWindow(p.bPrev, rx, pad, S(p, 40), barH, TRUE);
    MoveWindow(p.sPage, rx + S(p, 44), pad, S(p, 120), barH, TRUE);
    MoveWindow(p.bNext, rx + S(p, 168), pad, S(p, 40), barH, TRUE);
    MoveWindow(p.cZoom, rx + S(p, 224), pad + S(p, 2), S(p, 150), listH, TRUE);
    MoveWindow(p.view, rx, pad + barH + gap, std::max(10, rw), std::max(10, static_cast<int>(rc.bottom) - pad - (pad + barH + gap)), TRUE);
}

static void fillPaperCombo(Preview &p) {
    SendMessageW(p.cPaper, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < p.papers.size(); ++i) {
        const auto &pp = p.papers[i];
        std::wstring t = pp.name + TR(L"（", L" (") + fmtNum(pp.size.x / 10.0) + L"×" + fmtNum(pp.size.y / 10.0) +
                         TR(L"mm）", L" mm)");
        SendMessageW(p.cPaper, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        if (pp.id == static_cast<WORD>(p.paper)) sel = static_cast<int>(i);
    }
    SendMessageW(p.cPaper, CB_SETCURSEL, sel, 0);
}

static int CALLBACK enumFontProc(const LOGFONTW *lf, const TEXTMETRICW *, DWORD, LPARAM lp) {
    auto *v = reinterpret_cast<std::vector<std::wstring> *>(lp);
    if (lf->lfFaceName[0] != L'@') v->emplace_back(lf->lfFaceName);
    return 1;
}

static void fillFontCombo(Preview &p) {
    std::vector<std::wstring> fonts;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, enumFontProc, reinterpret_cast<LPARAM>(&fonts), 0);
    ReleaseDC(nullptr, dc);
    std::wstring curFont = utf8ToWide(p.font);
    fonts.push_back(curFont);
    std::sort(fonts.begin(), fonts.end(), [](const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    fonts.erase(std::unique(fonts.begin(), fonts.end()), fonts.end());
    SendMessageW(p.cFont, CB_RESETCONTENT, 0, 0);
    for (const auto &f : fonts) SendMessageW(p.cFont, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f.c_str()));
    SendMessageW(p.cFont, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(curFont.c_str()));
}

/// 色の一覧。Notepad++ の印刷設定と同じものに「（Notepad++ の設定）」を付ける
static void fillColorCombo(Preview &p) {
    SendMessageW(p.cColor, CB_RESETCONTENT, 0, 0);
    std::vector<int> modes;
    for (size_t i = 0; i < sizeof(COLOR_CHOICES) / sizeof(COLOR_CHOICES[0]); ++i) {
        const auto &c = COLOR_CHOICES[i];
        std::wstring t = TR(COLOR_TEXTS[i].ja, COLOR_TEXTS[i].en);
        if (c.mode == p.cfg.printOption) t += TR(L"（Notepad++ の設定）", L" (Notepad++ setting)");
        SendMessageW(p.cColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        modes.push_back(c.mode);
    }
    // 一覧にない番号（4：背景色なしのカラーなど）が Notepad++ の設定なら、その項目も足す
    if (std::find(modes.begin(), modes.end(), p.cfg.printOption) == modes.end()) {
        std::wstring t = TR(L"Notepad++ の設定（", L"Notepad++ setting (") + std::to_wstring(p.cfg.printOption) + TR(L"）", L")");
        SendMessageW(p.cColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        modes.push_back(p.cfg.printOption);
    }
    int sel = 0;
    for (size_t i = 0; i < modes.size(); ++i)
        if (modes[i] == p.colorMode) sel = static_cast<int>(i);
    SendMessageW(p.cColor, CB_SETCURSEL, sel, 0);
    for (size_t i = 0; i < modes.size(); ++i) SendMessageW(p.cColor, CB_SETITEMDATA, i, modes[i]);
}

static void fillEdits(Preview &p) {
    p.loading = true;
    SetWindowTextW(p.eSize, fmtNum(p.sizeFrac / 100.0).c_str());
    for (int i = 0; i < 4; ++i) SetWindowTextW(p.eMargin[i], fmtNum(p.margin[i]).c_str());
    std::wstring curFont = utf8ToWide(p.font);
    if (SendMessageW(p.cFont, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(curFont.c_str())) == CB_ERR) {
        SendMessageW(p.cFont, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(curFont.c_str()));
        SendMessageW(p.cFont, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(curFont.c_str()));
    }
    p.loading = false;
}

/// 入力欄の値を読み、変わっていれば組み直す
// 数値欄のマウスホイール（2026-11-16 ユーザー決定）：その欄をクリックして入力中のときだけ増減する。
// 入力中でなければ何もしない（プレビューのスクロール中などに、上を通っただけで値が変わらないように）。
// 刻み：文字サイズ 0.5pt、余白 1mm、部数 1。値を書き換えると EN_CHANGE から今までどおり 0.6 秒後に反映される
enum WheelKind : DWORD_PTR { WHEEL_SIZE, WHEEL_MARGIN, WHEEL_COPIES };

static LRESULT CALLBACK wheelEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR kind) {
    if (msg == WM_MOUSEWHEEL && GetFocus() == h) {
        // 1 目盛り（WHEEL_DELTA＝120）で 1 刻み。速く回すと 240 などまとめて届くので数え、細かいホイールの端数は貯めておく
        static HWND accFor = nullptr;
        static int acc = 0;
        if (accFor != h) {
            accFor = h;
            acc = 0;
        }
        acc += GET_WHEEL_DELTA_WPARAM(wp);
        int notches = acc / WHEEL_DELTA;
        acc -= notches * WHEEL_DELTA;
        if (notches == 0) return 0;
        const double step = (kind == WHEEL_SIZE ? 0.5 : 1.0) * notches;
        const double lo = kind == WHEEL_MARGIN ? 0.0 : 1.0;
        const double hi = kind == WHEEL_COPIES ? 999.0 : 200.0;
        double v = 0;
        if (!parseNum(windowText(h), lo, hi, v)) v = lo;
        const double unit = kind == WHEEL_SIZE ? 0.5 : 1.0;
        v = std::round((v + step) / unit) * unit;
        v = std::clamp(v, lo, hi);
        std::wstring t = kind == WHEEL_COPIES ? std::to_wstring(static_cast<int>(v)) : fmtNum(v);
        SetWindowTextW(h, t.c_str());
        SendMessageW(h, EM_SETSEL, t.size(), t.size());
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, wheelEditProc, 1);
    return DefSubclassProc(h, msg, wp, lp);
}

static void applyEdits(Preview &p) {
    KillTimer(p.dlg, TIMER_APPLY);
    bool changed = false;
    double v;
    if (parseNum(windowText(p.eSize), 1, 200, v)) {
        int f = static_cast<int>(std::lround(v * 100));
        if (f != p.sizeFrac) {
            p.sizeFrac = f;
            changed = true;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (parseNum(windowText(p.eMargin[i]), 0, 200, v) && std::fabs(v - p.margin[i]) > 1e-6) {
            p.margin[i] = v;
            changed = true;
        }
    }
    if (changed) rebuild(p, false);
}

static void saveSettings(Preview &p) {
    applyEdits(p);
    iniSet(L"Print", L"FontName", p.font != p.nppFont ? utf8ToWide(p.font) : L"");
    iniSet(L"Print", L"FontSize", p.sizeFrac != p.nppSizeFrac ? fmtNum(p.sizeFrac / 100.0) : L"");
    const wchar_t *keys[4] = {L"MarginTop", L"MarginBottom", L"MarginLeft", L"MarginRight"};
    for (int i = 0; i < 4; ++i) iniSet(L"Print", keys[i], std::fabs(p.margin[i] - p.cfg.margin[i]) > 1e-6 ? fmtNum(p.margin[i]) : L"");
    iniSet(L"Print", L"Printer", p.printer != defaultPrinter() ? p.printer : L"");
    iniSet(L"Print", L"Paper", p.paper != p.defPaper ? std::to_wstring(p.paper) : L"");
    iniSet(L"Print", L"Orientation", p.orient != p.defOrient ? std::to_wstring(p.orient) : L"");
    iniSet(L"Print", L"ColorMode", p.colorMode != p.cfg.printOption ? std::to_wstring(p.colorMode) : L"");
    iniSet(L"Print", L"LineNumbers", p.lnWanted != p.cfg.lineNumber ? (p.lnWanted ? L"1" : L"0") : L"");
    iniSet(L"View", L"Zoom", std::to_wstring(p.zoom));
}

static void doPrint(Preview &p) {
    applyEdits(p);
    if (p.starts.empty() || !p.error.empty()) return;
    double c = 1;
    if (!parseNum(windowText(p.eCopies), 1, 999, c)) {
        showMessage(p.dlg, TR(L"部数は 1〜999 の数で入れてください。", L"Enter a number of copies from 1 to 999."), MB_ICONWARNING);
        return;
    }
    int copies = static_cast<int>(c);
    HDC dc = CreateDCW(L"WINSPOOL", p.printer.c_str(), nullptr, reinterpret_cast<DEVMODEW *>(p.devmode.data()));
    if (!dc) {
        showMessage(p.dlg, std::wstring(TR(L"プリンターを開けませんでした。\n", L"Could not open the printer.\n")) + p.printer, MB_ICONERROR);
        return;
    }
    std::wstring docName = p.docName.empty() ? L"Notepad++" : p.docName;
    DOCINFOW di{sizeof(di)};
    di.lpszDocName = docName.c_str();
    SetWindowTextW(p.sStatus, TR(L"印刷しています…", L"Printing..."));
    UpdateWindow(p.sStatus);
    HCURSOR oldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    bool ok = false, cancelled = false;
    if (StartDocW(dc, &di) > 0) {
        ok = true;
        // プリンターの座標は印刷できる範囲の左上が原点なので、用紙の左上からの座標をずらして描く
        int ox = -GetDeviceCaps(dc, PHYSICALOFFSETX), oy = -GetDeviceCaps(dc, PHYSICALOFFSETY);
        for (int k = 0; k < copies && ok; ++k) {
            for (size_t i = 0; i < p.starts.size() && ok; ++i) {
                if (StartPage(dc) <= 0) {
                    ok = false;
                    break;
                }
                drawPage(p, dc, dc, static_cast<int>(i), ox, oy);
                if (EndPage(dc) <= 0) ok = false;
            }
        }
        if (ok) EndDoc(dc);
        else AbortDoc(dc);
    } else {
        cancelled = GetLastError() == ERROR_CANCELLED;
    }
    DeleteDC(dc);
    SetCursor(oldCur);
    if (ok) {
        std::wstring t;
        if (!g_en) {
            t = L"印刷しました（" + std::to_wstring(p.starts.size()) + L" ページ";
            if (copies > 1) t += L" × " + std::to_wstring(copies) + L" 部";
            t += L"）";
        } else {
            t = L"Printed (" + std::to_wstring(p.starts.size()) + (p.starts.size() == 1 ? L" page" : L" pages");
            if (copies > 1) t += L" x " + std::to_wstring(copies) + L" copies";
            t += L")";
        }
        SetWindowTextW(p.sStatus, t.c_str());
    } else {
        SetWindowTextW(p.sStatus, cancelled ? TR(L"印刷をやめました", L"Printing was canceled") : L"");
        if (!cancelled) showMessage(p.dlg, std::wstring(TR(L"印刷できませんでした。\n", L"Printing failed.\n")) + p.printer, MB_ICONERROR);
    }
}

/// 表示言語に合わせて、窓の題名・見出し・ボタン・一覧の文字を入れ直す
static void applyTexts(Preview &p) {
    std::wstring title = std::wstring(TR(L"印刷プレビュー - ", L"Print Preview - ")) +
                         (p.docName.empty() ? std::wstring(L"Notepad++") : p.docName) + L" - NppPrintPreview";
    SetWindowTextW(p.dlg, title.c_str());
    const wchar_t *ja[LB_COUNT] = {L"プリンター", L"用紙", L"向き", L"フォント", L"文字サイズ", L"pt", L"色",
                                   L"余白（mm）", L"上", L"下", L"左", L"右", L"部数", L"表示言語"};
    const wchar_t *en[LB_COUNT] = {L"Printer", L"Paper", L"Orientation", L"Font", L"Font size", L"pt", L"Color",
                                   L"Margins (mm)", L"Top", L"Bottom", L"Left", L"Right", L"Copies", L"Language"};
    for (int i = 0; i < LB_COUNT; ++i) SetWindowTextW(p.lbl[i], TR(ja[i], en[i]));
    SetWindowTextW(p.bReset, TR(L"Notepad++ の設定に戻す", L"Reset to Notepad++ settings"));
    SetWindowTextW(p.chkSel, TR(L"選択範囲だけ", L"Selection only"));
    SetWindowTextW(p.chkLn, TR(L"行番号を印刷する", L"Print line numbers"));
    SetWindowTextW(p.bPrint, TR(L"印刷", L"Print"));
    SetWindowTextW(p.bClose, TR(L"閉じる", L"Close"));
    SetWindowTextW(p.sStatus, L"");

    auto refill = [](HWND combo, std::initializer_list<const wchar_t *> items, int sel) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (auto s : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
    };
    refill(p.cOrient, {TR(L"縦", L"Portrait"), TR(L"横", L"Landscape")}, p.orient == DMORIENT_LANDSCAPE ? 1 : 0);
    refill(p.cZoom, {TR(L"ページ全体", L"Whole page"), TR(L"幅に合わせる", L"Fit width"), TR(L"実寸（100%）", L"Actual size (100%)")},
           p.zoom);
    std::wstring lang = iniGet(L"General", L"Language");
    refill(p.cLang, {TR(L"自動（Notepad++ に合わせる）", L"Auto (same as Notepad++)"), L"日本語", L"English"},
           lang == L"ja" ? 1 : lang == L"en" ? 2 : 0);
    fillPaperCombo(p);
    fillColorCombo(p);
    updateInfo(p);
    updatePageLabel(p);
}

static void initPreview(Preview &p) {
    p.dpi = static_cast<int>(GetDpiForWindow(p.dlg));
    if (p.dpi <= 0) p.dpi = 96;
    bool dark = SendMessageW(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
    if (dark) {
        NppDarkColors dc{};
        if (SendMessageW(g_npp._nppHandle, NPPM_GETDARKMODECOLORS, sizeof(dc), reinterpret_cast<LPARAM>(&dc))) {
            p.viewBg = dc.softerBackground;
            p.viewText = dc.text;
        } else {
            p.viewBg = RGB(0x38, 0x38, 0x38);
            p.viewText = RGB(0xE0, 0xE0, 0xE0);
        }
    }

    // 文書
    wchar_t buf[MAX_PATH * 4] = {};
    SendMessageW(g_npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH * 4, reinterpret_cast<LPARAM>(buf));
    p.docPath = buf;
    buf[0] = 0;
    SendMessageW(g_npp._nppHandle, NPPM_GETFILENAME, MAX_PATH * 4, reinterpret_cast<LPARAM>(buf));
    p.docName = buf;

    p.docLen = static_cast<intptr_t>(sci(p.mainSci, SCI_GETLENGTH));
    p.hasSel = !sci(p.mainSci, SCI_GETSELECTIONEMPTY);
    p.selStart = static_cast<intptr_t>(sci(p.mainSci, SCI_GETSELECTIONSTART));
    p.selEnd = static_cast<intptr_t>(sci(p.mainSci, SCI_GETSELECTIONEND));
    if (p.selEnd <= p.selStart) p.hasSel = false;
    p.rangeStart = p.hasSel ? p.selStart : 0;
    p.rangeEnd = p.hasSel ? p.selEnd : p.docLen;

    // 設定：Notepad++ の今の設定が初期値。プラグイン側で変えた値があればそれを使う
    p.cfg = loadPrintCfg();
    p.nppFont = styleFont(p.mainSci, STYLE_DEFAULT);
    p.nppSizeFrac = static_cast<int>(sci(p.mainSci, SCI_STYLEGETSIZEFRACTIONAL, STYLE_DEFAULT));
    if (p.nppSizeFrac <= 0) p.nppSizeFrac = 1000;
    std::wstring v = iniGet(L"Print", L"FontName");
    p.font = v.empty() ? p.nppFont : wideToUtf8(v);
    double d;
    p.sizeFrac = parseNum(iniGet(L"Print", L"FontSize"), 1, 200, d) ? static_cast<int>(std::lround(d * 100)) : p.nppSizeFrac;
    const wchar_t *keys[4] = {L"MarginTop", L"MarginBottom", L"MarginLeft", L"MarginRight"};
    for (int i = 0; i < 4; ++i) p.margin[i] = parseNum(iniGet(L"Print", keys[i]), 0, 200, d) ? d : p.cfg.margin[i];

    p.printers = listPrinters();
    std::wstring savedPrinter = iniGet(L"Print", L"Printer");
    p.printer = defaultPrinter();
    if (!savedPrinter.empty() && std::find(p.printers.begin(), p.printers.end(), savedPrinter) != p.printers.end())
        p.printer = savedPrinter;
    if (p.printer.empty() && !p.printers.empty()) p.printer = p.printers[0];
    loadPapers(p);
    short savedPaper = static_cast<short>(_wtoi(iniGet(L"Print", L"Paper").c_str()));
    p.paper = savedPaper > 0 && paperSupported(p, savedPaper) ? savedPaper : p.defPaper;
    short savedOrient = static_cast<short>(_wtoi(iniGet(L"Print", L"Orientation").c_str()));
    p.orient = savedOrient == DMORIENT_PORTRAIT || savedOrient == DMORIENT_LANDSCAPE ? savedOrient : p.defOrient;
    p.zoom = std::clamp(_wtoi(iniGet(L"View", L"Zoom").c_str()), 0, 2);
    std::wstring cm = iniGet(L"Print", L"ColorMode");
    p.colorMode = cm.empty() ? p.cfg.printOption : std::clamp(_wtoi(cm.c_str()), 0, 5);
    std::wstring ln = iniGet(L"Print", L"LineNumbers");
    p.lnWanted = ln.empty() ? p.cfg.lineNumber : ln != L"0";

    // 部品
    for (int i = 0; i < LB_COUNT; ++i) p.lbl[i] = mk(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_LABEL + i);
    const DWORD comboStyle = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
    const DWORD editStyle = ES_AUTOHSCROLL | WS_TABSTOP;
    p.cPrinter = mk(p, L"COMBOBOX", L"", comboStyle, ID_PRINTER);
    p.cPaper = mk(p, L"COMBOBOX", L"", comboStyle, ID_PAPER);
    p.cOrient = mk(p, L"COMBOBOX", L"", comboStyle, ID_ORIENT);
    p.cFont = mk(p, L"COMBOBOX", L"", comboStyle, ID_FONT);
    p.cColor = mk(p, L"COMBOBOX", L"", comboStyle, ID_COLOR);
    p.eSize = mk(p, L"EDIT", L"", editStyle, ID_SIZE, WS_EX_CLIENTEDGE);
    for (int i = 0; i < 4; ++i) p.eMargin[i] = mk(p, L"EDIT", L"", editStyle, ID_MTOP + i, WS_EX_CLIENTEDGE);
    p.bReset = mk(p, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_RESET);
    p.chkSel = mk(p, L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, ID_SEL);
    p.chkLn = mk(p, L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, ID_LINENUM);
    p.sInfo = mk(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_INFO);
    p.sStatus = mk(p, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_STATUS);
    p.cLang = mk(p, L"COMBOBOX", L"", comboStyle, ID_LANG);
    p.eCopies = mk(p, L"EDIT", L"1", editStyle | ES_NUMBER, ID_COPIES, WS_EX_CLIENTEDGE);
    p.bPrint = mk(p, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, ID_PRINT);
    p.bClose = mk(p, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);
    p.bPrev = mk(p, L"BUTTON", L"◀", BS_PUSHBUTTON | WS_TABSTOP, ID_PREV);
    p.sPage = mk(p, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, ID_PAGE);
    p.bNext = mk(p, L"BUTTON", L"▶", BS_PUSHBUTTON | WS_TABSTOP, ID_NEXT);
    p.cZoom = mk(p, L"COMBOBOX", L"", comboStyle, ID_ZOOM);
    p.view = mk(p, VIEW_CLASS, L"", WS_TABSTOP | WS_HSCROLL | WS_VSCROLL, ID_VIEW);
    SetWindowSubclass(p.eSize, wheelEditProc, 1, WHEEL_SIZE);
    for (int i = 0; i < 4; ++i) SetWindowSubclass(p.eMargin[i], wheelEditProc, 1, WHEEL_MARGIN);
    SetWindowSubclass(p.eCopies, wheelEditProc, 1, WHEEL_COPIES);

    for (const auto &pr : p.printers) SendMessageW(p.cPrinter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pr.c_str()));
    SendMessageW(p.cPrinter, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(p.printer.c_str()));
    fillFontCombo(p);
    applyTexts(p);
    SendMessageW(p.chkSel, BM_SETCHECK, p.hasSel ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(p.chkSel, p.hasSel);
    fillEdits(p);
    setUiFont(p);

    // 画面に出さない Scintilla に、編集中の文書をつなぐ（Notepad++ の画面のビューは触らない）
    p.sci = CreateWindowExW(0, L"Scintilla", L"", WS_CHILD, 0, 0, 200, 200, p.dlg,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SCI)), GetModuleHandleW(nullptr), nullptr);
    if (p.sci) {
        LRESULT doc = sci(p.mainSci, SCI_GETDOCPOINTER);
        sci(p.sci, SCI_SETDOCPOINTER, 0, doc);
    } else {
        p.error = TR(L"Scintilla を作れませんでした。", L"Could not create a Scintilla window.");
    }

    // Notepad++ のダークモードに合わせる
    SendMessageW(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_INIT, reinterpret_cast<LPARAM>(p.dlg));

    // 大きさ：Notepad++ のある画面の作業領域の 85%×90%
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(g_npp._nppHandle, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int ww = (wa.right - wa.left) * 85 / 100, wh = (wa.bottom - wa.top) * 90 / 100;
    SetWindowPos(p.dlg, nullptr, wa.left + ((wa.right - wa.left) - ww) / 2, wa.top + ((wa.bottom - wa.top) - wh) / 2, ww, wh,
                 SWP_NOZORDER);
    layout(p);
    if (p.sci) rebuild(p, true);
    else updateInfo(p);
}

static void destroyPreview(Preview &p) {
    KillTimer(p.dlg, TIMER_APPLY);
    dropCache(p);
    if (p.target) DeleteDC(p.target);
    p.target = nullptr;
    if (p.uiFont) DeleteObject(p.uiFont);
    p.uiFont = nullptr;
}

static INT_PTR CALLBACK dlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Preview *p = g_pv;
    switch (msg) {
    case WM_INITDIALOG:
        g_pv = p = reinterpret_cast<Preview *>(lp);
        p->dlg = hwnd;
        initPreview(*p);
        SetFocus(p->view);
        return FALSE;
    case WM_SIZE:
        if (p && p->view) layout(*p);
        return TRUE;
    case WM_GETMINMAXINFO:
        if (p) {
            auto *mm = reinterpret_cast<MINMAXINFO *>(lp);
            mm->ptMinTrackSize = {S(*p, 760), S(*p, 560)};
        }
        return TRUE;
    case WM_DPICHANGED:
        if (p) {
            p->dpi = HIWORD(wp);
            setUiFont(*p);
            auto *r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
            layout(*p);
            dropCache(*p);
            updateView(*p);
        }
        return TRUE;
    case WM_TIMER:
        if (p && wp == TIMER_APPLY) applyEdits(*p);
        return TRUE;
    case WM_COMMAND: {
        if (!p) return FALSE;
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDOK: applyEdits(*p); return TRUE;  // Enter：入力を今すぐ反映（印刷はしない）
        case IDCANCEL:
            saveSettings(*p);
            EndDialog(hwnd, 0);
            return TRUE;
        case ID_PRINTER:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(p->cPrinter, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < static_cast<int>(p->printers.size())) {
                    p->printer = p->printers[i];
                    loadPapers(*p);
                    if (!paperSupported(*p, p->paper)) p->paper = p->defPaper;
                    fillPaperCombo(*p);
                    rebuild(*p, true);
                }
            }
            return TRUE;
        case ID_PAPER:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(p->cPaper, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < static_cast<int>(p->papers.size())) {
                    p->paper = static_cast<short>(p->papers[i].id);
                    rebuild(*p, true);
                }
            }
            return TRUE;
        case ID_ORIENT:
            if (code == CBN_SELCHANGE) {
                p->orient = SendMessageW(p->cOrient, CB_GETCURSEL, 0, 0) == 1 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                rebuild(*p, true);
            }
            return TRUE;
        case ID_FONT:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(p->cFont, CB_GETCURSEL, 0, 0));
                if (i >= 0) {
                    int n = static_cast<int>(SendMessageW(p->cFont, CB_GETLBTEXTLEN, i, 0));
                    std::wstring f(static_cast<size_t>(n) + 1, L'\0');
                    SendMessageW(p->cFont, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(f.data()));
                    f.resize(static_cast<size_t>(n));
                    p->font = wideToUtf8(f);
                    rebuild(*p, false);
                }
            }
            return TRUE;
        case ID_LANG:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(p->cLang, CB_GETCURSEL, 0, 0));
                iniSet(L"General", L"Language", i == 1 ? L"ja" : i == 2 ? L"en" : L"");
                decideLanguage();
                applyTexts(*p);
                updateMenuTexts();
                layout(*p);
            }
            return TRUE;
        case ID_COLOR:
            if (code == CBN_SELCHANGE) {
                int i = static_cast<int>(SendMessageW(p->cColor, CB_GETCURSEL, 0, 0));
                if (i >= 0) {
                    p->colorMode = static_cast<int>(SendMessageW(p->cColor, CB_GETITEMDATA, i, 0));
                    rebuild(*p, false);
                }
            }
            return TRUE;
        case ID_SIZE:
        case ID_MTOP:
        case ID_MBOTTOM:
        case ID_MLEFT:
        case ID_MRIGHT:
            if (code == EN_CHANGE && !p->loading) SetTimer(hwnd, TIMER_APPLY, 600, nullptr);
            if (code == EN_KILLFOCUS) applyEdits(*p);
            return TRUE;
        case ID_RESET:
            if (code == BN_CLICKED) {
                p->font = p->nppFont;
                p->sizeFrac = p->nppSizeFrac;
                for (int i = 0; i < 4; ++i) p->margin[i] = p->cfg.margin[i];
                p->colorMode = p->cfg.printOption;
                fillColorCombo(*p);
                fillEdits(*p);
                rebuild(*p, false);
            }
            return TRUE;
        case ID_SEL:
            if (code == BN_CLICKED) {
                bool on = SendMessageW(p->chkSel, BM_GETCHECK, 0, 0) == BST_CHECKED;
                p->rangeStart = on ? p->selStart : 0;
                p->rangeEnd = on ? p->selEnd : p->docLen;
                p->cur = 0;
                rebuild(*p, false);
            }
            return TRUE;
        case ID_LINENUM:
            if (code == BN_CLICKED) {
                p->lnWanted = SendMessageW(p->chkLn, BM_GETCHECK, 0, 0) == BST_CHECKED;
                rebuild(*p, false);
            }
            return TRUE;
        case ID_PRINT:
            if (code == BN_CLICKED) doPrint(*p);
            return TRUE;
        case ID_PREV:
            if (code == BN_CLICKED) setPage(*p, p->cur - 1);
            return TRUE;
        case ID_NEXT:
            if (code == BN_CLICKED) setPage(*p, p->cur + 1);
            return TRUE;
        case ID_ZOOM:
            if (code == CBN_SELCHANGE) {
                p->zoom = std::clamp(static_cast<int>(SendMessageW(p->cZoom, CB_GETCURSEL, 0, 0)), 0, 2);
                p->scrollX = p->scrollY = 0;
                updateView(*p);
                SetFocus(p->view);
            }
            return TRUE;
        }
        return FALSE;
    }
    case WM_DESTROY:
        if (p) destroyPreview(*p);
        g_pv = nullptr;
        return FALSE;
    }
    return FALSE;
}

#pragma pack(push, 2)
struct DlgTemplate {
    DLGTEMPLATE t;
    WORD menu, cls, title;
};
#pragma pack(pop)
alignas(4) static DlgTemplate g_tpl = {
    {WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN, 0, 0, 0, 0, 400, 300}, 0, 0, 0};

static void cmdPrintPreview() {
    if (g_pv) return;
    HWND mainSci = currentScintilla();
    if (!mainSci) return;
    registerViewClass();
    Preview p;
    p.mainSci = mainSci;
    DialogBoxIndirectParamW(g_module, &g_tpl.t, g_npp._nppHandle, dlgProc, reinterpret_cast<LPARAM>(&p));
}

// ---- MulCoPre との連動（MulCoPre があるときだけ使える） ----

static std::wstring loadExePath() { return iniGet(L"MulCoPre", L"ExePath"); }

/// MulCoPre のインストーラーが「アプリの一覧」の登録に書いた場所（InstallLocation ＋ MainBinaryName）。なければ空
static std::wstring installedExe(HKEY root) {
    const wchar_t *key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MultiColumnPreview";
    auto read = [&](const wchar_t *name) {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD size = sizeof(buf);
        if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return std::wstring();
        std::wstring s = buf;
        // 値は "C:\...\MultiColumnPreview" のように引用符付きで書かれている
        if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
        return s;
    };
    std::wstring dir = read(L"InstallLocation");
    if (dir.empty()) return L"";
    std::wstring exe = read(L"MainBinaryName");
    if (exe.empty()) exe = L"MulCoPre.exe";
    if (dir.back() != L'\\') dir += L'\\';
    return dir + exe;
}

/// MulCoPre.exe の場所：設定 → インストーラーの登録 → よくあるインストール先。見つからなければ空
static std::wstring locateExe() {
    std::wstring saved = loadExePath();
    if (!saved.empty() && fileExists(saved)) return saved;
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        std::wstring exe = installedExe(root);
        if (!exe.empty() && fileExists(exe)) return exe;
    }
    const std::wstring candidates[] = {
        envPath(L"LOCALAPPDATA", L"\\MultiColumnPreview\\MulCoPre.exe"),
        envPath(L"ProgramFiles", L"\\MultiColumnPreview\\MulCoPre.exe"),
    };
    for (const auto &c : candidates)
        if (!c.empty() && fileExists(c)) return c;
    return L"";
}

/// メニューの文字を今の表示言語にする（灰色などの状態はそのまま）
static void updateMenuTexts() {
    HMENU menu = GetMenu(g_npp._nppHandle);
    if (!menu) return;
    for (int i = 0; i < static_cast<int>(sizeof(g_funcs) / sizeof(g_funcs[0])); ++i) {
        if (!g_funcs[i]._pFunc || g_funcs[i]._cmdID == 0) continue;
        wcsncpy_s(g_funcs[i]._itemName, funcName(i), _TRUNCATE);
        MENUITEMINFOW mii{sizeof(mii)};
        mii.fMask = MIIM_STRING;
        mii.dwTypeData = g_funcs[i]._itemName;
        SetMenuItemInfoW(menu, static_cast<UINT>(g_funcs[i]._cmdID), FALSE, &mii);
    }
    DrawMenuBar(g_npp._nppHandle);
}

/// 「MulCoPre で段組みプレビュー」を、MulCoPre が見つからなければ灰色にする
static void updateMulCoPreMenu() {
    HMENU menu = GetMenu(g_npp._nppHandle);
    int id = g_funcs[FUNC_MULCOPRE]._cmdID;
    if (!menu || id == 0) return;
    EnableMenuItem(menu, static_cast<UINT>(id), MF_BYCOMMAND | (locateExe().empty() ? MF_GRAYED : MF_ENABLED));
    DrawMenuBar(g_npp._nppHandle);
}

/// 文書（選択範囲があれば選択範囲。Notepad++ の印刷と同じ）を MulCoPre に渡す
static void cmdMulCoPre() {
    std::wstring exe = locateExe();
    if (exe.empty()) return;  // メニューは灰色のはず。案内は出さない
    HWND h = currentScintilla();
    if (!h) return;

    bool selection = !sci(h, SCI_GETSELECTIONEMPTY);
    std::string bytes;
    if (selection) {
        LRESULT len = sci(h, SCI_GETSELTEXT, 0, 0);
        std::vector<char> buf(static_cast<size_t>(len) + 2, '\0');
        sci(h, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(buf.data()));
        bytes.assign(buf.data());
    } else {
        LRESULT len = sci(h, SCI_GETLENGTH, 0, 0);
        if (len <= 0) {
            showMessage(nullptr, TR(L"文書が空です。", L"The document is empty."));
            return;
        }
        std::vector<char> buf(static_cast<size_t>(len) + 1, '\0');
        sci(h, SCI_GETTEXT, static_cast<WPARAM>(len + 1), reinterpret_cast<LPARAM>(buf.data()));
        bytes.assign(buf.data(), static_cast<size_t>(len));
    }
    std::string utf8 = toUtf8(bytes, static_cast<int>(sci(h, SCI_GETCODEPAGE, 0, 0)));

    wchar_t docName[MAX_PATH] = {};
    SendMessageW(g_npp._nppHandle, NPPM_GETFILENAME, MAX_PATH, reinterpret_cast<LPARAM>(docName));
    std::wstring name = docName[0] ? docName : L"Notepad++";
    // 選択範囲のときは --selection を付けるだけ。「（選択範囲）」はアプリが自分の表示言語で付ける（2026-11-16 ユーザー依頼）

    // 元のファイルのフォルダ（保存してあるファイルのときだけ）。MulCoPre は .md の画像の相対パスをここから読む。
    // 末尾の「\」は付けない（「"C:\"」の「\"」が引用符として読まれてしまうため）。古い MulCoPre は知らない引数を読み飛ばす
    std::wstring docDir;
    {
        wchar_t full[MAX_PATH * 4] = {};
        SendMessageW(g_npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH * 4, reinterpret_cast<LPARAM>(full));
        std::wstring fp = full;
        size_t slash = fp.find_last_of(L"\\/");
        DWORD attr = GetFileAttributesW(fp.c_str());
        if (slash != std::wstring::npos && attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            docDir = fp.substr(0, slash);
            while (!docDir.empty() && (docDir.back() == L'\\' || docDir.back() == L'/')) docDir.pop_back();
        }
    }

    // 一時ファイル：%TEMP%\MulCoPre\npp-<時刻>.txt（UTF-8・BOM 付き）。MulCoPre が読んだら消す
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring dir = std::wstring(tempDir) + L"MulCoPre";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tempFile = dir + L"\\npp-" + std::to_wstring(GetTickCount64()) + L".txt";
    HANDLE f = CreateFileW(tempFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        showMessage(nullptr, std::wstring(TR(L"一時ファイルを作れませんでした。\n", L"Could not create a temporary file.\n")) + tempFile, MB_ICONERROR);
        return;
    }
    static const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    bool ok = WriteFile(f, bom, sizeof(bom), &written, nullptr) &&
              WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) && written == utf8.size();
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tempFile.c_str());
        showMessage(nullptr, std::wstring(TR(L"一時ファイルに書き込めませんでした。\n", L"Could not write the temporary file.\n")) + tempFile, MB_ICONERROR);
        return;
    }

    std::wstring cmd = L"\"" + exe + L"\" --temp " + (selection ? L"--selection " : L"") + L"--name \"" + name + L"\" " +
                       (docDir.empty() ? L"" : L"--dir \"" + docDir + L"\" ") + L"\"" + tempFile + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        DeleteFileW(tempFile.c_str());
        showMessage(nullptr, std::wstring(TR(L"MulCoPre を起動できませんでした（エラー ", L"Could not start MulCoPre (error ")) +
                                 std::to_wstring(err) + TR(L"）。\n", L").\n") + exe,
                    MB_ICONERROR);
        return;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

static void cmdSetLocation() {
    wchar_t file[MAX_PATH] = L"MulCoPre.exe";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_npp._nppHandle;
    ofn.lpstrFilter = L"MulCoPre (MulCoPre.exe)\0*.exe\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = TR(L"MulCoPre.exe の場所", L"Location of MulCoPre.exe");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    iniSet(L"MulCoPre", L"ExePath", file);
    updateMulCoPreMenu();
}

// バージョン情報：版、作成者（配布先の BOOTH へのリンク）、最新版の確認（2026-11-16 ユーザー決定）
// 最新版の確認：「確認する」を押したときだけ GitHub の Releases（最新の版）に問い合わせ、今の版と比べて結果を出す。
// 自動アップデートはしない（更新は Notepad++ の「プラグイン管理」で行う。2026-11-16 ユーザー決定）。
// 問い合わせは別のスレッドで行い、結果はタスクダイアログのタイマー（TDN_TIMER）で拾って本文を書き換える。
static const wchar_t AUTHOR_URL[] = L"https://denkikaiden.booth.pm/";
static const wchar_t GITHUB_REPO[] = L"Denkikaiden/NppPrintPreview";
static const wchar_t CHECK_HREF[] = L"check:";

struct UpdateCheck {
    std::atomic<int> state{0}; // 0 まだ／1 問い合わせ中／2 終わり
    int status = 0;            // HTTP の状態コード（0＝つながらなかった）
    std::wstring tag;          // 最新の版（"v" は除く）
};

static std::wstring repoName() {
    // 動作確認用：ini の [General] UpdateRepo で問い合わせ先を変えられる（説明書には書かない）
    std::wstring r = iniGet(L"General", L"UpdateRepo");
    return r.empty() ? GITHUB_REPO : r;
}

static std::wstring releasesUrl() { return L"https://github.com/" + repoName() + L"/releases"; }

/** GitHub の API で最新のリリースの tag_name を取る（WinHTTP、5 秒で諦める） */
static void fetchLatest(std::shared_ptr<UpdateCheck> uc, std::wstring repo) {
    HINTERNET ses = WinHttpOpen(L"NppPrintPreview", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
    std::wstring path = L"/repos/" + repo + L"/releases/latest";
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::string body;
    if (req) {
        WinHttpSetTimeouts(req, 5000, 5000, 5000, 5000);
        const wchar_t *hdr = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            uc->status = (int)code;
            for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail > 0 && body.size() < (1u << 20);) {
                std::string buf(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, buf.data(), avail, &read) || read == 0) break;
                body.append(buf, 0, read);
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    // "tag_name":"v1.2.0" を探す（JSON の全体は読まない）
    size_t k = body.find("\"tag_name\"");
    size_t q1 = k == std::string::npos ? k : body.find('"', body.find(':', k) + 1);
    size_t q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
    if (q2 != std::string::npos) {
        std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
        uc->tag.assign(tag.begin(), tag.end()); // 版の番号は ASCII
    }
    uc->state = 2;
}

// 問い合わせのスレッド。窓を閉じても結果の入れ物は shared_ptr で残る。
// 問い合わせ中に Notepad++ が終わって DLL が外れないよう、DLL の参照を 1 つ持ち、終わったら FreeLibraryAndExitThread で返す
struct FetchArgs {
    std::shared_ptr<UpdateCheck> uc;
    std::wstring repo;
    HMODULE mod;
};

static DWORD WINAPI fetchThread(LPVOID p) {
    FetchArgs *a = static_cast<FetchArgs *>(p);
    fetchLatest(a->uc, a->repo);
    HMODULE mod = a->mod;
    delete a;
    FreeLibraryAndExitThread(mod, 0);
}

static void startFetch(std::shared_ptr<UpdateCheck> uc) {
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&fetchThread), &mod);
    auto *args = new FetchArgs{uc, repoName(), mod};
    HANDLE th = CreateThread(nullptr, 0, fetchThread, args, 0, nullptr);
    if (th) {
        CloseHandle(th);
    } else {
        delete args;
        if (mod) FreeLibrary(mod);
        uc->state = 2; // つながらなかった扱い
    }
}

/** "1.2.0" を比べる。a が新しければ正 */
static int compareVersions(const std::wstring &a, const std::wstring &b) {
    int x[3] = {}, y[3] = {};
    swscanf_s(a.c_str(), L"%d.%d.%d", &x[0], &x[1], &x[2]);
    swscanf_s(b.c_str(), L"%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

static std::wstring aboutContent(const std::wstring &updateLine) {
    return std::wstring(TR(L"作成者：", L"Author: ")) + L"<a href=\"" + AUTHOR_URL + L"\">" + TR(L"でん⚡きかいでん", L"denkikaiden") +
           L"</a>\n" + updateLine;
}

static std::wstring updateResultLine(const UpdateCheck &uc) {
    std::wstring gh = L"<a href=\"" + releasesUrl() + L"\">GitHub</a>";
    if (uc.status == 200 && !uc.tag.empty()) {
        if (compareVersions(uc.tag, PLUGIN_VERSION) > 0)
            return std::wstring(TR(L"新しい版 ", L"Version ")) + uc.tag + TR(L" があります（", L" is available (") + gh +
                   TR(L"）。\nNotepad++ の「プラグイン」→「プラグイン管理」から更新できます。",
                      L").\nYou can update it from Plugins > Plugins Admin in Notepad++.");
        return std::wstring(TR(L"最新版です（", L"You have the latest version (")) + PLUGIN_VERSION + L")";
    }
    if (uc.status == 404) return std::wstring(TR(L"公開されている版が見つかりませんでした（", L"No published version was found (")) + gh + L")";
    return TR(L"確かめられませんでした。インターネットにつながっているか確かめてください",
              L"Could not check. Please make sure you are connected to the internet");
}

static HRESULT CALLBACK aboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lParam, LONG_PTR ref) {
    auto *uc = reinterpret_cast<std::shared_ptr<UpdateCheck> *>(ref);
    if (msg == TDN_HYPERLINK_CLICKED) {
        const wchar_t *href = reinterpret_cast<const wchar_t *>(lParam);
        if (wcscmp(href, CHECK_HREF) == 0) {
            if ((*uc)->state != 0) return S_OK;
            (*uc)->state = 1;
            std::wstring text = aboutContent(TR(L"最新版の確認：確かめています…", L"Latest version: checking…"));
            SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(text.c_str()));
            startFetch(*uc);
        } else {
            ShellExecuteW(nullptr, L"open", href, nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (msg == TDN_TIMER && (*uc)->state == 2) {
        (*uc)->state = 3; // 書き換えは 1 回だけ
        std::wstring text = aboutContent(updateResultLine(**uc));
        SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(text.c_str()));
    }
    return S_OK;
}

static void cmdAbout() {
    std::wstring main = std::wstring(L"NppPrintPreview ") + PLUGIN_VERSION;
    std::wstring content = aboutContent(std::wstring(TR(L"最新版の確認：", L"Latest version: ")) + L"<a href=\"" + CHECK_HREF + L"\">" +
                                        TR(L"確認する", L"Check now") + L"</a>");
    auto uc = std::make_shared<UpdateCheck>();
    TASKDIALOGCONFIG tc{sizeof(tc)};
    tc.hwndParent = g_npp._nppHandle;
    tc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_CALLBACK_TIMER;
    tc.lpCallbackData = reinterpret_cast<LONG_PTR>(&uc);
    tc.dwCommonButtons = TDCBF_OK_BUTTON;
    tc.pszWindowTitle = PLUGIN_NAME;
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = main.c_str();
    tc.pszContent = content.c_str();
    tc.pfCallback = aboutCallback;
    // Notepad++ は comctl32 6 を読み込んでいるので TaskDialogIndirect が使える。見つからなければ文字だけの窓にする
    using TaskDialogFn = HRESULT(WINAPI *)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
    HMODULE cc = GetModuleHandleW(L"comctl32.dll");
    auto fn = cc ? reinterpret_cast<TaskDialogFn>(GetProcAddress(cc, "TaskDialogIndirect")) : nullptr;
    if (fn && SUCCEEDED(fn(&tc, nullptr, nullptr, nullptr))) return;
    showMessage(nullptr, main + L"\n\n" + TR(L"作成者：でん⚡きかいでん  ", L"Author: denkikaiden  ") + AUTHOR_URL + L"\n" +
                             TR(L"最新版：", L"Latest version: ") + releasesUrl());
}

// ---- Notepad++ から呼ばれる関数 ----

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }

extern "C" __declspec(dllexport) void setInfo(NppData data) { g_npp = data; }

extern "C" __declspec(dllexport) const wchar_t *getName() { return PLUGIN_NAME; }

extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *count) {
    decideLanguage();
    for (int i = 0; i < static_cast<int>(sizeof(g_funcs) / sizeof(g_funcs[0])); ++i)
        if (g_funcs[i]._pFunc) wcsncpy_s(g_funcs[i]._itemName, funcName(i), _TRUNCATE);
    *count = static_cast<int>(sizeof(g_funcs) / sizeof(g_funcs[0]));
    return g_funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *n) {
    if (!n || n->nmhdr.hwndFrom != g_npp._nppHandle) return;
    // 起動時に加え、文書を切り替えるたび（ファイルを開いたときも）灰色の状態を当て直す。
    // Notepad++ がメニューの状態を戻すことがあり、ファイルを開いたあと押せる状態になっていた（2026-11-16 ユーザー指摘）。
    // 当て直すときに MulCoPre を探し直すので、あとから入れた・消した MulCoPre にも追いつく
    if (n->nmhdr.code == NPPN_READY || (g_ready && n->nmhdr.code == NPPN_BUFFERACTIVATED)) updateMulCoPreMenu();
    if (n->nmhdr.code == NPPN_READY) g_ready = true;
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_module = module;
    return TRUE;
}
