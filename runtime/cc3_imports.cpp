// Imports C&C 3 needs on top of what the RA3 runtime already had (docs/GAP_VS_RA3.md):
// MSVCR80 / KERNEL32 / USER32 / ole32 / OLEAUT32 / SHLWAPI functions, D3DX math, and bring-up stubs for
// GDI32 (text drawing, real rendering comes later) and DINPUT8, and WININET (the Steam store check).
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "fpu.h"
#include "input.h"
#include "rt.h"
#include "steam.h"

using namespace rt;

#define CRT "MSVCR80.dll"
#define K32 "KERNEL32.dll"
#define USR "USER32.dll"
#define OLE32 "ole32.dll"
#define OLEAUT "OLEAUT32.dll"
#define SHLW "SHLWAPI.dll"
#define GDI "GDI32.dll"
#define DINPUT "DINPUT8.dll"
#define INET "WININET.dll"
#define D3DX "d3dx9_29.dll"

namespace
{
// ---- shared helpers
uint32_t tmBuf32() { static thread_local uint32_t a = gmalloc(64); return a; }
void putTm(uint32_t a, const struct tm& t)
{
	int v[9] = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
	for (int i = 0; i < 9; i++) guest::store<int32_t>(a + 4 * i, v[i]);
}
struct tm getTm(uint32_t a)
{
	struct tm t{};
	int* v[9] = {&t.tm_sec, &t.tm_min, &t.tm_hour, &t.tm_mday, &t.tm_mon, &t.tm_year, &t.tm_wday, &t.tm_yday, &t.tm_isdst};
	for (int i = 0; i < 9; i++) *v[i] = guest::load<int32_t>(a + 4 * i);
	return t;
}
// "C" locale classification of a byte (MSVC: EOF and 0..255 are valid, the rest is undefined -> false)
bool byteOk(uint32_t ch) { return (int)ch >= 0 && (int)ch < 256; }

// Windows path made absolute against the game's working directory C:\CC3, separators normalised, "." and ".." folded
std::string fullWinPath(const std::string& in)
{
	std::string p = in;
	std::replace(p.begin(), p.end(), '/', '\\');
	if (p.size() >= 2 && p[1] == ':') {}
	else if (!p.empty() && p[0] == '\\') p = "C:" + p;
	else p = "C:\\CC3\\" + p;
	std::vector<std::string> parts;
	size_t b = 3;
	while (b <= p.size())
	{
		size_t e = p.find('\\', b);
		if (e == std::string::npos) e = p.size();
		std::string s = p.substr(b, e - b);
		if (s == "..") { if (!parts.empty()) parts.pop_back(); }
		else if (!s.empty() && s != ".") parts.push_back(s);
		b = e + 1;
	}
	std::string r = p.substr(0, 3);
	for (size_t i = 0; i < parts.size(); i++) r += (i ? "\\" : "") + parts[i];
	if (!in.empty() && (in.back() == '\\' || in.back() == '/') && r.back() != '\\') r += '\\';
	return r;
}

// struct _stat64i32 (48 bytes): mode at +6, nlink +8, size +20, times +24/+32/+40
int statTo(const std::string& win, uint32_t o)
{
	struct stat st{};
	if (stat(hostPath(win).c_str(), &st) != 0) return -1;
	memset(guest::host(o), 0, 48);
	guest::store<uint16_t>(o + 6, (uint16_t)(S_ISDIR(st.st_mode) ? 0x41ff : 0x81b6));
	guest::store<uint16_t>(o + 8, 1);
	guest::store<uint32_t>(o + 20, (uint32_t)st.st_size);
	guest::store<uint64_t>(o + 24, (uint64_t)st.st_atime);
	guest::store<uint64_t>(o + 32, (uint64_t)st.st_mtime);
	guest::store<uint64_t>(o + 40, (uint64_t)st.st_ctime);
	return 0;
}

// SYSTEMTIME (8 x uint16) -> struct tm; a null pointer means the current local time
struct tm sysTime(uint32_t p)
{
	struct tm t{};
	if (!p) { time_t now = time(nullptr); localtime_r(&now, &t); return t; }
	t.tm_year = guest::load<uint16_t>(p) - 1900; t.tm_mon = guest::load<uint16_t>(p + 2) - 1; t.tm_wday = guest::load<uint16_t>(p + 4);
	t.tm_mday = guest::load<uint16_t>(p + 6); t.tm_hour = guest::load<uint16_t>(p + 8); t.tm_min = guest::load<uint16_t>(p + 10); t.tm_sec = guest::load<uint16_t>(p + 12);
	return t;
}
// GetDateFormat / GetTimeFormat result: cch 0 asks for the size (with the NUL)
template <class Str> uint32_t putFormatted(uint32_t buf, uint32_t cch, const std::string& s8)
{
	if (!cch) return (uint32_t)s8.size() + 1;
	if (s8.size() + 1 > cch) { setLastError(122); return 0; }   // ERROR_INSUFFICIENT_BUFFER
	if constexpr (sizeof(typename Str::value_type) == 2) putWStr(buf, utf8to16(s8)); else putStr(buf, s8);
	return (uint32_t)s8.size() + 1;
}

uint32_t fakeHandle() { static std::atomic<uint32_t> next{0x5a000010}; return next.fetch_add(4); }
}

