// nhllegacy WASM port — HTTP Range VFS device.
//
// Reads assets from a pre-packed bundle served over HTTP via partial
// Range requests. Each fetched chunk is optionally LZX-decompressed and
// cached in OPFS for warm restarts.
//
// Design:
//   1. A JSON manifest maps logical guest paths (cache:\foo\bar.bin) to
//      {offset, size, uncompressed_size, compression, hash} descriptors
//      within a single monolithic .bundle file.
//   2. On Open(), the device checks OPFS cache first. On miss it issues a
//      blocking HTTP Range request for the descriptor's byte range.
//   3. If the chunk is LZX-compressed, it decompresses via the inline
//      lzx::Decompress() (src/lzx_decompress.h).
//   4. The decompressed payload is persisted to OPFS for next time, and
//      then served from an in-memory buffer (MemFile).
//
// This device is read-only and designed to be the lower layer of a union
// mount (see WasmOverlayDevice). The writable upper layer (saves, shader
// cache, etc.) is a standard HostPathDevice backed by OPFS.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef __EMSCRIPTEN__

#include <rex/filesystem/device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/vfs.h>

namespace nhllegacy {

// ———————————————————————————————————————————————————————————————————————————
//  ChunkDescriptor — one entry in the bundle manifest
// ———————————————————————————————————————————————————————————————————————————
struct ChunkDescriptor {
    uint64_t offset = 0;            // byte offset in the .bundle file
    uint64_t compressed_size = 0;   // bytes to fetch via Range request
    uint64_t uncompressed_size = 0; // bytes after decompression
    std::string compression;        // "none" or "lzx"
    std::string sha256;             // hex-encoded SHA-256 of the uncompressed data
};

using Manifest = std::unordered_map<std::string, ChunkDescriptor>;

// ———————————————————————————————————————————————————————————————————————————
//  HttpRangeDevice — read-only VFS device for the WASM bundle
// ———————————————————————————————————————————————————————————————————————————
class HttpRangeDevice : public rex::filesystem::Device {
public:
    // `mount_path`   : VFS mount point, typically "\\CACHE"
    // `bundle_url`   : full URL to the .bundle file (e.g. "/data/nhllegacy.bundle")
    // `manifest`     : pre-parsed manifest (path → descriptor)
    // `opfs_cache_dir`: OPFS directory for decompressed-file cache
    HttpRangeDevice(std::string_view mount_path,
                    std::string bundle_url,
                    Manifest manifest,
                    std::filesystem::path opfs_cache_dir);
    ~HttpRangeDevice() override;

    bool Initialize() override;
    void Dump(rex::string::StringBuffer* string_buffer) override;
    rex::filesystem::Entry* ResolvePath(std::string_view path) override;

    bool is_read_only() const override { return true; }
    const std::string& name() const override { return name_; }
    uint32_t attributes() const override { return 0; }
    uint32_t component_name_max_length() const override { return 255; }
    uint32_t total_allocation_units() const override { return 128 * 1024; }
    uint32_t available_allocation_units() const override { return 0; }
    uint32_t sectors_per_allocation_unit() const override { return 1; }
    uint32_t bytes_per_sector() const override { return 0x200; }

    // Manifest loading: loads a JSON manifest from a URL or string.
    // Returns true on success.
    static bool LoadManifestFromString(const std::string& json, Manifest& out);
    static bool LoadManifestFromUrl(const std::string& manifest_url, Manifest& out);

    const std::string& bundle_url() const { return bundle_url_; }
    const Manifest& manifest() const { return manifest_; }
    const std::filesystem::path& opfs_cache_dir() const { return opfs_cache_dir_; }

private:
    std::string name_;
    std::string bundle_url_;
    Manifest manifest_;
    std::filesystem::path opfs_cache_dir_;
    std::unique_ptr<rex::filesystem::Entry> root_;
};

// ———————————————————————————————————————————————————————————————————————————
//  HttpRangeEntry — declared here so the Device can friend access children_
// ———————————————————————————————————————————————————————————————————————————
class HttpRangeEntry final : public rex::filesystem::Entry {
public:
    HttpRangeEntry(rex::filesystem::Device* device, rex::filesystem::Entry* parent,
                   std::string_view path, bool is_dir,
                   uint64_t size = 0, const ChunkDescriptor* desc = nullptr);

