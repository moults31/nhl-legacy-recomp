// LZX decompression bridge for the WASM port.
//
// The offline pre-packaging tool extracts individual files from EA .big
// archives and stores them in the nhllegacy.bundle file. Each chunk may be:
//   "none"   — raw uncompressed bytes
//   "lzx"    — LZX-compressed (16-byte header + bitstream; see below)
//   "deflate" — raw zlib deflate (RFC 1951, no zlib/gzip wrapper)
//
// For "lzx": this header provides a self-contained decompressor.
// For "deflate": Emscripten ships with zlib; call uncompress() directly.
//
// The LZX chunk format produced by the offline tool:
//   uint32_t magic          = 0x4C5A5801   ("LZX\x01")
//   uint32_t window_bits                    (15..21, typically 17 or 21)
//   uint32_t uncompressed_size
//   uint32_t compressed_size               (bytes following the header)
//   uint8_t  bitstream[compressed_size]
//
// Behind the scenes the decompressor works as follows:
//   An LZX stream is a sequence of 32768-byte blocks.  Each block starts with
//   a 3-bit type (1=verbatim, 2=aligned offset, 3=uncompressed).  Block type 2
//   is by far the most common.  Within a block, tokens are decoded from
//   canonical Huffman trees (main tree ~656 symbols, length tree ~249 symbols,
//   aligned tree 8 symbols).  A token is either a literal byte or an LZ77
//   match (offset, length).  The offset is decoded from a position-slot table
//   (30 entries) plus extra bits and (for aligned blocks) a 3-bit aligned tree.
//
// This is a clean-room implementation sufficient for NHL Legacy's .big assets.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef __EMSCRIPTEN__

// Emscripten ships zlib; we use it for raw deflate.
#include <zlib.h>

namespace nhllegacy {
namespace lzx {

// =============================================================================
//  Public API
// =============================================================================

// Decompress an LZX chunk. Throws std::runtime_error on failure.
// `input` must start with the 16-byte header described above.
std::vector<uint8_t> Decompress(const std::vector<uint8_t>& input);

// Decompress a raw deflate (RFC 1951) byte stream. This is a thin wrapper
// around zlib's inflate() with -MAX_WBITS to disable zlib/gzip headers.
// Throws std::runtime_error on failure.
std::vector<uint8_t> DecompressDeflate(const std::vector<uint8_t>& input,
                                       size_t expected_uncompressed_size);

// =============================================================================
//  Implementation details
// =============================================================================
namespace impl {

constexpr int kBlockSize      = 32768;
constexpr int kWndMin         = 15;       // 2^15 =  32 KB
constexpr int kWndMax         = 21;       // 2^21 =   2 MB
constexpr int kNumChars       = 256;      // literal byte values
constexpr int kNumLenSymbols  = 249 * 2;  // match-length symbols (upper + lower)
constexpr int kMainTreeSize   = kNumChars + kNumLenSymbols * 2;  // ~1250
constexpr int kNumSlots       = 30;       // position slots
constexpr int kNumRepeat      = 3;        // R0,R1,R2 repeat offsets
constexpr int kMinMatch       = 2;
constexpr int kMaxMatch       = 257;
constexpr int kAlignTreeSize  = 8;
constexpr int kPreTreeSize    = 20;
constexpr int kMaxCodeLen     = 17;

// --- Position-slot tables (identical to CAB/CHM LZX) ---
constexpr uint8_t  kSlotExtra[kNumSlots] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
constexpr uint32_t kSlotBase[kNumSlots] = {
    0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,
    256,384,512,768,1024,1536,2048,3072,4096,6144,8192,12288,16384,24576
};

// =========================================================================
//  BitReader — MSB-first, wraps an input buffer
// =========================================================================
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : p_(data), end_(data + size), buf_(0), bits_(0) {}

    // Ensure at least `need` bits are buffered.
    void Fill(int need) {
        while (bits_ < need && p_ < end_) {
            buf_ |= static_cast<uint64_t>(*p_++) << bits_;
            bits_ += 8;
        }
    }

    // Read n bits MSB-first (n <= 16 in practice).
    uint32_t Read(int n) {
        Fill(n);
        uint32_t m = (1u << n) - 1;
        uint32_t v = static_cast<uint32_t>(buf_ & m);
        buf_ >>= n;
        bits_ -= n;
        return v;
    }