// ======================= MSVCR80
IMP_C(CRT, _time32) { int32_t t = (int32_t)time(nullptr); if (ARG(0)) guest::store<int32_t>(ARG(0), t); SET(t); }
IMP_C(CRT, _localtime32) { time_t t = guest::load<int32_t>(ARG(0)); struct tm tm; localtime_r(&t, &tm); putTm(tmBuf32(), tm); SET(tmBuf32()); }
IMP_C(CRT, _gmtime32) { time_t t = guest::load<int32_t>(ARG(0)); struct tm tm; gmtime_r(&t, &tm); putTm(tmBuf32(), tm); SET(tmBuf32()); }
IMP_C(CRT, _mktime32) { struct tm tm = getTm(ARG(0)); time_t t = mktime(&tm); putTm(ARG(0), tm); SET((uint32_t)(int32_t)t); }
IMP_C(CRT, _difftime32) { RET_F((double)(int32_t)ARG(0) - (double)(int32_t)ARG(1)); }

IMP_C(CRT, isprint) { SET(byteOk(ARG(0)) && isprint((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isalpha) { SET(byteOk(ARG(0)) && isalpha((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, iscntrl) { SET(byteOk(ARG(0)) && iscntrl((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, islower) { SET(byteOk(ARG(0)) && islower((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isupper) { SET(byteOk(ARG(0)) && isupper((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, ispunct) { SET(byteOk(ARG(0)) && ispunct((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isxdigit) { SET(byteOk(ARG(0)) && isxdigit((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isalnum) { SET(byteOk(ARG(0)) && isalnum((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isdigit) { SET(byteOk(ARG(0)) && isdigit((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, isspace) { SET(byteOk(ARG(0)) && isspace((int)ARG(0)) ? 1 : 0); }
IMP_C(CRT, iswascii) { SET((ARG(0) & 0xffff) < 0x80 ? 1 : 0); }
IMP_C(CRT, iswalpha) { SET(iswalpha((wint_t)(ARG(0) & 0xffff)) ? 1 : 0); }
IMP_C(CRT, iswgraph) { SET(iswgraph((wint_t)(ARG(0) & 0xffff)) ? 1 : 0); }
IMP_C(CRT, iswdigit) { SET(iswdigit((wint_t)(ARG(0) & 0xffff)) ? 1 : 0); }
IMP_C(CRT, iswalnum) { SET(iswalnum((wint_t)(ARG(0) & 0xffff)) ? 1 : 0); }
IMP_C(CRT, iswspace) { SET(iswspace((wint_t)(ARG(0) & 0xffff)) ? 1 : 0); }
IMP_C(CRT, towupper) { SET(towupper((wint_t)(ARG(0) & 0xffff)) & 0xffff); }

IMP_C(CRT, _strupr) { for (char* p = (char*)guest::host(ARG(0)); *p; p++) *p = (char)toupper((unsigned char)*p); SET(ARG(0)); }
IMP_C(CRT, _wcsicmp)
{
	for (uint32_t a = ARG(0), b = ARG(1);; a += 2, b += 2)
	{
		wint_t x = towlower(guest::load<char16_t>(a)), y = towlower(guest::load<char16_t>(b));
		if (x != y || !x) { SET((uint32_t)((int)x - (int)y)); return; }
	}
}
IMP_C(CRT, _memicmp)
{
	const unsigned char* a = (const unsigned char*)guest::host(ARG(0));
	const unsigned char* b = (const unsigned char*)guest::host(ARG(1));
	for (uint32_t i = 0; i < ARG(2); i++)
	{
		int d = tolower(a[i]) - tolower(b[i]);
		if (d) { SET((uint32_t)d); return; }
	}
	SET(0);
}
IMP_C(CRT, _wcsdup) { std::u16string s = gwstr(ARG(0)); uint32_t p = gmalloc(2 * (uint32_t)s.size() + 2); putWStr(p, s); SET(p); }
// multi-byte functions: the game runs with a single-byte code page
IMP_C(CRT, _mbscmp) { SET((uint32_t)strcmp((char*)guest::host(ARG(0)), (char*)guest::host(ARG(1)))); }
IMP_C(CRT, _mbsicmp) { SET((uint32_t)strcasecmp((char*)guest::host(ARG(0)), (char*)guest::host(ARG(1)))); }
IMP_C(CRT, _mbslen) { SET((uint32_t)strlen((char*)guest::host(ARG(0)))); }
IMP_C(CRT, _ismbblead) { SET(0); }

// x87 intrinsics: the argument comes in st0 (two-argument ones: first argument in st1), the result goes to st0
IMP_C(CRT, _CIsqrt) { double x = fpop(c); RET_F(sqrt(x)); }
IMP_C(CRT, _CIsin) { double x = fpop(c); RET_F(sin(x)); }
IMP_C(CRT, _CIcos) { double x = fpop(c); RET_F(cos(x)); }
IMP_C(CRT, _CItan) { double x = fpop(c); RET_F(tan(x)); }
IMP_C(CRT, _CIatan) { double x = fpop(c); RET_F(atan(x)); }
IMP_C(CRT, _CIatan2) { double x = fpop(c), y = fpop(c); RET_F(atan2(y, x)); }
IMP_C(CRT, _CIexp) { double x = fpop(c); RET_F(exp(x)); }
IMP_C(CRT, _CIlog) { double x = fpop(c); RET_F(log(x)); }
IMP_C(CRT, _CIlog10) { double x = fpop(c); RET_F(log10(x)); }

IMP_C(CRT, _stat64i32) { SET((uint32_t)statTo(gstr(ARG(0)), ARG(1))); }
IMP_C(CRT, _wstat64i32) { SET((uint32_t)statTo(utf16to8(gwstr(ARG(0))), ARG(1))); }

// int _open(const char*, int oflag, int pmode) (the C++ overload MSVCR80 exports) / _close: host descriptors
IMP_ID(CRT, "?_open@@YAHPBDHH@Z", cc3_open, 0)
{
	uint32_t of = ARG(1);
	int fl = (of & 3) == 1 ? O_WRONLY : (of & 3) == 2 ? O_RDWR : O_RDONLY;
	if (of & 0x8) fl |= O_APPEND;
	if (of & 0x100) fl |= O_CREAT;
	if (of & 0x200) fl |= O_TRUNC;
	if (of & 0x400) fl |= O_EXCL;
	int fd = open(hostPath(gstr(ARG(0))).c_str(), fl, 0666);
	SET((uint32_t)fd);
}
IMP_C(CRT, _close) { SET(close((int)ARG(0)) == 0 ? 0 : (uint32_t)-1); }

IMP_C(CRT, _fullpath)
{
	std::string r = fullWinPath(gstr(ARG(1)));
	uint32_t dst = ARG(0), n = ARG(2);
	if (!dst) { dst = gmalloc((uint32_t)r.size() + 1); n = (uint32_t)r.size() + 1; }
	if (r.size() + 1 > n) { SET(0); return; }
	putStr(dst, r);
	SET(dst);
}
IMP_C(CRT, _wsplitpath)
{
	std::u16string p = gwstr(ARG(0));
	std::u16string drive, dir, name, ext;
	size_t i = 0;
	if (p.size() >= 2 && p[1] == u':') { drive = p.substr(0, 2); i = 2; }
	size_t slash = p.find_last_of(u"\\/");
	if (slash != std::u16string::npos && slash >= i) { dir = p.substr(i, slash + 1 - i); i = slash + 1; }
	std::u16string rest = p.substr(i);
	size_t dot = rest.rfind(u'.');
	if (dot != std::u16string::npos) { name = rest.substr(0, dot); ext = rest.substr(dot); } else name = rest;
	if (ARG(1)) putWStr(ARG(1), drive);
	if (ARG(2)) putWStr(ARG(2), dir);
	if (ARG(3)) putWStr(ARG(3), name);
	if (ARG(4)) putWStr(ARG(4), ext);
}
// errno_t wcsncpy_s(dst, dstSize, src, count); count _TRUNCATE (-1): copy what fits and return STRUNCATE (80)
IMP_C(CRT, wcsncpy_s)
{
	uint32_t d = ARG(0), size = ARG(1), s = ARG(2), count = ARG(3);
	if (!d || !size) { SET(22); return; }
	if (!s) { guest::store<char16_t>(d, 0); SET(22); return; }
	std::u16string src = gwstr(s);
	bool trunc = count == 0xffffffffu;
	size_t n = std::min<size_t>(src.size(), trunc ? src.size() : count);
	if (n + 1 > size)
	{
		if (!trunc) { guest::store<char16_t>(d, 0); SET(34); return; }   // ERANGE
		n = size - 1;
		putWStr(d, src.substr(0, n));
		SET(80);
		return;
	}
	putWStr(d, src.substr(0, n));
	SET(0);
}
// wchar_t* wcstok(wchar_t*, const wchar_t*): the old two-argument form with per-thread state
IMP_C(CRT, wcstok)
{
	static thread_local uint32_t save = 0;
	uint32_t p = ARG(0) ? ARG(0) : save;
	if (!p) { SET(0); return; }
	std::u16string delim = gwstr(ARG(1));
	auto isDelim = [&](char16_t ch) { return delim.find(ch) != std::u16string::npos; };
	while (guest::load<char16_t>(p) && isDelim(guest::load<char16_t>(p))) p += 2;
	if (!guest::load<char16_t>(p)) { save = 0; SET(0); return; }
	uint32_t start = p;
	while (guest::load<char16_t>(p) && !isDelim(guest::load<char16_t>(p))) p += 2;
	if (guest::load<char16_t>(p)) { guest::store<char16_t>(p, 0); save = p + 2; } else save = 0;
	SET(start);
}
IMP_C(CRT, _wtof) { RET_F(atof(utf16to8(gwstr(ARG(0))).c_str())); }
IMP_C(CRT, swprintf_s)
{
	ArgReader ap{c->r[ESP] + 16};
	std::u16string s = formatW(gwstr(ARG(2)), ap);
	if (s.size() + 1 > ARG(1)) { if (ARG(1)) guest::store<char16_t>(ARG(0), 0); SET((uint32_t)-1); return; }
	putWStr(ARG(0), s);
	SET((uint32_t)s.size());
}
IMP_C(CRT, fputc) { log("[game] %c", (char)ARG(0)); SET(ARG(0) & 0xff); }
IMP_C(CRT, puts) { log("[game] %s", gstr(ARG(0)).c_str()); SET(0); }
IMP_C(CRT, abort) { fatal("abort() called by the game"); }

// ======================= KERNEL32
IMP_ID(K32, "GetModuleHandleExA", GetModuleHandleExA, 12)
{
	if (ARG(2)) guest::store<uint32_t>(ARG(2), ARG(1) && !(ARG(0) & 4) ? 0x7ff00000 : imageBase);
	SET(1);
}
IMP_ID(K32, "GetThreadLocale", GetThreadLocale, 0) { SET(config.language == "russian" ? 0x419 : 0x409); }
IMP_ID(K32, "GetFullPathNameA", GetFullPathNameA, 16)
{
	std::string r = fullWinPath(gstr(ARG(0)));
	uint32_t n = ARG(1), buf = ARG(2);
	if (r.size() + 1 > n) { SET((uint32_t)r.size() + 1); return; }
	putStr(buf, r);
	if (ARG(3))
	{
		size_t s = r.find_last_of('\\');
		guest::store<uint32_t>(ARG(3), s == std::string::npos || s + 1 == r.size() ? 0 : buf + (uint32_t)s + 1);
	}
	SET((uint32_t)r.size());
}
IMP_ID(K32, "QueueUserAPC", QueueUserAPC, 12) { log("QueueUserAPC(%08x, %08x, %08x): not supported", ARG(0), ARG(1), ARG(2)); SET(0); }
IMP_ID(K32, "CreateProcessA", CreateProcessA, 40) { log("CreateProcessA(%s, %s): refused", ARG(0) ? gstr(ARG(0)).c_str() : "-", ARG(1) ? gstr(ARG(1)).c_str() : "-"); setLastError(2); SET(0); }
IMP_ID(K32, "CreateProcessW", CreateProcessW, 40) { log("CreateProcessW(%s, %s): refused", ARG(0) ? utf16to8(gwstr(ARG(0))).c_str() : "-", ARG(1) ? utf16to8(gwstr(ARG(1))).c_str() : "-"); setLastError(2); SET(0); }
IMP_ID(K32, "GetExitCodeProcess", GetExitCodeProcess, 8) { if (ARG(1)) guest::store<uint32_t>(ARG(1), 0); SET(1); }
IMP_ID(K32, "GetEnvironmentStrings", GetEnvironmentStrings, 0) { static uint32_t a = [] { uint32_t p = gmalloc(4); guest::store<uint32_t>(p, 0); return p; }(); SET(a); }
IMP_ID(K32, "CreateDirectoryA", CreateDirectoryA, 8)
{
	if (mkdir(hostPath(gstr(ARG(0))).c_str(), 0777) == 0) { SET(1); return; }
	setLastError(errno == EEXIST ? 183 : 3);
	SET(0);
}
// (locale, flags, SYSTEMTIME*, format, buffer, cch): a fixed short format; the game only shows these in save lists
IMP_ID(K32, "GetDateFormatA", GetDateFormatA, 24) { struct tm t = sysTime(ARG(2)); char b[32]; strftime(b, sizeof b, "%d.%m.%Y", &t); SET(putFormatted<std::string>(ARG(4), ARG(5), b)); }
IMP_ID(K32, "GetDateFormatW", GetDateFormatW, 24) { struct tm t = sysTime(ARG(2)); char b[32]; strftime(b, sizeof b, "%d.%m.%Y", &t); SET(putFormatted<std::u16string>(ARG(4), ARG(5), b)); }
IMP_ID(K32, "GetTimeFormatW", GetTimeFormatW, 24) { struct tm t = sysTime(ARG(2)); char b[32]; strftime(b, sizeof b, "%H:%M:%S", &t); SET(putFormatted<std::u16string>(ARG(4), ARG(5), b)); }

// ======================= USER32
IMP_ID(USR, "EnableMenuItem", EnableMenuItem, 12) { SET(0); }
IMP_ID(USR, "GetDC", GetDC, 4) { SET(0x5a000004); }
IMP_ID(USR, "ReleaseDC", ReleaseDC, 8) { SET(1); }
IMP_ID(USR, "LoadCursorA", LoadCursorA, 8) { SET(0x5a000008); }
IMP_ID(USR, "SetCursorPos", SetCursorPos, 8) { SET(1); }
IMP_ID(USR, "GetCursorPos", GetCursorPos, 4) { int x, y; input::cursorPos(x, y); guest::store<int32_t>(ARG(0), x); guest::store<int32_t>(ARG(0) + 4, y); SET(1); }
IMP_ID(USR, "wsprintfW", wsprintfW, 0) { ArgReader ap{c->r[ESP] + 12}; std::u16string s = formatW(gwstr(ARG(1)), ap); if (s.size() > 1023) s.resize(1023); putWStr(ARG(0), s); SET((uint32_t)s.size()); }

// ======================= ole32 / OLEAUT32 / SHLWAPI
IMP_ID(OLE32, "StringFromGUID2", StringFromGUID2, 12)
{
	if (ARG(2) < 39) { SET(0); return; }
	uint32_t g = ARG(0);
	char b[48];
	snprintf(b, sizeof b, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", guest::load<uint32_t>(g), guest::load<uint16_t>(g + 4), guest::load<uint16_t>(g + 6),
	         guest::load<uint8_t>(g + 8), guest::load<uint8_t>(g + 9), guest::load<uint8_t>(g + 10), guest::load<uint8_t>(g + 11),
	         guest::load<uint8_t>(g + 12), guest::load<uint8_t>(g + 13), guest::load<uint8_t>(g + 14), guest::load<uint8_t>(g + 15));
	putWStr(ARG(1), utf8to16(b));
	SET(39);
}
IMP_ID(OLE32, "CoGetObject", CoGetObject, 16) { if (ARG(3)) guest::store<uint32_t>(ARG(3), 0); SET(0x80004005); }
// BSTR: 4-byte byte length in front of the characters, the pointer points at the characters
IMP_ID(OLEAUT, "#2", SysAllocString, 4)
{
	if (!ARG(0)) { SET(0); return; }
	std::u16string s = gwstr(ARG(0));
	uint32_t p = gmalloc(4 + 2 * (uint32_t)s.size() + 2);
	guest::store<uint32_t>(p, 2 * (uint32_t)s.size());
	putWStr(p + 4, s);
	SET(p + 4);
}
IMP_ID(OLEAUT, "#6", SysFreeString, 4) { if (ARG(0)) gfree(ARG(0) - 4); }
IMP_ID(SHLW, "PathFileExistsA", PathFileExistsA, 4) { SET(access(hostPath(gstr(ARG(0))).c_str(), F_OK) == 0 ? 1 : 0); }

// ======================= GDI32 (bring-up: text measures plausibly, nothing is drawn yet)
// CC3 renders its UI text with GDI into a DIB section and uploads that as a texture. Real glyphs come with the
// font step (FreeType / Android Canvas); until then the bitmaps stay empty.
namespace { constexpr int kFontH = 16, kAscent = 13, kCharW = 8; }
IMP_ID(GDI, "CreateFontA", CreateFontA, 56) { SET(fakeHandle()); }
IMP_ID(GDI, "CreateFontIndirectA", CreateFontIndirectA, 4) { SET(fakeHandle()); }
IMP_ID(GDI, "CreateCompatibleDC", CreateCompatibleDC, 4) { SET(fakeHandle()); }
IMP_ID(GDI, "CreateDIBitmap", CreateDIBitmap, 24) { SET(fakeHandle()); }
// (hdc, BITMAPINFO*, usage, void** bits, section, offset): the pixel memory must exist, the game writes and reads it
IMP_ID(GDI, "CreateDIBSection", CreateDIBSection, 24)
{
	uint32_t bmi = ARG(1);
	int32_t w = guest::load<int32_t>(bmi + 4), h = guest::load<int32_t>(bmi + 8);
	uint32_t bpp = guest::load<uint16_t>(bmi + 14);
	uint32_t stride = ((uint32_t)std::abs(w) * std::max(1u, bpp) + 31) / 32 * 4;
	uint32_t size = stride * (uint32_t)std::abs(h);
	uint32_t bits = size ? gmalloc(size) : 0;
	if (bits) memset(guest::host(bits), 0, size);
	if (ARG(3)) guest::store<uint32_t>(ARG(3), bits);
	static int n = 0;
	if (n++ < 20) log("GDI: CreateDIBSection %dx%d %u bpp -> %u bytes at %08x", w, h, bpp, size, bits);
	SET(bits ? fakeHandle() : 0);
}
IMP_ID(GDI, "SelectObject", SelectObject, 8) { SET(0x50001); }
IMP_ID(GDI, "DeleteObject", DeleteObject, 4) { SET(1); }
IMP_ID(GDI, "DeleteDC", DeleteDC, 4) { SET(1); }
IMP_ID(GDI, "SetBkColor", SetBkColor, 8) { SET(0xffffff); }
IMP_ID(GDI, "SetTextColor", SetTextColor, 8) { SET(0); }
IMP_ID(GDI, "SetBkMode", SetBkMode, 8) { SET(2); }
IMP_ID(GDI, "SetMapMode", SetMapMode, 8) { SET(1); }
IMP_ID(GDI, "SetDeviceGammaRamp", SetDeviceGammaRamp, 8) { SET(1); }
IMP_ID(GDI, "BitBlt", BitBlt, 36) { SET(1); }
IMP_ID(GDI, "TextOutA", TextOutA, 20) { SET(1); }
IMP_ID(GDI, "ExtTextOutW", ExtTextOutW, 32) { SET(1); }
// TEXTMETRICA: 11 LONGs, then 4 chars and 4 bytes
IMP_ID(GDI, "GetTextMetricsA", GetTextMetricsA, 8)
{
	uint32_t m = ARG(1);
	memset(guest::host(m), 0, 56);
	int32_t v[11] = {kFontH, kAscent, kFontH - kAscent, 0, 0, kCharW, 2 * kCharW, 400, 0, 96, 96};
	for (int i = 0; i < 11; i++) guest::store<int32_t>(m + 4 * i, v[i]);
	guest::store<uint8_t>(m + 44, 32); guest::store<uint8_t>(m + 45, 255); guest::store<uint8_t>(m + 46, '?'); guest::store<uint8_t>(m + 47, ' ');
	guest::store<uint8_t>(m + 52, 0x06);   // TMPF_VECTOR | TMPF_TRUETYPE
	guest::store<uint8_t>(m + 53, 1);      // DEFAULT_CHARSET
	SET(1);
}
IMP_ID(GDI, "GetTextExtentPoint32W", GetTextExtentPoint32W, 16) { guest::store<int32_t>(ARG(3), kCharW * (int32_t)ARG(2)); guest::store<int32_t>(ARG(3) + 4, kFontH); SET(1); }
IMP_ID(GDI, "GetTextExtentPointI", GetTextExtentPointI, 16) { guest::store<int32_t>(ARG(3), kCharW * (int32_t)ARG(2)); guest::store<int32_t>(ARG(3) + 4, kFontH); SET(1); }
// glyph index = the character code until a real font is behind this
IMP_ID(GDI, "GetGlyphIndicesW", GetGlyphIndicesW, 20)
{
	for (uint32_t i = 0; i < ARG(2); i++) guest::store<uint16_t>(ARG(3) + 2 * i, guest::load<uint16_t>(ARG(1) + 2 * i));
	SET(ARG(2));
}
// (hdc, first, count, indices*, ABC*): ABC = {int a; uint b; int c}
IMP_ID(GDI, "GetCharABCWidthsI", GetCharABCWidthsI, 20)
{
	for (uint32_t i = 0; i < ARG(2); i++) { uint32_t o = ARG(4) + 12 * i; guest::store<int32_t>(o, 0); guest::store<uint32_t>(o + 4, kCharW); guest::store<int32_t>(o + 8, 0); }
	SET(1);
}

// ======================= DINPUT8 (bring-up: not available yet; keyboard and mouse reach the game as window messages)
IMP_ID(DINPUT, "DirectInput8Create", DirectInput8Create, 20)
{
	log("DirectInput8Create(version %x): not implemented yet - the game gets window messages only", ARG(1));
	if (ARG(3)) guest::store<uint32_t>(ARG(3), 0);
	SET(0x80004005);   // E_FAIL
}

// ======================= WININET
// No internet for the game (its online services are gone). The Steam build asks the Steam store for its own entry
// at start (store.steampowered.com/api/appdetails?appids=24790) and stops without it, so that one request is answered
// here as the store would; every other request fails to connect.
namespace
{
constexpr uint32_t kNoInternet = 12007;   // ERROR_INTERNET_NAME_NOT_RESOLVED
struct WebReq { std::string host, path, body; size_t at = 0; bool sent = false; };
std::mutex webMu;
std::map<uint32_t, WebReq> web;   // handle -> internet session, connection or request
uint32_t webNext = 0x1ee70000;
std::string storeAnswer(const std::string& host, const std::string& path)
{
	std::string h = host;
	for (auto& ch : h) ch = (char)tolower((unsigned char)ch);
	if (h != "store.steampowered.com" || path.find("/api/appdetails") == std::string::npos) return std::string();
	size_t q = path.find("appids=");
	std::string id = q == std::string::npos ? std::to_string(steam::kAppId) : path.substr(q + 7, path.find_first_not_of("0123456789", q + 7) - q - 7);
	return "{\"" + id + "\":{\"success\":true,\"data\":{\"type\":\"game\",\"name\":\"" + steam::kGameName + "\",\"steam_appid\":" + id + "}}}";
}
// one URL_COMPONENTSA field (pointer at o, length at o + 4): copy into the caller's buffer, or point into the URL
void urlPart(uint32_t uc, uint32_t o, uint32_t url, size_t at, const std::string& v)
{
	uint32_t ptr = guest::load<uint32_t>(uc + o), len = guest::load<uint32_t>(uc + o + 4);
	if (!len) return;
	if (ptr)
	{
		uint32_t n = std::min<uint32_t>((uint32_t)v.size(), len - 1);
		memcpy(guest::host(ptr), v.data(), n);
		guest::store<uint8_t>(ptr + n, 0);
		guest::store<uint32_t>(uc + o + 4, n);
	}
	else
	{
		guest::store<uint32_t>(uc + o, v.empty() ? 0 : url + (uint32_t)at);
		guest::store<uint32_t>(uc + o + 4, (uint32_t)v.size());
	}
}
}
IMP_ID(INET, "InternetOpenA", InternetOpenA, 20) { std::lock_guard<std::mutex> l(webMu); uint32_t h = webNext++; web[h] = WebReq(); SET(h); }
// (url, length, flags, URL_COMPONENTSA*): scheme://host[:port]/path?extra
IMP_ID(INET, "InternetCrackUrlA", InternetCrackUrlA, 16)
{
	uint32_t url = ARG(0), uc = ARG(3);
	std::string u = ARG(1) ? std::string((const char*)guest::host(url), ARG(1)) : gstr(url);
	size_t se = u.find("://");
	if (se == std::string::npos || !uc) { setLastError(12006); SET(0); return; }   // ERROR_INTERNET_UNRECOGNIZED_SCHEME
	std::string scheme = u.substr(0, se);
	size_t hs = se + 3, pe = u.find_first_of("/?#", hs);
	if (pe == std::string::npos) pe = u.size();
	std::string hostPort = u.substr(hs, pe - hs), host = hostPort;
	uint32_t port = scheme == "https" ? 443 : 80;
	if (size_t col = hostPort.find(':'); col != std::string::npos) { host = hostPort.substr(0, col); port = (uint32_t)atoi(hostPort.c_str() + col + 1); }
	size_t qs = u.find_first_of("?#", pe);
	std::string path = u.substr(pe, (qs == std::string::npos ? u.size() : qs) - pe), extra = qs == std::string::npos ? "" : u.substr(qs);
	urlPart(uc, 4, url, 0, scheme);
	guest::store<uint32_t>(uc + 12, scheme == "https" ? 4 : scheme == "http" ? 3 : 0);   // INTERNET_SCHEME_HTTP(S)
	urlPart(uc, 16, url, hs, host);
	guest::store<uint16_t>(uc + 24, (uint16_t)port);
	urlPart(uc, 28, url, 0, "");
	urlPart(uc, 36, url, 0, "");
	urlPart(uc, 44, url, pe, path);
	urlPart(uc, 52, url, qs == std::string::npos ? 0 : qs, extra);
	SET(1);
}
// (internet, server, port, user, password, service, flags, context)
IMP_ID(INET, "InternetConnectA", InternetConnectA, 32)
{
	std::string host = ARG(1) ? gstr(ARG(1)) : "";
	std::lock_guard<std::mutex> l(webMu);
	if (!web.count(ARG(0))) { setLastError(kNoInternet); SET(0); return; }
	uint32_t h = webNext++;
	web[h].host = host;
	SET(h);
}
// (connect, verb, object, version, referrer, accept types, flags, context)
IMP_ID(INET, "HttpOpenRequestA", HttpOpenRequestA, 32)
{
	std::string path = ARG(2) ? gstr(ARG(2)) : "/";
	std::lock_guard<std::mutex> l(webMu);
	auto it = web.find(ARG(0));
	if (it == web.end()) { setLastError(kNoInternet); SET(0); return; }
	uint32_t h = webNext++;
	web[h].host = it->second.host;
	web[h].path = path;
	SET(h);
}
IMP_ID(INET, "HttpAddRequestHeadersA", HttpAddRequestHeadersA, 16) { SET(1); }
// (request, headers, headers length, optional, optional length)
IMP_ID(INET, "HttpSendRequestA", HttpSendRequestA, 20)
{
	std::lock_guard<std::mutex> l(webMu);
	auto it = web.find(ARG(0));
	if (it == web.end()) { setLastError(kNoInternet); SET(0); return; }
	WebReq& r = it->second;
	r.body = storeAnswer(r.host, r.path);
	r.at = 0;
	r.sent = !r.body.empty();
	log("wininet: GET %s%s -> %s", r.host.c_str(), r.path.c_str(), r.sent ? "answered (Steam store)" : "no internet");
	if (!r.sent) setLastError(kNoInternet);
	SET(r.sent ? 1 : 0);
}
// (request, bytes available*, flags, context)
IMP_ID(INET, "InternetQueryDataAvailable", InternetQueryDataAvailable, 16)
{
	std::lock_guard<std::mutex> l(webMu);
	auto it = web.find(ARG(0));
	if (it == web.end() || !it->second.sent) { if (ARG(1)) guest::store<uint32_t>(ARG(1), 0); setLastError(kNoInternet); SET(0); return; }
	if (ARG(1)) guest::store<uint32_t>(ARG(1), (uint32_t)(it->second.body.size() - it->second.at));
	SET(1);
}
// (handle, buffer, bytes to read, bytes read*)
IMP_ID(INET, "InternetReadFile", InternetReadFile, 16)
{
	std::lock_guard<std::mutex> l(webMu);
	auto it = web.find(ARG(0));
	if (ARG(3)) guest::store<uint32_t>(ARG(3), 0);
	if (it == web.end() || !it->second.sent) { setLastError(kNoInternet); SET(0); return; }
	WebReq& r = it->second;
	uint32_t n = (uint32_t)std::min<size_t>(ARG(2), r.body.size() - r.at);
	if (n) memcpy(guest::host(ARG(1)), r.body.data() + r.at, n);
	r.at += n;
	if (ARG(3)) guest::store<uint32_t>(ARG(3), n);
	SET(1);
}
IMP_ID(INET, "InternetCloseHandle", InternetCloseHandle, 4) { std::lock_guard<std::mutex> l(webMu); web.erase(ARG(0)); SET(1); }

// ======================= d3dx9_29 math (the texture / effect functions come from the d3dx9_35 set, imports.cpp alias)
namespace
{
using M4 = float[16];
void loadM(uint32_t a, float* m) { for (int i = 0; i < 16; i++) m[i] = guest::load<float>(a + 4 * i); }
void storeM(uint32_t a, const float* m) { for (int i = 0; i < 16; i++) guest::store<float>(a + 4 * i, m[i]); }
float argF(CPU* c, int i) { return guest::load<float>(c->r[ESP] + 4 + 4 * i); }
void rotation(uint32_t out, float x, float y, float z, float angle)
{
	float l = std::sqrt(x * x + y * y + z * z);
	if (l > 0) { x /= l; y /= l; z /= l; }
	float s = std::sin(angle), co = std::cos(angle), t = 1 - co;
	// D3DX row-vector convention: v' = v * M
	float m[16] = {t * x * x + co,     t * x * y + s * z, t * x * z - s * y, 0,
	               t * x * y - s * z, t * y * y + co,     t * y * z + s * x, 0,
	               t * x * z + s * y, t * y * z - s * x, t * z * z + co,     0,
	               0, 0, 0, 1};
	storeM(out, m);
}
}
IMP_ID(D3DX, "D3DXMatrixRotationX", D3DXMatrixRotationX, 8) { rotation(ARG(0), 1, 0, 0, argF(c, 1)); SET(ARG(0)); }
IMP_ID(D3DX, "D3DXMatrixRotationY", D3DXMatrixRotationY, 8) { rotation(ARG(0), 0, 1, 0, argF(c, 1)); SET(ARG(0)); }
IMP_ID(D3DX, "D3DXMatrixRotationZ", D3DXMatrixRotationZ, 8) { rotation(ARG(0), 0, 0, 1, argF(c, 1)); SET(ARG(0)); }
IMP_ID(D3DX, "D3DXMatrixRotationAxis", D3DXMatrixRotationAxis, 12)
{
	uint32_t v = ARG(1);
	rotation(ARG(0), guest::load<float>(v), guest::load<float>(v + 4), guest::load<float>(v + 8), argF(c, 2));
	SET(ARG(0));
}
IMP_ID(D3DX, "D3DXMatrixTranspose", D3DXMatrixTranspose, 8)
{
	float m[16], r[16];
	loadM(ARG(1), m);
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r[4 * i + j] = m[4 * j + i];
	storeM(ARG(0), r);
	SET(ARG(0));
}
// (out, float* determinant, m): null when the matrix cannot be inverted
IMP_ID(D3DX, "D3DXMatrixInverse", D3DXMatrixInverse, 12)
{
	float m[16], inv[16];
	loadM(ARG(2), m);
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (ARG(1)) guest::store<float>(ARG(1), det);
	if (det == 0.f) { SET(0); return; }
	for (float& x : inv) x /= det;
	storeM(ARG(0), inv);
	SET(ARG(0));
}
// (out, D3DXVECTOR4*, D3DXMATRIX*): row vector times matrix
IMP_ID(D3DX, "D3DXVec4Transform", D3DXVec4Transform, 12)
{
	float m[16], v[4], r[4];
	loadM(ARG(2), m);
	for (int i = 0; i < 4; i++) v[i] = guest::load<float>(ARG(1) + 4 * i);
	for (int j = 0; j < 4; j++) r[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + v[3] * m[12 + j];
	for (int j = 0; j < 4; j++) guest::store<float>(ARG(0) + 4 * j, r[j]);
	SET(ARG(0));
}
// size in bytes of a vertex with this flexible vertex format
IMP_ID(D3DX, "D3DXGetFVFVertexSize", D3DXGetFVFVertexSize, 4)
{
	uint32_t f = ARG(0), n = 0;
	switch (f & 0x400e)
	{
	case 0x002: n = 12; break;             // XYZ
	case 0x004: n = 16; break;             // XYZRHW
	case 0x006: n = 16; break;             // XYZB1
	case 0x008: n = 20; break;             // XYZB2
	case 0x00a: n = 24; break;             // XYZB3
	case 0x00c: n = 28; break;             // XYZB4
	case 0x00e: n = 32; break;             // XYZB5
	case 0x4002: n = 16; break;            // XYZW
	}
	if (f & 0x010) n += 12;                // NORMAL
	if (f & 0x020) n += 4;                 // PSIZE
	if (f & 0x040) n += 4;                 // DIFFUSE
	if (f & 0x080) n += 4;                 // SPECULAR
	uint32_t texCount = (f >> 8) & 0xf;
	for (uint32_t t = 0; t < texCount; t++)
	{
		uint32_t fmt = (f >> (16 + 2 * t)) & 3;   // 0: 2 floats, 1: 3, 2: 4, 3: 1
		n += fmt == 0 ? 8 : fmt == 1 ? 12 : fmt == 2 ? 16 : 4;
	}
	SET(n);
}
