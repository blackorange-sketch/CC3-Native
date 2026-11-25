#include "game_build.h"
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <functional>
#include <unistd.h>
#include <sys/syscall.h>
#include <atomic>
#include <algorithm>
#include <dirent.h>
#include <map>
#include <mutex>
#include <sys/stat.h>

#include "rt.h"
#include "steam.h"

extern const unsigned lifted_count;

namespace rt
{
Config config;
uint32_t imageBase, imageEntry;
std::vector<std::pair<uint32_t, uint32_t>> imageSections;

static FILE* logFp;
static std::mutex logMu;

void (*logHook)(const char*) = nullptr;
void log(const char* f, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	std::lock_guard<std::mutex> l(logMu);
	if (!logFp && !config.logFile.empty()) logFp = fopen(config.logFile.c_str(), "a");
	static const auto t0 = std::chrono::steady_clock::now();
	char ts[24] = "";
	if (strncmp(buf, "build ", 6) != 0) snprintf(ts, sizeof ts, "[%7.3f] ", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	fprintf(stderr, "%s%s\n", ts, buf);
	if (logFp) { fprintf(logFp, "%s%s\n", ts, buf); fflush(logFp); }
	if (logHook) logHook(buf);
}

#ifdef CC3_TRACE
}
thread_local uint32_t trace_ring[256];
thread_local uint32_t trace_ring_ra[256];
thread_local uint32_t trace_ring_pos;
namespace rt {
static struct { uint32_t* ring; uint32_t* pos; uint32_t tid; uint32_t* ra; int ostid; } rings[32];
static std::atomic<int> ringCount{0};
static void profLoop();
void traceRegister(uint32_t tid)
{
	// fill the slot first, publish the count afterwards (the profiler thread reads rings[] concurrently)
	static std::mutex regMu;
	{
		std::lock_guard<std::mutex> l(regMu);
		int i = ringCount.load();
		if (i < 32) { rings[i] = {trace_ring, &trace_ring_pos, tid, trace_ring_ra, (int)syscall(SYS_gettid)}; ringCount.store(i + 1); }
	}
	static std::once_flag once; std::call_once(once, [] { if (verbose()) std::thread(profLoop).detach(); });
}
// poor man's profiler: every 2 ms record the most recently entered function of each thread
static std::mutex profMu;
static std::map<std::pair<uint32_t, uint32_t>, uint32_t> profHist;
void profDump();
static void profLoop()
{
	for (int iter = 1;; iter++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		if (iter % 2500 == 0) profDump();   // every ~5 s, also while the game is still loading
		std::lock_guard<std::mutex> l(profMu);
		for (int i = 0; i < ringCount && i < 32; i++)
		{
			uint32_t v = rings[i].ring[(*rings[i].pos - 1) & 255];
			profHist[{rings[i].tid, v}]++;
		}
	}
}
void profDump()
{
	std::lock_guard<std::mutex> l(profMu);
	std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> byThread;
	for (auto& kv : profHist) byThread[kv.first.first].push_back({kv.second, kv.first.second});
	for (auto& t : byThread)
	{
		std::sort(t.second.rbegin(), t.second.rend());
		uint32_t total = 0; for (auto& e : t.second) total += e.first;
		std::string line;
		for (size_t k = 0; k < t.second.size() && k < 6; k++)
		{
			uint32_t v = t.second[k].second; char b[96];
			imports::Entry* e = (v >> 28) == 0xb ? imports::lookup(v ^ 0xc0000000u) : nullptr;
			if (e) snprintf(b, sizeof b, " %s:%u", e->name.c_str(), t.second[k].first); else snprintf(b, sizeof b, " %08x:%u", v, t.second[k].first);
			line += b;
		}
		long long cpuMs = -1;
		for (int i = 0; i < ringCount && i < 32; i++) if (rings[i].tid == t.first)
		{
			char path[64]; snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", rings[i].ostid);
			if (FILE* f = fopen(path, "r")) { long long ns = 0; if (fscanf(f, "%lld", &ns) == 1) cpuMs = ns / 1000000; fclose(f); }
		}
		log("PROF thread %u samples %u cpu_ms_total %lld top:%s", t.first, total, cpuMs, line.c_str());
	}
	profHist.clear();
}
void traceDump()
{
	for (int i = 0; i < ringCount && i < 32; i++)
	{
		for (FILE* f : {stderr, logFp})
		{
			if (!f) continue;
			fprintf(f, "-- thread %u%s, last functions (oldest first):", rings[i].tid, rings[i].ring == trace_ring ? " (CURRENT)" : "");
			uint32_t p = *rings[i].pos;
			for (uint32_t k = 0; k < 256; k++)
			{
				uint32_t v = rings[i].ring[(p - 256 + k) & 255];
				imports::Entry* e = (v >> 28) == 0xb ? imports::lookup(v ^ 0xc0000000u) : nullptr;
				uint32_t ra = rings[i].ra[(p - 256 + k) & 255];
				if (e) fprintf(f, " %s!%s", e->dll.c_str(), e->name.c_str());
				else if (ra) fprintf(f, " %08x<%08x", v, ra);
				else fprintf(f, " %08x", v);
			}
			fprintf(f, "\n");
			fflush(f);
		}
	}
}
void traceDumpCurrent(int n)
{
	for (FILE* f : {stderr, logFp})
	{
		if (!f) continue;
		fprintf(f, "   writer ring (last %d, oldest first):", n);
		uint32_t p = trace_ring_pos;
		for (int k = n; k > 0; k--)
		{
			uint32_t v = trace_ring[(p - k) & 255], ra = trace_ring_ra[(p - k) & 255];
			imports::Entry* e = (v >> 28) == 0xb ? imports::lookup(v ^ 0xc0000000u) : nullptr;
			if (e) fprintf(f, " %s!%s", e->dll.c_str(), e->name.c_str());
			else if (ra) fprintf(f, " %08x<%08x", v, ra);
			else fprintf(f, " %08x", v);
		}
		fprintf(f, "\n");
		fflush(f);
	}
}
std::atomic<uint32_t> gLowerCallers[64], gLowerKeys[64];
void callStats()
{
	std::vector<std::pair<uint64_t, std::string>> v;
	for (auto& e : imports::all()) if (e.calls && (e.dll.find("d3d") != std::string::npos || e.name.find("::") != std::string::npos || e.name.find("D3DX") != std::string::npos)) v.push_back({e.calls, e.name});
	std::sort(v.rbegin(), v.rend());
	std::string line;
	for (size_t i = 0; i < v.size() && i < 40; i++) line += " " + v[i].second + "=" + std::to_string(v[i].first);
	log("calls:%s", line.c_str());
	std::vector<std::pair<uint64_t, std::string>> w;
	for (auto& e : imports::all()) if (e.calls && !(e.dll.find("d3d") != std::string::npos || e.name.find("::") != std::string::npos || e.name.find("D3DX") != std::string::npos)) w.push_back({e.calls, e.dll + "!" + e.name});
	std::sort(w.rbegin(), w.rend());
	std::string l2;
	for (size_t i = 0; i < w.size() && i < 60; i++) l2 += " " + w[i].second + "=" + std::to_string(w[i].first);
	log("calls-other:%s", l2.c_str());
	{ std::string t; for (int i = 0; i < 64; i++) if (gLowerCallers[i] > 100000) { char b[32]; snprintf(b, sizeof b, " %x=", (unsigned)gLowerKeys[i].load()); t += b + std::to_string(gLowerCallers[i].load()); } log("tolower callers:%s", t.c_str()); }
}
void traceProfile(int ms)
{
	std::map<std::pair<uint32_t, uint32_t>, int> hist;
	int n = ringCount < 32 ? (int)ringCount : 32;
	for (int t = 0; t < ms; t++)
	{
		for (int i = 0; i < n; i++)
		{
			uint32_t v = rings[i].ring[(*rings[i].pos - 1) & 255];
			hist[{rings[i].tid, v}]++;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	for (int i = 0; i < n; i++)
	{
		std::vector<std::pair<int, uint32_t>> v;
		for (auto& kv : hist) if (kv.first.first == rings[i].tid) v.push_back({kv.second, kv.first.second});
		std::sort(v.rbegin(), v.rend());
		std::string line;
		for (size_t k = 0; k < v.size() && k < 6; k++)
		{
			char b[96];
			imports::Entry* e = (v[k].second >> 28) == 0xb ? imports::lookup(v[k].second ^ 0xc0000000u) : nullptr;
			if (e) snprintf(b, sizeof b, " %s:%d", e->name.c_str(), v[k].first);
			else snprintf(b, sizeof b, " %08x:%d", v[k].second, v[k].first);
			line += b;
		}
		log("profile t%u (%d samples):%s", rings[i].tid, ms, line.c_str());
	}
}
#else
void traceRegister(uint32_t) {}
void traceDump() {}
void profDump() {}
void traceDumpCurrent(int) {}
void traceProfile(int) {}
void callStats() {}
#endif

void fatal(const char* f, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	log("FATAL: %s", buf);
	traceDump();
	if (logFp) fflush(logFp);
	fflush(stderr); _exit(70);
}

// ---- strings
std::string gstr(uint32_t a)
{
	if (!a) return "";
	return std::string(reinterpret_cast<const char*>(guest::host(a)));
}
std::string gstrn(uint32_t a, uint32_t n)
{
	const char* p = reinterpret_cast<const char*>(guest::host(a));
	return std::string(p, strnlen(p, n));
}
std::u16string gwstr(uint32_t a)
{
	std::u16string s;
	if (!a) return s;
	for (;; a += 2) { char16_t ch = guest::load<char16_t>(a); if (!ch) break; s += ch; }
	return s;
}
std::string utf16to8(const std::u16string& s)
{
	std::string o;
	for (size_t i = 0; i < s.size(); i++)
	{
		uint32_t cp = s[i];
		if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < s.size()) { cp = 0x10000 + ((cp - 0xd800) << 10) + (s[++i] - 0xdc00); }
		if (cp < 0x80) o += (char)cp;
		else if (cp < 0x800) { o += (char)(0xc0 | cp >> 6); o += (char)(0x80 | (cp & 0x3f)); }
		else if (cp < 0x10000) { o += (char)(0xe0 | cp >> 12); o += (char)(0x80 | ((cp >> 6) & 0x3f)); o += (char)(0x80 | (cp & 0x3f)); }
		else { o += (char)(0xf0 | cp >> 18); o += (char)(0x80 | ((cp >> 12) & 0x3f)); o += (char)(0x80 | ((cp >> 6) & 0x3f)); o += (char)(0x80 | (cp & 0x3f)); }
	}
	return o;
}
std::u16string utf8to16(const std::string& s)
{
	std::u16string o;
	for (size_t i = 0; i < s.size();)
	{
		unsigned char ch = s[i];
		uint32_t cp; int n;
		if (ch < 0x80) { cp = ch; n = 1; }
		else if ((ch >> 5) == 6) { cp = ch & 0x1f; n = 2; }
		else if ((ch >> 4) == 14) { cp = ch & 0x0f; n = 3; }
		else if ((ch >> 3) == 30) { cp = ch & 7; n = 4; }
		else { cp = ch; n = 1; }   // stray byte: pass through (Latin-1)
		for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3f);
		i += n;
		if (cp >= 0x10000) { cp -= 0x10000; o += (char16_t)(0xd800 + (cp >> 10)); o += (char16_t)(0xdc00 + (cp & 0x3ff)); }
		else o += (char16_t)cp;
	}
	return o;
}
uint32_t putStr(uint32_t dst, const std::string& s)
{
	memcpy(guest::host(dst), s.c_str(), s.size() + 1);
	return (uint32_t)s.size();
}
uint32_t putWStr(uint32_t dst, const std::u16string& s)
{
	memcpy(guest::host(dst), s.c_str(), (s.size() + 1) * 2);
	return (uint32_t)s.size();
}
uint32_t newStr(const std::string& s)
{
	uint32_t a = gmalloc((uint32_t)s.size() + 1);
	putStr(a, s);
	return a;
}

// ---- guest calls
void callGuest(CPU* c, uint32_t fn, std::initializer_list<uint32_t> args, int popBytes)
{
	uint32_t saved = c->r[ESP];
	uint32_t esp = saved;
	std::vector<uint32_t> v(args);
	for (size_t i = v.size(); i-- > 0;) { esp -= 4; guest::store<uint32_t>(esp, v[i]); }
	esp -= 4;
	guest::store<uint32_t>(esp, 0xfffffff4);   // return address, never executed: lifted code returns by C return
	c->r[ESP] = esp;
	::call_target(c, fn);
	c->r[ESP] = saved;   // cdecl and stdcall both end up with the arguments gone
	(void)popBytes;
}
uint32_t callGuestRet(CPU* c, uint32_t fn, std::initializer_list<uint32_t> args, int popBytes)
{
	callGuest(c, fn, args, popBytes);
	return c->r[EAX];
}

// ---- handles
namespace
{
std::mutex hmu;
std::map<uint32_t, std::pair<void*, int>> handles;
uint32_t nextHandle = 0x100;
}
uint32_t newHandle(void* obj, int kind)
{
	std::lock_guard<std::mutex> l(hmu);
	uint32_t h = nextHandle;
	nextHandle += 4;
	handles[h] = {obj, kind};
	return h;
}
void* getHandle(uint32_t h, int kind)
{
	std::lock_guard<std::mutex> l(hmu);
	auto it = handles.find(h);
	return it != handles.end() && (kind == 0 || it->second.second == kind) ? it->second.first : nullptr;
}
void closeHandle(uint32_t h)
{
	std::lock_guard<std::mutex> l(hmu);
	handles.erase(h);
}

// ---- paths: Windows path -> host path, case-insensitive
namespace
{
std::string lowerStr(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); }); return s; }

