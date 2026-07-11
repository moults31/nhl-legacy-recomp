// nhllegacy WASM port — HTTP Range VFS device implementation.
// See http_range_device.h for the design overview.

#include "http_range_device.h"

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <span>
#include <sstream>
#include <utility>

#include <emscripten.h>
#include <emscripten/fetch.h>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/system/xtypes.h>

#include "lzx_decompress.h"

namespace nhllegacy {

namespace fs = rex::filesystem;
using rex::X_STATUS;

// ============================================================================
//  Minimal JSON parser — enough for the manifest format
// ============================================================================
namespace {

void SkipWhitespace(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
}

std::string ReadJsonString(const char*& p, const char* end) {
    SkipWhitespace(p, end);
    if (p >= end || *p != '"') return {};
    ++p;
    std::string s;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
            ++p;
            switch (*p) {
            case '"':  s += '"';  break;
            case '\\': s += '\\'; break;
            case '/':  s += '/';  break;
            case 'n':  s += '\n'; break;
            case 'r':  s += '\r'; break;
            case 't':  s += '\t'; break;
            default:   s += *p;   break;
            }
        } else {
            s += *p;
        }
        ++p;
    }
    if (p < end) ++p;
    return s;
}

uint64_t ReadJsonNumber(const char*& p, const char* end) {
    SkipWhitespace(p, end);
    uint64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + static_cast<uint64_t>(*p - '0');
        ++p;
    }
    return v;
}

void SkipJsonValue(const char*& p, const char* end) {
    SkipWhitespace(p, end);
    if (p >= end) return;
    if (*p == '"') { ReadJsonString(p, end); return; }
    if (*p == '{') {
        ++p; int d = 1;
        while (p < end && d > 0) {
            if (*p == '{') ++d;
            else if (*p == '}') --d;
            else if (*p == '"') { ReadJsonString(p, end); continue; }
            ++p;
        }
        return;
    }
    if (*p == '[') {
        ++p; int d = 1;
        while (p < end && d > 0) {
            if (*p == '[') ++d;
            else if (*p == ']') --d;
            else if (*p == '"') { ReadJsonString(p, end); continue; }
            ++p;
        }
        return;
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']') ++p;
}

}  // namespace

// ============================================================================
//  Blocking HTTP fetch (pthreads-compatible)
// ============================================================================
namespace {

std::vector<uint8_t> FetchBlocking(const std::string& url,
                                   uint64_t range_start = 0,
                                   uint64_t range_end = 0) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
    attr.timeoutMSecs = 30000;

    std::string range_value;
    const char* extra_hdrs[3] = {nullptr, nullptr, nullptr};
    if (range_end > range_start) {
        range_value = "bytes=" + std::to_string(range_start) + "-" +
                      std::to_string(range_end - 1);
        extra_hdrs[0] = "Range";
        extra_hdrs[1] = range_value.c_str();
        attr.requestHeaders = extra_hdrs;
    }

    emscripten_fetch_t* fetch = emscripten_fetch(&attr, url.c_str());

    std::vector<uint8_t> result;
    if (fetch && (fetch->status == 200 || fetch->status == 206) &&
        fetch->data && fetch->numBytes > 0) {
        result.assign(fetch->data, fetch->data + fetch->numBytes);
    }
    if (fetch) emscripten_fetch_close(fetch);
    return result;
}

}  // namespace

// ============================================================================
//  OPFS file I/O helpers
// ============================================================================
namespace {

bool WriteFileOpfs(const std::filesystem::path& path,
                   const void* data, size_t size) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data),
            static_cast<std::streamsize>(size));
    return f.good();
}

std::vector<uint8_t> ReadFileOpfs(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
}

std::filesystem::path CachePath(const std::filesystem::path& root,
                                const std::string& sha256_hex) {
    if (sha256_hex.size() < 4) return root / sha256_hex;
    return root / sha256_hex.substr(0, 2) / sha256_hex;
}

}  // namespace

// ============================================================================
//  MemFile — serves an in-memory buffer as a File
// ============================================================================
namespace {

class MemFile final : public fs::File {
public:
    MemFile(uint32_t access, fs::Entry* entry,
            std::shared_ptr<const std::vector<uint8_t>> data)
        : fs::File(access, entry), data_(std::move(data)) {}

    void Destroy() override { delete this; }

    X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                      size_t* out) override {
        const auto& d = *data_;
        if (byte_offset >= d.size()) {
            if (out) *out = 0;
            return X_STATUS_END_OF_FILE;
        }
        size_t n = std::min(buffer.size(), d.size() - byte_offset);
        std::memcpy(buffer.data(), d.data() + byte_offset, n);
        if (out) *out = n;
        return X_STATUS_SUCCESS;
    }