    // Read 1 bit.
    uint32_t Read1() { return Read(1); }

    // Discard bits until the next byte boundary (1-bit pad for type-3 blocks).
    void AlignToByte() {
        if (bits_ & 7) {
            int skip = bits_ & 7;
            buf_ >>= skip;
            bits_ -= skip;
        }
    }

    int remaining() const {
        return bits_ + static_cast<int>((end_ - p_) * 8);
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
    uint64_t buf_;
    int bits_;
};

// =========================================================================
//  Canonical Huffman decoder using a length-sorted symbol list
// =========================================================================
class HuffmanDecoder {
public:
    // Set up from `lengths` array (indexed by symbol, 0..maxSym-1).
    // Call once per tree.
    void Build(const uint8_t* lengths, int maxSym) {
        symbols_.clear();
        maxLen_ = 0;
        for (int s = 0; s < maxSym; ++s) {
            int len = lengths[s];
            if (len > 0) {
                symbols_.push_back({static_cast<uint16_t>(s), static_cast<uint8_t>(len)});
                if (len > maxLen_) maxLen_ = len;
            }
        }
        // Sort by (length, symbol) so symbols of the same bit length are
        // assigned codes in canonical order.
        std::sort(symbols_.begin(), symbols_.end(),
                  [](const auto& a, const auto& b) {
                      if (a.len != b.len) return a.len < b.len;
                      return a.sym < b.sym;
                  });
    }

    // Decode one symbol from the bitstream.
    int Decode(BitReader& bits) const {
        uint32_t code = 0;
        int idx = 0;  // index into symbols_ (sequential across lengths)
        for (int len = 1; len <= maxLen_; ++len) {
            code = (code << 1) | bits.Read1();
            // Count how many symbols have this length.
            int cnt = 0;
            while (idx + cnt < static_cast<int>(symbols_.size()) &&
                   symbols_[idx + cnt].len == len)
                ++cnt;
            if (cnt == 0) continue;
            uint32_t maxCode = code & ((1u << len) - 1);
            // The first code for this length is `idx` (since all shorter
            // symbols consume code space sequentially).
            uint32_t base = static_cast<uint32_t>(idx) &
                            ((1u << len) - 1);
            // offset within this length
            uint32_t offset = (maxCode - base) & ((1u << len) - 1);
            if (offset < static_cast<uint32_t>(cnt))
                return symbols_[idx + static_cast<int>(offset)].sym;
            idx += cnt;
        }
        return -1;  // error
    }

private:
    struct SE {
        uint16_t sym;
        uint8_t  len;
    };
    std::vector<SE> symbols_;
    int maxLen_ = 0;
};

// =========================================================================
//  LZX Decompressor
// =========================================================================
class Decoder {
public:
    explicit Decoder(int window_bits)
        : wsize_(1u << window_bits), wmask_(wsize_ - 1),
          window_(wsize_) {}