std::string resolveCI(const std::string& root, const std::string& rel)
{
	std::string cur = root;
	size_t i = 0;
	while (i < rel.size())
	{
		size_t j = rel.find('/', i);
		if (j == std::string::npos) j = rel.size();
		std::string part = rel.substr(i, j - i);
		i = j + 1;
		if (part.empty() || part == ".") continue;
		if (part == "..") { size_t k = cur.rfind('/'); if (k != std::string::npos && k > root.size() - 1) cur = cur.substr(0, k); continue; }
		std::string direct = cur + "/" + part;
		struct stat st;
		if (stat(direct.c_str(), &st) == 0) { cur = direct; continue; }
		DIR* d = opendir(cur.c_str());
		bool found = false;
		if (d)
		{
			std::string want = lowerStr(part);
			while (auto e = readdir(d))
				if (lowerStr(e->d_name) == want) { cur += "/" + std::string(e->d_name); found = true; break; }
			closedir(d);
		}
		if (!found) cur = direct;   // does not exist (yet): keep the requested spelling
	}
	return cur;
}
}

std::string hostPath(const std::string& win)
{
	std::string p = win;
	std::replace(p.begin(), p.end(), '\\', '/');
	bool absolute = (p.size() >= 2 && p[1] == ':') || (!p.empty() && p[0] == '/');
	if (p.size() >= 2 && p[1] == ':') p = p.substr(2);
	while (!p.empty() && p[0] == '/') p.erase(0, 1);
	while (p.rfind("./", 0) == 0) p.erase(0, 2);
	if (!absolute) p = "cc3/" + p;   // relative paths resolve against the game's working directory C:\CC3
	// everything under CC3/ is the game folder; the rest is our writable area
	std::string lp = lowerStr(p);
	if (lp == "cc3" || lp.rfind("cc3/", 0) == 0) return resolveCI(config.gameRoot, p.size() > 4 ? p.substr(4) : "");
	// the Steam build sees the game folder inside the Steam library (steam.cpp GetAppInstallDir)
	static const std::string lib = steam::kLibraryPath;
	if (lp == lib || lp.rfind(lib + "/", 0) == 0) return resolveCI(config.gameRoot, p.size() > lib.size() + 1 ? p.substr(lib.size() + 1) : "");
	return resolveCI(config.dataRoot, p);
}
}