    X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t* out) override {
        if (out) *out = 0;
        return X_STATUS_UNSUCCESSFUL;
    }

private:
    std::shared_ptr<const std::vector<uint8_t>> data_;
};

}  // namespace

// ============================================================================
//  HttpRangeEntry
// ============================================================================

HttpRangeEntry::HttpRangeEntry(fs::Device* device, fs::Entry* parent,
                               std::string_view path, bool is_dir,
                               uint64_t size, const ChunkDescriptor* desc)
    : fs::Entry(device, parent, path),
      is_dir_(is_dir),
      chunk_(desc) {
    size_ = static_cast<size_t>(size);
    allocation_size_ = size_;
    attributes_ = is_dir_
                      ? fs::kFileAttributeDirectory
                      : (fs::kFileAttributeNormal | fs::kFileAttributeReadOnly);
    size_t slash = path.find_last_of("\\/");
    name_ = std::string(slash == std::string_view::npos
                            ? path
                            : path.substr(slash + 1));
}

X_STATUS HttpRangeEntry::Open(uint32_t desired_access, fs::File** out_file) {
    // Directory handles: the guest opens dirs for relative-path resolution
    // (GetChild / ResolvePath on the entry). Return an empty MemFile so
    // ReadSync returns EOF immediately — the guest only queries, never reads
    // directory data.
    if (is_dir_) {
        static const auto kEmpty = std::make_shared<const std::vector<uint8_t>>();
        *out_file = new MemFile(desired_access, this, kEmpty);
        return X_STATUS_SUCCESS;
    }
    if (!chunk_) return X_STATUS_NO_SUCH_FILE;

    auto data = ResolvePayload();
    if (!data) return X_STATUS_NO_SUCH_FILE;

    *out_file = new MemFile(desired_access, this, data);
    return X_STATUS_SUCCESS;
}

std::shared_ptr<const std::vector<uint8_t>> HttpRangeEntry::ResolvePayload() {
    auto* dev = static_cast<HttpRangeDevice*>(device_);
    const auto& bundle_url = dev->bundle_url();
    const auto& cache_root = dev->opfs_cache_dir();

    // 1. Check OPFS cache.
    if (!chunk_->sha256.empty() && !cache_root.empty()) {
        auto path = CachePath(cache_root, chunk_->sha256);
        auto cached = ReadFileOpfs(path);
        if (!cached.empty() && cached.size() == chunk_->uncompressed_size) {
            return std::make_shared<const std::vector<uint8_t>>(
                std::move(cached));
        }
    }

    // 2. Fetch from HTTP bundle via Range request.
    uint64_t end = chunk_->offset + chunk_->compressed_size;
    auto raw = FetchBlocking(bundle_url, chunk_->offset, end);
    if (raw.empty() || raw.size() != chunk_->compressed_size) {
        REXLOG_WARN("[http-range] fetch failed for {} ({} bytes @ {})",
                    path(), chunk_->compressed_size, chunk_->offset);
        return nullptr;
    }

    // 3. Decompress if needed.
    std::shared_ptr<const std::vector<uint8_t>> result;
    if (chunk_->compression == "lzx") {
        try {
            auto dec = lzx::Decompress(raw);
            result = std::make_shared<const std::vector<uint8_t>>(
                std::move(dec));
        } catch (const std::exception& e) {
            REXLOG_ERROR("[http-range] LZX decompress failed for {}: {}",
                         path(), e.what());
            return nullptr;
        }
    } else if (chunk_->compression == "deflate") {
        try {
            auto dec = lzx::DecompressDeflate(raw, chunk_->uncompressed_size);
            result = std::make_shared<const std::vector<uint8_t>>(
                std::move(dec));
        } catch (const std::exception& e) {
            REXLOG_ERROR("[http-range] deflate decompress failed for {}: {}",
                         path(), e.what());
            return nullptr;
        }
    } else {
        result = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
    }

    // 4. Persist to OPFS for next time.
    if (result && !chunk_->sha256.empty() && !cache_root.empty()) {
        WriteFileOpfs(CachePath(cache_root, chunk_->sha256),
                      result->data(), result->size());
    }

    return result;
}

// ============================================================================
//  HttpRangeDevice
// ============================================================================

HttpRangeDevice::HttpRangeDevice(std::string_view mount_path,
                                 std::string bundle_url,
                                 Manifest manifest,
                                 std::filesystem::path opfs_cache_dir)
    : fs::Device(mount_path),
      name_("\\Device\\HttpRange"),
      bundle_url_(std::move(bundle_url)),
      manifest_(std::move(manifest)),
      opfs_cache_dir_(std::move(opfs_cache_dir)) {}

