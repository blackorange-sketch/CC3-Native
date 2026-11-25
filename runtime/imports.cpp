#include "imports.h"
#include "guest.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

namespace imports
{
namespace
{
struct Reg { HostFn fn; int pop; };
struct DataReg { uint32_t size; DataInit init; };
std::map<std::string, DataReg>& dataRegistry() { static std::map<std::string, DataReg> r; return r; }
std::map<std::string, Reg>& registry() { static std::map<std::string, Reg> r; return r; }
std::vector<Entry>& entries() { static std::vector<Entry> e = [] { std::vector<Entry> v; v.reserve(1 << 16); return v; }(); return e; }   // never reallocates
std::atomic<size_t> published{0};
std::mutex addMu;

std::string key(std::string dll, const std::string& name)
{
	std::transform(dll.begin(), dll.end(), dll.begin(), [](unsigned char c) { return std::tolower(c); });
	return dll + "!" + name;
}
}

uint32_t (*dataAlloc)(uint32_t) = nullptr;
thread_local Entry* current = nullptr;
void registerFn(const char* dll, const char* name, HostFn fn, int pop) { registry()[key(dll, name)] = {fn, pop}; }
void registerData(const char* dll, const char* name, uint32_t size, DataInit init) { dataRegistry()[key(dll, name)] = {size, init}; }

uint32_t bind(const std::string& dll, const std::string& name)
{
	if (auto d = dataRegistry().find(key(dll, name)); d != dataRegistry().end() && dataAlloc)
	{
		uint32_t a = dataAlloc(d->second.size);
		if (d->second.init) *reinterpret_cast<uint32_t*>(guest::base + a) = d->second.init();
		return a;
	}
	uint32_t stub = StubBase + static_cast<uint32_t>(entries().size()) * StubStride;
	auto it = registry().find(key(dll, name));
	// DLL versions with the same API: an import of the alias falls back to the implementation registered for the target
	// (CC3 links d3dx9_29, the runtime's D3DX is registered as d3dx9_35)
	static const std::pair<const char*, const char*> aliases[] = {{"d3dx9_29.dll", "d3dx9_35.dll"}};
	if (it == registry().end())
		for (auto& [from, to] : aliases)
			if (key(dll, "") == key(from, "")) { it = registry().find(key(to, name)); break; }
	entries().push_back({dll, name, stub, it == registry().end() ? nullptr : it->second.fn, it == registry().end() ? 0 : it->second.pop});
	published = entries().size();
	return stub;
}

uint32_t addDynamic(const std::string& dll, const std::string& name, HostFn fn, int pop, uint32_t user)
{
	std::lock_guard<std::mutex> l(addMu);
	if (entries().size() >= (1u << 16)) abort();
	uint32_t stub = StubBase + static_cast<uint32_t>(entries().size()) * StubStride;
	entries().push_back({dll, name, stub, fn, pop, 0, user});
	published = entries().size();
	return stub;
}

Entry* lookup(uint32_t a)
{
	if (a < StubBase || (a - StubBase) % StubStride)
		return nullptr;

	size_t i = (a - StubBase) / StubStride;
	return i < published ? &entries()[i] : nullptr;
}

const std::vector<Entry>& all() { return entries(); }

bool knownDll(const std::string& dll)
{
	std::string k = key(dll, "");
	auto it = registry().lower_bound(k);
	return it != registry().end() && it->first.compare(0, k.size(), k) == 0;
}
uint32_t bindKnown(const std::string& dll, const std::string& name)
{
	static std::mutex mu;
	static std::map<std::string, uint32_t> cache;
	std::lock_guard<std::mutex> l(mu);
	auto c = cache.find(key(dll, name));
	if (c != cache.end()) return c->second;
	auto it = registry().find(key(dll, name));
	uint32_t stub = it == registry().end() ? 0 : addDynamic(dll, name, it->second.fn, it->second.pop, 0);
	return cache[key(dll, name)] = stub;
}
}