    rex::X_STATUS Open(uint32_t desired_access, rex::filesystem::File** out_file) override;
    bool can_map() const override { return false; }

private:
    void AddChild(std::unique_ptr<HttpRangeEntry> child) {
        children_.push_back(std::move(child));
    }

    std::shared_ptr<const std::vector<uint8_t>> ResolvePayload();

    bool is_dir_;
    const ChunkDescriptor* chunk_;
    friend class HttpRangeDevice;
};

// ———————————————————————————————————————————————————————————————————————————
//  WasmOverlayDevice — writable OPFS over HTTP Range (union mount for WASM)
// ———————————————————————————————————————————————————————————————————————————
//
// Stacks a writable HostPathDevice (backed by OPFS, for saves / shader
// cache) over a read-only HttpRangeDevice (game assets from the bundle).
// Path resolution prefers the writable upper layer and falls back to the
// HTTP-based lower layer; creates and writes always land in the upper.
//
// Intended use: the game probes cache:\<path>; the overlay resolves it
// through upper first (OPFS — existing loose files, saves, shader cache)
// then lower (HTTP bundle — pre-packed game assets).  This avoids the
// write-through-to-network problem and keeps OPFS isolated.
//
class WasmOverlayDevice : public rex::filesystem::Device {
public:
    // `mount_path`       : VFS mount point ("\\CACHE")
    // `writable_root`    : OPFS path for the writable upper layer
    // `bundle_url`       : URL of the pre-packed .bundle file
    // `manifest`         : pre-parsed manifest
    // `opfs_cache`       : OPFS path for the decompressed-file cache
    WasmOverlayDevice(std::string_view mount_path,
                      const std::filesystem::path& writable_root,
                      std::string bundle_url,
                      Manifest manifest,
                      const std::filesystem::path& opfs_cache);
    ~WasmOverlayDevice() override;

    bool Initialize() override;
    void Dump(rex::string::StringBuffer* string_buffer) override;
    rex::filesystem::Entry* ResolvePath(std::string_view path) override;

    bool is_read_only() const override { return false; }
    const std::string& name() const override { return upper_->name(); }
    uint32_t attributes() const override { return upper_->attributes(); }
    uint32_t component_name_max_length() const override {
        return upper_->component_name_max_length();
    }
    uint32_t total_allocation_units() const override {
        return upper_->total_allocation_units();
    }
    uint32_t available_allocation_units() const override {
        return upper_->available_allocation_units();
    }
    uint32_t sectors_per_allocation_unit() const override {
        return upper_->sectors_per_allocation_unit();
    }
    uint32_t bytes_per_sector() const override {
        return upper_->bytes_per_sector();
    }

private:
    std::unique_ptr<rex::filesystem::Device> upper_;
    std::unique_ptr<rex::filesystem::Device> lower_;
};

// ———————————————————————————————————————————————————————————————————————————
//  SetupWasmVfs — one-shot wiring of the full WASM VFS
// ———————————————————————————————————————————————————————————————————————————
// Called from OnPostSetup when `__EMSCRIPTEN__` is defined. It:
//   1. Mounts OPFS at /opfs (via JS interop).
//   2. Creates writable guest directories under /opfs/guest/.
//   3. Loads the manifest from the server (or an inline default).
//   4. Assembles the WasmOverlayDevice and registers it at \\CACHE.
//   5. Registers the "cache:" → "\\CACHE" symlink.
//
// `bundle_url` and `manifest_url` default to well-known paths relative
// to the page origin (/data/).
//
// Integration in nhllegacy_app.h OnPostSetup():
//
//   #ifdef __EMSCRIPTEN__
//     nhllegacy::SetupWasmVfs(runtime()->file_system());
//   #else
//     // ... existing HostPathDevice / UnionDevice logic ...
//   #endif
//
// Returns the overlay device pointer on success, nullptr on failure.
// The device is owned by the VFS — do not delete.
rex::filesystem::Device* SetupWasmVfs(
    rex::filesystem::VirtualFileSystem* vfs,
    const std::string& bundle_url = "/data/nhllegacy.bundle",
    const std::string& manifest_url = "/data/nhllegacy.manifest.json");

}  // namespace nhllegacy

#endif  // __EMSCRIPTEN__