HttpRangeDevice::~HttpRangeDevice() = default;

bool HttpRangeDevice::Initialize() {
    struct Info {
        std::string full_path;
        bool        is_dir;
        uint64_t    size = 0;
        const ChunkDescriptor* chunk = nullptr;
    };

    std::unordered_map<std::string, Info> entries;

    for (const auto& [path, desc] : manifest_) {
        uint64_t sz = desc.uncompressed_size;
        entries[path] = {path, false, sz, &desc};

        // Register all ancestor directories.
        for (size_t p = 0; p < path.size(); ) {
            size_t s = path.find_first_of("\\/", p);
            if (s == std::string::npos) break;
            std::string dir = path.substr(0, s);
            entries.try_emplace(dir, Info{dir, true, 0, nullptr});
            p = s + 1;
        }
    }

    // Sort by path so parents always precede children.
    std::vector<std::string> ordered;
    ordered.reserve(entries.size());
    for (const auto& [p, _] : entries) ordered.push_back(p);
    std::sort(ordered.begin(), ordered.end());

    std::unordered_map<std::string, HttpRangeEntry*> ptrs;

    for (const std::string& p : ordered) {
        const Info& info = entries[p];

        HttpRangeEntry* parent = nullptr;
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) {
            std::string pp = p.substr(0, slash);
            auto it = ptrs.find(pp);
            if (it != ptrs.end()) parent = it->second;
        }

        auto entry = std::make_unique<HttpRangeEntry>(
            this, parent, p, info.is_dir, info.size, info.chunk);

        HttpRangeEntry* raw = entry.get();
        ptrs[p] = raw;

        if (parent) {
            parent->AddChild(std::move(entry));
        } else {
            // Top-level file — attach to root directory
            if (!root_) {
                root_ = std::make_unique<HttpRangeEntry>(this, nullptr, "",
                                                          /*is_dir=*/true, 0, nullptr);
            }
            static_cast<HttpRangeEntry*>(root_.get())->AddChild(std::move(entry));
        }
    }

    // Safety: if the manifest was empty, create a bare root so ResolvePath
    // doesn't crash.
    if (!root_) {
        root_ = std::make_unique<HttpRangeEntry>(this, nullptr, "",
                                                  /*is_dir=*/true, 0, nullptr);
    }

    return true;
}

void HttpRangeDevice::Dump(rex::string::StringBuffer*) {}

fs::Entry* HttpRangeDevice::ResolvePath(std::string_view path) {
    if (!root_) return nullptr;
    size_t i = 0;
    while (i < path.size() && (path[i] == '\\' || path[i] == '/')) ++i;
    return root_->ResolvePath(path.substr(i));
}

// ============================================================================
//  Manifest parser
// ============================================================================

bool HttpRangeDevice::LoadManifestFromString(const std::string& json,
                                             Manifest& out) {
    const char* p = json.data();
    const char* e = p + json.size();

    SkipWhitespace(p, e);
    if (p >= e || *p != '{') return false;
    ++p;

    // Find the "files" key — skip any other top-level keys (version, etc.).
    bool found_files = false;
    while (p < e) {
        SkipWhitespace(p, e);
        if (p >= e || *p == '}') break;
        if (*p == ',') { ++p; continue; }
        std::string tk = ReadJsonString(p, e);
        SkipWhitespace(p, e);
        if (p >= e || *p != ':') return false;
        ++p;
        if (tk == "files") {
            found_files = true;
            break;
        }
        SkipJsonValue(p, e);
    }
    if (!found_files) return false;

    SkipWhitespace(p, e);
    if (p >= e || *p != '{') return false;
    ++p;

    for (;;) {
        SkipWhitespace(p, e);
        if (p >= e) break;
        if (*p == '}') { ++p; break; }
        if (*p == ',') { ++p; continue; }

        std::string fpath = ReadJsonString(p, e);
        SkipWhitespace(p, e);
        if (p >= e || *p != ':') return false;
        ++p;

        SkipWhitespace(p, e);
        if (p >= e || *p != '{') return false;
        ++p;

        ChunkDescriptor d;
        for (;;) {
            SkipWhitespace(p, e);
            if (p >= e) break;
            if (*p == '}') { ++p; break; }
            if (*p == ',') { ++p; continue; }

            std::string k = ReadJsonString(p, e);
            SkipWhitespace(p, e);
            if (p >= e || *p != ':') return false;
            ++p;

            if (k == "o" || k == "offset")
                d.offset = ReadJsonNumber(p, e);
            else if (k == "s" || k == "compressed_size")
                d.compressed_size = ReadJsonNumber(p, e);
            else if (k == "us" || k == "uncompressed_size")
                d.uncompressed_size = ReadJsonNumber(p, e);
            else if (k == "c" || k == "compression")
                d.compression = ReadJsonString(p, e);
            else if (k == "h" || k == "sha256")
                d.sha256 = ReadJsonString(p, e);
            else
                SkipJsonValue(p, e);
        }

        if (!fpath.empty() && d.compressed_size > 0)
            out[fpath] = d;
    }

    return true;
}

