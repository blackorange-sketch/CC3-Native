#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <mutex>

#include "rt.h"

namespace rt
{
namespace
{
uint32_t watchAddr = getenv("CC3_WATCH") ? (uint32_t)strtoul(getenv("CC3_WATCH"), nullptr, 16) : 0;
void watchLog(const char* what, uint32_t p, uint32_t sz)
{
	CPU* c = thisThread().cpu;
	log("heap %s %08x size %u (ret %08x)", what, p, sz, c ? guest::load<uint32_t>(c->r[ESP]) : 0);
}
// Block header lives 16 bytes before the payload: {size, magic, pad...}. Size classes are powers of
// two from 32 bytes; larger blocks come straight from the arena and are recycled by exact size.
constexpr uint32_t Magic = 0xa110ca7e;
std::mutex mu;
uint32_t bump = HeapBase, committedTo = HeapBase, lastReq = 0;
uint64_t liveBytes = 0, freeBytes = 0;
std::map<uint32_t, std::vector<uint32_t>> freeBySize;   // class size -> payload addresses

uint32_t classOf(uint32_t n)
{
	// Tight classes keep fragmentation and waste low: powers of two up to 256 B, quarter-octave steps up to
	// 1 MiB, 64 KiB multiples above (big stream buffers).
	if (n <= 256) { uint32_t s = 32; while (s < n) s <<= 1; return s; }
	if (n > (1u << 20)) return (n + 0xffffu) & ~0xffffu;
	uint32_t s = 256;
	while (s * 2 < n) s <<= 1;
	uint32_t step = s / 4;
	return (n + step - 1) / step * step;
}

void commitTo(uint32_t end)
{
	if (end <= committedTo) return;
	uint32_t newEnd = (end + 0xfffff) & ~0xfffffu;   // 1 MiB steps
	if (newEnd > HeapEnd) fatal("guest heap exhausted (request %u bytes, live %llu MiB, free-listed %llu MiB)", lastReq, (unsigned long long)(liveBytes >> 20), (unsigned long long)(freeBytes >> 20));
	if (!guest::commit(committedTo, newEnd - committedTo)) fatal("cannot commit guest heap");
	committedTo = newEnd;
}
}

void heapInit() {}
uint64_t heapLiveBytes() { std::lock_guard<std::mutex> l(mu); return liveBytes; }

// heap census (leak hunt): 1 in 16 blocks remember who allocated them (the game's return address and its caller,
// set by the malloc imports through gAllocSite; 0 = the runtime itself)
thread_local uint64_t gAllocSite = 0;
namespace
{
std::unordered_map<uint32_t, std::pair<uint64_t, uint32_t>> census;
std::unordered_map<uint64_t, int64_t> siteLive, siteLast;
}
std::string heapCensusLine()
{
	std::lock_guard<std::mutex> l(mu);
	std::vector<std::pair<int64_t, uint64_t>> grow;
	for (auto& kv : siteLive) grow.push_back({kv.second - siteLast[kv.first], kv.first});
	std::sort(grow.begin(), grow.end(), [](auto& a, auto& b) { return a.first > b.first; });
	std::string o;
	char b[96];
	for (size_t i = 0; i < grow.size() && i < 10; i++)
	{
		uint64_t s = grow[i].second;
		snprintf(b, sizeof b, " %08x<%08x:%+lldK(%lldK)", (uint32_t)s, (uint32_t)(s >> 32), (long long)(grow[i].first * 16 / 1024), (long long)(siteLive[s] * 16 / 1024));
		o += b;
	}
	siteLast = siteLive;
	return o;
}
namespace { void (*reclaimHook)() = nullptr; thread_local bool inReclaim = false; }
void setHeapReclaim(void (*fn)()) { reclaimHook = fn; }

uint32_t gmalloc(uint32_t size, uint32_t align)
{
	std::unique_lock<std::mutex> l(mu);
	uint32_t cls = classOf(size ? size : 1);
	if (align > 16) cls = classOf(cls + align);
	lastReq = size;
	// about to run out: let the owner of released data (D3D textures the game let go of) free it, then retry
	if (reclaimHook && !inReclaim && freeBySize[cls].empty() && ((bump + 31) & ~15u) + cls > HeapEnd)
	{
		l.unlock();
		inReclaim = true;
		reclaimHook();
		inReclaim = false;
		l.lock();
	}
	auto& fl = freeBySize[cls];
	uint32_t block;
	if (!fl.empty()) { block = fl.back(); fl.pop_back(); freeBytes -= cls; }
	else
	{
		block = (bump + 15) & ~15u;
		bump = block + 16 + cls;
		commitTo(bump);
		block += 16;
	}
	uint32_t payload = block;
	if (align > 16)
	{
		payload = (block + align - 1) & ~(align - 1);
		if (payload == block) payload += align;   // keep room for the header
	}
	liveBytes += cls;
	{
		uint64_t site = gAllocSite;
		gAllocSite = 0;
		if (((payload >> 4) & 15) == 0) { census[payload] = {site, cls}; siteLive[site] += cls; }   // 1 in 16 blocks
	}
	guest::store<uint32_t>(payload - 16, cls);
	guest::store<uint32_t>(payload - 12, Magic);
	guest::store<uint32_t>(payload - 8, block);   // original block start (differs when over-aligned)
	guest::store<uint32_t>(payload - 4, size);
	if (watchAddr && payload <= watchAddr && watchAddr < payload + cls) watchLog("alloc", payload, size);
	return payload;
}

void gfree(uint32_t p)
{
	if (!p) return;
	std::lock_guard<std::mutex> l(mu);
	if (guest::load<uint32_t>(p - 12) != Magic) { log("free of a foreign pointer %08x", p); return; }
	uint32_t cls = guest::load<uint32_t>(p - 16), block = guest::load<uint32_t>(p - 8);
	if (watchAddr && p <= watchAddr && watchAddr < p + guest::load<uint32_t>(p - 16)) watchLog("free", p, 0);
	guest::store<uint32_t>(p - 12, 0);
	if (((p >> 4) & 15) == 0)
	{
		auto it = census.find(p);
		if (it != census.end()) { siteLive[it->second.first] -= it->second.second; census.erase(it); }
	}
	freeBySize[cls].push_back(block);
	liveBytes -= cls; freeBytes += cls;
}

uint32_t gsize(uint32_t p) { return p ? guest::load<uint32_t>(p - 4) : 0; }

uint32_t grealloc(uint32_t p, uint32_t size)
{
	if (!p) return gmalloc(size);
	uint32_t old = gsize(p);
	uint32_t cls = guest::load<uint32_t>(p - 16);
	if (size <= cls - 0 && size <= cls && guest::load<uint32_t>(p - 8) == p) { guest::store<uint32_t>(p - 4, size); return p; }
	uint32_t n = gmalloc(size);
	memcpy(guest::host(n), guest::host(p), std::min(old, size));
	gfree(p);
	return n;
}

namespace
{
std::mutex vmu;
uint32_t vmNext = VmBase;
std::map<uint32_t, uint32_t> vmLive;   // VirtualAlloc'ed blocks: address -> size
std::map<uint32_t, uint32_t> vmFree;   // released ranges (merged): address -> size
uint64_t vmLiveBytes = 0;
}

// VirtualAlloc: 64 KiB granular, committed, zeroed. Released blocks (VirtualFree MEM_RELEASE) are reused first fit -
// CC3's memory pools and the movie player allocate and release large blocks all the time.
uint32_t vmAlloc(uint32_t size)
{
	std::lock_guard<std::mutex> l(vmu);
	uint32_t sz = (size + 0xffff) & ~0xffffu;
	if (!sz) return 0;
	for (auto it = vmFree.begin(); it != vmFree.end(); ++it)
		if (it->second >= sz)
		{
			uint32_t a = it->first, rest = it->second - sz;
			vmFree.erase(it);
			if (rest) vmFree[a + sz] = rest;
			memset(guest::host(a), 0, sz);
			vmLive[a] = sz; vmLiveBytes += sz;
			return a;
		}
	uint32_t a = vmNext;
	if ((uint64_t)a + sz > VmEnd)
	{
		static int n = 0;
		if (n++ < 5) log("VirtualAlloc: %u bytes refused - region full (live %llu MiB in %zu blocks, %zu free ranges)", size, (unsigned long long)(vmLiveBytes >> 20), vmLive.size(), vmFree.size());
		return 0;
	}
	if (!guest::commit(a, sz)) return 0;
	vmNext += sz;
	vmLive[a] = sz; vmLiveBytes += sz;
	return a;
}
bool vmRelease(uint32_t addr)
{
	std::lock_guard<std::mutex> l(vmu);
	auto it = vmLive.find(addr);
	if (it == vmLive.end()) return false;
	uint32_t a = it->first, sz = it->second;
	vmLive.erase(it); vmLiveBytes -= sz;
	auto nx = vmFree.lower_bound(a);
	if (nx != vmFree.end() && nx->first == a + sz) { sz += nx->second; vmFree.erase(nx); }
	auto pv = vmFree.lower_bound(a);
	if (pv != vmFree.begin()) { --pv; if (pv->first + pv->second == a) { pv->second += sz; return true; } }
	vmFree[a] = sz;
	return true;
}
}