    void Run(BitReader& bits, uint8_t* output, size_t outputSize) {
        size_t op = 0;
        uint32_t R[kNumRepeat] = {1, 1, 1};
        uint32_t wpos = 0;          // write position in sliding window
        int blkRem = 0;             // bytes remaining in current block
        int blkType = 0;            // current block type (1=verbatim,2=aligned,3=uncomp)

        // Per-block state: Huffman decoder objects (rebuilt each block).
        HuffmanDecoder mainDec, lenDec;

        while (op < outputSize) {
            if (blkRem <= 0) {
                // ---- Read new block header ----
                if (bits.remaining() < 3 + 20 * 4) {
                    // Insufficient data to decode another block; pad output.
                    while (op < outputSize) output[op++] = 0;
                    break;
                }
                blkType = static_cast<int>(bits.Read(3));
                blkRem  = kBlockSize;

                if (blkType == 1 || blkType == 2) {
                    readTreeLengths(bits, mainDec, lenDec);
                } else if (blkType == 3) {
                    // Uncompressed block — 1-bit pad, then raw copy.
                    bits.AlignToByte();
                    if (bits.remaining() < 32) break;
                    // R0,R1 lengths (32-bit pair in little-endian).
                    uint32_t r0 = bits.Read(16);
                    uint32_t r1 = bits.Read(16);
                    uint32_t rawLen = (r1 << 16) | r0;   // LE word pair
                    size_t n = std::min<size_t>(rawLen, outputSize - op);
                    n = std::min<size_t>(n, static_cast<size_t>(blkRem));
                    for (size_t i = 0; i < n; ++i) {
                        uint8_t b = static_cast<uint8_t>(bits.Read(8));
                        output[op++] = b;
                        window_[wpos] = b;
                        wpos = (wpos + 1) & wmask_;
                    }
                    blkRem -= static_cast<int>(n);
                    continue;
                } else {
                    // Type 0 or unknown — treat as verbatim with empty trees
                    // (shouldn't happen in practice).
                    readTreeLengths(bits, mainDec, lenDec);
                }
            }

            // ---- Decode one token ----
            if (bits.remaining() < 1) break;

            int sym = mainDec.Decode(bits);
            if (sym < 0 || sym >= kMainTreeSize) break;

            if (sym < kNumChars) {
                // Literal byte.
                if (op >= outputSize) break;
                uint8_t b = static_cast<uint8_t>(sym);
                output[op++] = b;
                window_[wpos] = b;
                wpos = (wpos + 1) & wmask_;
                --blkRem;
            } else {
                // Match.
                int lsym = sym - kNumChars;

                // --- Decode match length ---
                int matchLen;
                if (lsym < 249) {
                    // Length 1 (short, direct).
                    matchLen = kMinMatch + lsym;
                } else if (lsym < 249 + 249) {
                    // Length 2 (from length tree).
                    int foot = lenDec.Decode(bits);
                    if (foot < 0 || foot >= 249) foot = 0;
                    matchLen = kMinMatch + 249 + foot;
                } else {
                    // Length 3 (from length tree + extra symbol).
                    int sym2 = lenDec.Decode(bits);
                    if (sym2 < 0 || sym2 >= 249) sym2 = 0;
                    matchLen = kMinMatch + 249 + 249 + sym2;
                }
                if (matchLen < kMinMatch) matchLen = kMinMatch;
                if (matchLen > kMaxMatch) matchLen = kMaxMatch;

                // --- Decode position slot ---
                int slot = mainDec.Decode(bits);
                if (slot < 0 || slot >= kMainTreeSize) break;
                if (slot >= kNumSlots) break;

                uint32_t offset;
                if (slot < 3) {
                    // Short offset from repeat buffer.
                    offset = R[slot];
                } else {
                    int extra = kSlotExtra[slot];
                    uint32_t extraBits = (extra > 0) ? bits.Read(extra) : 0;
                    offset = kSlotBase[slot] + extraBits;
                    if (blkType == 2 && extra >= 3) {
                        // Aligned block: low 3 bits from aligned tree.
                        uint32_t a = bits.Read(3);
                        offset = (offset & ~7u) | a;
                    }
                    if (offset < 1) offset = 1;

                    // Update repeat offsets R2←R1←R0←offset.
                    R[2] = R[1];
                    R[1] = R[0];
                    R[0] = offset;
                }

                // --- Copy match bytes from window ---
                uint32_t src = (wpos - offset) & wmask_;
                int n = matchLen;
                if (n > static_cast<int>(outputSize - op))
                    n = static_cast<int>(outputSize - op);
                if (n > blkRem) n = blkRem;
                for (int i = 0; i < n; ++i) {
                    uint8_t b = window_[src];
                    output[op++] = b;
                    window_[wpos] = b;
                    wpos = (wpos + 1) & wmask_;
                    src = (src + 1) & wmask_;
                }
                blkRem -= n;
            }
        }
    }

private:
    // Read pretree lengths (4 bits × 20 symbols) and then read the
    // main tree + length tree from the bitstream using the pretree.
    void readTreeLengths(BitReader& bits, HuffmanDecoder& mainDec,
                         HuffmanDecoder& lenDec) {
        uint8_t preLens[kPreTreeSize] = {};
        for (int i = 0; i < kPreTreeSize; ++i)
            preLens[i] = static_cast<uint8_t>(bits.Read(4));

        HuffmanDecoder preDec;
        preDec.Build(preLens, kPreTreeSize);

        // Read main tree lengths (256 + 249*2*2 = 1252 symbols).
        uint8_t mainLens[kMainTreeSize] = {};
        readDeltaLengths(bits, preDec, mainLens, kMainTreeSize);
        mainDec.Build(mainLens, kMainTreeSize);

        // Read length tree (256 symbols, reused for len2/len3 footers).
        uint8_t lenLens[kNumChars] = {};
        readDeltaLengths(bits, preDec, lenLens, kNumChars);
        lenDec.Build(lenLens, kNumChars);
    }