bool HttpRangeDevice::LoadManifestFromUrl(const std::string& manifest_url,
                                          Manifest& out) {
    auto body = FetchBlocking(manifest_url);
    if (body.empty()) return false;

    std::string json(reinterpret_cast<const char*>(body.data()), body.size());
    return LoadManifestFromString(json, out);
}

// ============================================================================
//  WasmOverlayDevice
// ============================================================================

WasmOverlayDevice::WasmOverlayDevice(
    std::string_view mount_path,
    const std::filesystem::path& writable_root,
    std::string bundle_url,
    Manifest manifest,
    const std::filesystem::path& opfs_cache)
    : fs::Device(mount_path) {
    upper_ = std::make_unique<fs::HostPathDevice>(
        mount_path, writable_root, /*read_only=*/false);
    lower_ = std::make_unique<HttpRangeDevice>(
        mount_path, std::move(bundle_url), std::move(manifest), opfs_cache);
}

WasmOverlayDevice::~WasmOverlayDevice() = default;

bool WasmOverlayDevice::Initialize() {
    if (!upper_->Initialize()) return false;
    lower_->Initialize();  // best-effort — network may be unavailable
    return true;
}

void WasmOverlayDevice::Dump(rex::string::StringBuffer* buf) {
    upper_->Dump(buf);
}

fs::Entry* WasmOverlayDevice::ResolvePath(std::string_view path) {
    fs::Entry* u = upper_->ResolvePath(path);
    return u ? u : lower_->ResolvePath(path);
}

// ============================================================================
//  SetupWasmVfs — OPFS mount + device assembly
// ============================================================================

namespace {

EM_JS(void, wasm_mount_opfs, (), {
    try {
        FS.mkdir("/opfs");
        FS.mount(FS.filesystems.OPFS, {}, "/opfs");
        FS.mkdir("/opfs/guest");
        FS.mkdir("/opfs/cache");
    } catch (e) {
        console.error("[nhl-wasm] OPFS mount error:", e);
    }
});

}  // namespace

fs::Device* SetupWasmVfs(fs::VirtualFileSystem* vfs,
                          const std::string& bundle_url,
                          const std::string& manifest_url) {
    if (!vfs) return nullptr;

    // 1. Mount OPFS persistent storage.
    wasm_mount_opfs();

    // 2. Ensure the writable guest directory exists.
    std::error_code ec;
    std::filesystem::create_directories("/opfs/guest", ec);

    // 3. Try to load the manifest; if it fails we still provide a
    //    writable-only HostPathDevice (no overlay).
    Manifest manifest;
    if (!HttpRangeDevice::LoadManifestFromUrl(manifest_url, manifest)) {
        REXLOG_WARN("[nhl-wasm] manifest load failed from {} — "
                    "HTTP asset layer unavailable", manifest_url);
        auto dev = std::make_unique<fs::HostPathDevice>(
            "\\CACHE", "/opfs/guest", /*read_only=*/false);
        if (dev->Initialize() && vfs->RegisterDevice(std::move(dev))) {
            vfs->RegisterSymbolicLink("cache:", "\\CACHE");
            REXLOG_INFO("[nhl-wasm] writable-only cache: mounted on OPFS");
        }
        return nullptr;
    }

    REXLOG_INFO("[nhl-wasm] manifest loaded: {} file(s)", manifest.size());

    // 4. Create the overlay device.
    auto overlay = std::make_unique<WasmOverlayDevice>(
        "\\CACHE", "/opfs/guest", bundle_url,
        std::move(manifest), "/opfs/cache");

    fs::Device* raw = overlay.get();
    if (!overlay->Initialize()) {
        REXLOG_ERROR("[nhl-wasm] WasmOverlayDevice init failed");
        return nullptr;
    }

    if (!vfs->RegisterDevice(std::move(overlay))) {
        REXLOG_ERROR("[nhl-wasm] VFS device registration failed");
        return nullptr;
    }

    vfs->RegisterSymbolicLink("cache:", "\\CACHE");
    REXLOG_INFO("[nhl-wasm] WASM VFS ready — OPFS writable + HTTP lower");
    return raw;
}

}  // namespace nhllegacy

#endif  // __EMSCRIPTEN__