namespace rt
{
}
// global cache of indirect targets: guest address -> lifted table index (shared by all call sites)
static std::atomic<uint64_t> gTargetCache[1u << 16];
static inline uint32_t targetSlot(uint32_t addr) { return (addr ^ (addr >> 16)) & ((1u << 16) - 1); }
static inline int liftedIndex(uint32_t addr)
{
	uint32_t slot = targetSlot(addr);
	uint64_t ce = gTargetCache[slot].load(std::memory_order_relaxed);
	if ((uint32_t)ce == addr && addr) return (int)(ce >> 32);
	unsigned lo = 0, hi = lifted_count;
	while (lo < hi)
	{
		unsigned mid = (lo + hi) / 2;
		if (lifted_table[mid].addr < addr) lo = mid + 1; else hi = mid;
	}
	if (!addr || lo >= lifted_count || lifted_table[lo].addr != addr) return -1;
	gTargetCache[slot].store((uint64_t)lo << 32 | addr, std::memory_order_relaxed);
	return (int)lo;
}
// ---- per-site inline cache miss: polymorphic sites miss often, so the shared hash cache comes before the binary
// search (the search on every miss cost ~14% of the game thread and made saving the game hang)
namespace rt { bool isLifted(uint32_t addr) { return liftedIndex(addr) >= 0; } }
void call_target_ic(CPU* c, uint32_t addr, std::atomic<uint64_t>* ic)
{
	// import stubs first (a range check): COM methods (every Direct3D call) and Win32 functions are called through
	// pointers too, and each such call used to binary-search the lifted table before reaching the import
	if (imports::lookup(addr)) { call_target(c, addr); return; }
	int i = liftedIndex(addr);
	if (i >= 0)
	{
		// a site keeps its first target: polymorphic sites (one call, many virtual methods) used to rewrite their
		// cache on every miss, and the writes bounced the cache line between the game's threads
		if (!ic->load(std::memory_order_relaxed)) ic->store((uint64_t)i << 32 | addr, std::memory_order_relaxed);
		lifted_table[i].fn(c);
		return;
	}
	call_target(c, addr);   // import stubs and the error path
}
// ---- dispatcher: every guest call that is not a direct call to a lifted function comes through here
void call_target(CPU* c, uint32_t addr)
{
	using namespace rt;
	#ifdef CC3_TRACE
	{ uint32_t i_ = trace_ring_pos++ & 255; trace_ring[i_] = 0xc0000000u ^ addr; trace_ring_ra[i_] = 0; }   // marker: indirect call target
	#endif
	// indirect calls (virtual methods, function pointers) are frequent: cache guest address -> table index.
	// Checked first: only lifted functions are cached, imports go through the stub lookup below.
	uint64_t ce = gTargetCache[targetSlot(addr)].load(std::memory_order_relaxed);
	if ((uint32_t)ce == addr && addr) { lifted_table[ce >> 32].fn(c); return; }
	if (auto* e = imports::lookup(addr))
	{
		if (!e->fn) fatal("unimplemented import %s!%s (called from guest %08x)", e->dll.c_str(), e->name.c_str(), guest::load<uint32_t>(c->r[ESP]));
		static const bool implog = getenv("CC3_IMPLOG") != nullptr;
		if (implog) log("import %s!%s ret=%08x a0=%08x a1=%08x", e->dll.c_str(), e->name.c_str(), guest::load<uint32_t>(c->r[ESP]), guest::load<uint32_t>(c->r[ESP] + 4), guest::load<uint32_t>(c->r[ESP] + 8));
		e->calls++;
		imports::current = e;
		e->fn(c);
		c->r[ESP] += 4 + e->pop;
		return;
	}
	if (int i = liftedIndex(addr); i >= 0) { lifted_table[i].fn(c); return; }
	// the address first: the register dump below is best effort
	log("call to unknown guest address %08x (return address %08x, caller import %s)", addr, guest::load<uint32_t>(c->r[ESP]), imports::current ? imports::current->name.c_str() : "-");
	{
		const char* nm[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
		for (int i = 0; i < 8; i++)
		{
			fprintf(stderr, "%s=%08x", nm[i], c->r[i]);
			uint32_t v = c->r[i];
			// only memory known to be mapped (the image): a register may hold any number, and reading it here must not fault
			bool inImage = false;
			for (auto& [a, n] : imageSections) if (v >= a && (uint64_t)v + 32 <= (uint64_t)a + n) inImage = true;
			if (inImage && i != 4)
			{
				fprintf(stderr, " [");
				for (int k = 0; k < 8; k++) fprintf(stderr, " %08x", guest::load<uint32_t>(v + 4 * k));
				fprintf(stderr, " ]");
			}
			fprintf(stderr, "\n");
		}
	}
	fatal("call to unknown guest address %08x (return address %08x)", addr, guest::load<uint32_t>(c->r[ESP]));
}

void unsupported(CPU* c, uint32_t va, const char* what)
{
	rt::fatal("unsupported instruction at %08x: %s", va, what);
}

uint32_t g_eflags_hi = 0;
uint32_t stub_tolower = 0xffffffffu;

namespace rt
{
std::string modulePath() { return "C:\\CC3\\RetailExe\\" + config.gameVersion + "\\" + gb::kExe; }
std::string commandLine()
{
	std::string sku = !config.skuDef.empty() ? config.skuDef : std::string(gb::kSkuPrefix) + "_" + config.language + "_" + config.gameVersion + ".SkuDef";
	return "\"" + modulePath() + "\" -config " + sku + " -win";
}
}
namespace rt { bool verbose() { static bool v = access("/sdcard/cc3_verbose", F_OK) == 0; return v; } }
// Per-game call hooks of the recompiled code (lifter env CC3_GATES / CC3_HOOKS).
bool game_gate(CPU* c, uint32_t fn)   // true = skip the call (eax = the value given in CC3_GATES)
{
	if (fn == gb::ResModeCheck)
	{
		// with Wide screen on, the modes of the screen's own shape join the resolution list (the game accepts
		// standard aspect ratios only); everything else goes through the game's own check
		if (!rt::config.wideScreen) return false;
		uint32_t m = guest::load<uint32_t>(c->r[ESP] + 4), bpp = guest::load<uint32_t>(c->r[ESP] + 8);
		if (m < 0x10000 || bpp < 32) return false;
		uint32_t w = guest::load<uint32_t>(m), h = guest::load<uint32_t>(m + 4);
		bool wide = h >= 600 && (double)w / h > 16.0 / 9.0 + 0.05;
		static int nl = 0;
		if (wide && nl++ < 8) rt::log("wide screen: resolution list accepts %u x %u", w, h);
		return wide;
	}
	if (fn == gb::TexSizePow2)
	{
		// (uint* w, uint* h): each rounded up to a power of two, at most the device's max texture size. The game's loop
		// never ends for a size above 2^31 (a broken input, see GridSetup); same result here, without the hang
		uint32_t caps = guest::load<uint32_t>(gb::DevCapsPtr);
		if (!caps) return true;
		for (int k = 0; k < 2; k++)
		{
			uint32_t p = guest::load<uint32_t>(c->r[ESP] + 4 + 4 * k), v = guest::load<uint32_t>(p);
			uint32_t mx = guest::load<uint32_t>(caps + 0x60 + 4 * k);
			uint64_t r = 1;
			while (r < v) r <<= 1;
			uint32_t out = r < mx ? (uint32_t)r : mx;
			static int nl = 0;
			if (v > mx && nl++ < 10) rt::log("gate: texture size %u (0x%x) above the device limit %u - clamped", v, v, mx);
			guest::store<uint32_t>(p, out);
		}
		return true;
	}
	if (fn == gb::GridSetup)
	{
		// (map heightmap*, float cellX, float cellY)
		static int nl = 0;
		if (nl++ < 6)
		{
			uint32_t hm = guest::load<uint32_t>(c->r[ESP] + 4);
			float fx = guest::load<float>(c->r[ESP] + 8), fy = guest::load<float>(c->r[ESP] + 12);
			if (hm) rt::log("gate: grid set-up map %d x %d border %d, cell %g x %g", (int32_t)guest::load<uint32_t>(hm + 0x14), (int32_t)guest::load<uint32_t>(hm + 0x18), (int32_t)guest::load<uint32_t>(hm + 0x1c), fx, fy);
			else rt::log("gate: grid set-up without a map, cell %g x %g", fx, fy);
		}
		return false;
	}
	return false;
}
void game_hook_after(CPU* c, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t ecx)
{
	(void)a1; (void)a2;
	static int nl = 0;
	if (nl > 40) return;
	if (fn == gb::ChunkStreamCtor && ecx)
	{
		nl++;
		uint32_t file = guest::load<uint32_t>(ecx), size = guest::load<uint32_t>(ecx + 0x14);
		uint32_t vt = file ? guest::load<uint32_t>(file) : 0;
		uint8_t head[16] = {};
		rt::log("hook: chunk stream %08x on file %08x (vtable %08x) size %u (arg %08x)", ecx, file, vt, size, a0);
		(void)head;
	}
	else if (fn == gb::HeightMapParse && ecx)
	{
		nl++;
		rt::log("hook: height map %08x parsed -> eax %08x: %d x %d border %d", ecx, c->r[EAX], (int32_t)guest::load<uint32_t>(ecx + 0x14), (int32_t)guest::load<uint32_t>(ecx + 0x18), (int32_t)guest::load<uint32_t>(ecx + 0x1c));
	}
}