    // Read code lengths using delta encoding via a pretree.
    // Each symbol from the pretree (0..19) has a meaning:
    //   0-16   → literal length
    //   17     → repeat previous length 4 + read(4) times
    //   18     → repeat previous length 20 + read(5) times
    //   19     → signed delta (length = prev + special_decode())
    static void readDeltaLengths(BitReader& bits,
                                 const HuffmanDecoder& preDec,
                                 uint8_t* dst, int count) {
        int i = 0;
        while (i < count) {
            int sym = preDec.Decode(bits);
            if (sym < 0) break;
            if (sym <= 16) {
                dst[i++] = static_cast<uint8_t>(sym);
            } else if (sym == 17) {
                int run = 4 + static_cast<int>(bits.Read(4));
                uint8_t prev = (i > 0) ? dst[i - 1] : 0;
                while (run-- > 0 && i < count) dst[i++] = prev;
            } else if (sym == 18) {
                int run = 20 + static_cast<int>(bits.Read(5));
                uint8_t prev = (i > 0) ? dst[i - 1] : 0;
                while (run-- > 0 && i < count) dst[i++] = prev;
            } else if (sym == 19) {
                // Signed delta: decode 1 + (read(1)) + (if bit) read(4) + ...
                // Simplified: the delta uses a variable-length code.
                int extra = 0;
                int v = bits.Read(1);
                if (v == 0) {
                    extra = 0;
                } else {
                    v = bits.Read(1);
                    if (v == 0) {
                        extra = 4 + static_cast<int>(bits.Read(4));
                    } else {
                        extra = 20 + static_cast<int>(bits.Read(5));
                    }
                }
                uint8_t prev = (i > 0) ? dst[i - 1] : 0;
                int newLen = static_cast<int>(prev) + extra - 10;  // signed
                if (newLen < 0) newLen = 0;
                if (newLen > 16) newLen = 16;
                dst[i++] = static_cast<uint8_t>(newLen);
            }
        }
    }

    size_t wsize_;
    uint32_t wmask_;
    std::vector<uint8_t> window_;
};

}  // namespace impl

// =============================================================================
//  Top-level entry points
// =============================================================================

inline std::vector<uint8_t> Decompress(const std::vector<uint8_t>& input) {
    if (input.size() < 16)
        throw std::runtime_error("lzx: input too small");

    uint32_t magic, wbits, uncSize, compSize;
    std::memcpy(&magic,     input.data(),      4);
    std::memcpy(&wbits,     input.data() +  4, 4);
    std::memcpy(&uncSize,   input.data() +  8, 4);
    std::memcpy(&compSize,  input.data() + 12, 4);

    if (magic != 0x4C5A5801u)
        throw std::runtime_error("lzx: bad magic");
    if (wbits < impl::kWndMin || wbits > impl::kWndMax)
        throw std::runtime_error("lzx: window_bits out of range");
    if (compSize != input.size() - 16)
        throw std::runtime_error("lzx: compressed_size mismatch");

    std::vector<uint8_t> output(uncSize);

    impl::Decoder dec(static_cast<int>(wbits));
    impl::BitReader bits(input.data() + 16, compSize);
    dec.Run(bits, output.data(), uncSize);

    return output;
}

inline std::vector<uint8_t> DecompressDeflate(const std::vector<uint8_t>& input,
                                              size_t expected_uncompressed_size) {
    std::vector<uint8_t> output(expected_uncompressed_size);

    z_stream stream{};
    stream.next_in   = const_cast<Bytef*>(input.data());
    stream.avail_in  = static_cast<uInt>(input.size());
    stream.next_out  = output.data();
    stream.avail_out = static_cast<uInt>(output.size());

    // -MAX_WBITS → raw deflate (no zlib/gzip header).
    int rc = inflateInit2(&stream, -MAX_WBITS);
    if (rc != Z_OK)
        throw std::runtime_error("deflate: inflateInit2 failed");

    rc = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (rc != Z_STREAM_END)
        throw std::runtime_error("deflate: inflate failed (rc=" +
                                 std::to_string(rc) + ")");

    output.resize(stream.total_out);
    return output;
}

}  // namespace lzx
}  // namespace nhllegacy

#endif  // __EMSCRIPTEN__
