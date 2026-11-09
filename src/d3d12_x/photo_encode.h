#pragma once
// Self-contained image encoders for the photo-mode export (no WIC: the game process loads the port's own
// combase.dll, so system codecs are avoided). RGB input, 8 bits per channel, rows packed (w*3 bytes).
//   PhotoEncodePng  - lossless PNG (per-row adaptive filter + deflate with fixed Huffman codes and LZ77)
//   PhotoEncodeJpg  - baseline JPEG, 4:4:4 (no chroma subsampling), standard Huffman tables
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <vector>

namespace photoenc {

// ---------------------------------------------------------------- PNG
static uint32_t Crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
  static uint32_t t[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t v = i; for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1; t[i] = v; } init = true; }
  c ^= 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

struct BitWriter {
  std::vector<uint8_t>& out; uint64_t acc = 0; int n = 0;
  explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
  void put(uint32_t bits, int cnt) { acc |= (uint64_t)bits << n; n += cnt; while (n >= 8) { out.push_back((uint8_t)acc); acc >>= 8; n -= 8; } }
  void flush() { if (n > 0) out.push_back((uint8_t)acc); acc = 0; n = 0; }
};
static inline uint32_t Rev(uint32_t code, int len) { uint32_t r = 0; for (int i = 0; i < len; ++i) { r = (r << 1) | (code & 1); code >>= 1; } return r; }
static inline void PutLit(BitWriter& bw, int sym) {
  if (sym < 144) bw.put(Rev(0x30 + sym, 8), 8);
  else if (sym < 256) bw.put(Rev(0x190 + sym - 144, 9), 9);
  else if (sym < 280) bw.put(Rev(sym - 256, 7), 7);
  else bw.put(Rev(0xC0 + sym - 280, 8), 8);
}
static void Deflate(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
  static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  static const uint8_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
  static const uint8_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
  const int HB = 15, WIN = 32768, CHAIN = 24;
  std::vector<int> head(1 << HB, -1), prev(WIN, -1);
  BitWriter bw(out);
  bw.put(1, 1); bw.put(1, 2);   // BFINAL=1, BTYPE=01 (fixed Huffman), single block
  auto hash = [&](size_t i) { return (int)(((d[i] << 10) ^ (d[i + 1] << 5) ^ d[i + 2]) & ((1 << HB) - 1)); };
  auto insert = [&](size_t i) { if (i + 2 < n) { int h = hash(i); prev[i & (WIN - 1)] = head[h]; head[h] = (int)i; } };
  size_t i = 0;
  while (i < n) {
    int bestLen = 0, bestDist = 0;
    if (i + 2 < n) {
      int cand = head[hash(i)], steps = 0; const int maxLen = (int)((n - i) < 258 ? (n - i) : 258);
      while (cand >= 0 && steps++ < CHAIN && (int)i - cand <= WIN - 1) {
        if (d[cand + bestLen] == d[i + bestLen]) {
          int l = 0; while (l < maxLen && d[cand + l] == d[i + l]) ++l;
          if (l > bestLen) { bestLen = l; bestDist = (int)i - cand; if (l == maxLen) break; }
        }
        int nx = prev[cand & (WIN - 1)]; if (nx >= cand) break; cand = nx;
      }
    }
    if (bestLen >= 3) {
      int li = 0; while (li < 28 && lbase[li + 1] <= bestLen) ++li;
      PutLit(bw, 257 + li); if (lext[li]) bw.put(bestLen - lbase[li], lext[li]);
      int di = 0; while (di < 29 && dbase[di + 1] <= bestDist) ++di;
      bw.put(Rev(di, 5), 5); if (dext[di]) bw.put(bestDist - dbase[di], dext[di]);
      for (int k = 0; k < bestLen; ++k) insert(i + k);
      i += bestLen;
    } else { PutLit(bw, d[i]); insert(i); ++i; }
  }
  PutLit(bw, 256); bw.flush();
}
static void PutBE32(std::vector<uint8_t>& o, uint32_t v) { o.push_back(v >> 24); o.push_back((v >> 16) & 0xFF); o.push_back((v >> 8) & 0xFF); o.push_back(v & 0xFF); }
static void Chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  PutBE32(o, (uint32_t)data.size()); size_t s = o.size(); o.insert(o.end(), type, type + 4); o.insert(o.end(), data.begin(), data.end());
  PutBE32(o, Crc32(o.data() + s, data.size() + 4));
}
static bool PhotoEncodePng(const uint8_t* rgb, int w, int h, std::vector<uint8_t>& png) {
  if (!rgb || w <= 0 || h <= 0) return false;
  const size_t stride = (size_t)w * 3;
  std::vector<uint8_t> raw; raw.resize((stride + 1) * h);
  std::vector<uint8_t> cand[5]; for (auto& c : cand) c.resize(stride);
  for (int y = 0; y < h; ++y) {
    const uint8_t* cur = rgb + y * stride; const uint8_t* up = y ? cur - stride : nullptr;
    size_t bestSum = (size_t)-1; int best = 0;
    for (int f = 0; f < 5; ++f) {
      size_t sum = 0;
      for (size_t x = 0; x < stride; ++x) {
        int a = x >= 3 ? cur[x - 3] : 0, b = up ? up[x] : 0, c = (up && x >= 3) ? up[x - 3] : 0, v;
        switch (f) {
          case 0: v = cur[x]; break; case 1: v = cur[x] - a; break; case 2: v = cur[x] - b; break;
          case 3: v = cur[x] - ((a + b) >> 1); break;
          default: { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); v = cur[x] - ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c)); }
        }
        uint8_t u = (uint8_t)v; cand[f][x] = u; sum += u < 128 ? u : 256 - u;
      }
      if (sum < bestSum) { bestSum = sum; best = f; }
    }
    uint8_t* dst = raw.data() + y * (stride + 1); dst[0] = (uint8_t)best; memcpy(dst + 1, cand[best].data(), stride);
  }
  std::vector<uint8_t> z; z.reserve(raw.size() / 2); z.push_back(0x78); z.push_back(0x01);
  Deflate(raw.data(), raw.size(), z);
  uint32_t a = 1, b = 0; for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  PutBE32(z, (b << 16) | a);
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  png.assign(sig, sig + 8);
  std::vector<uint8_t> ihdr; PutBE32(ihdr, w); PutBE32(ihdr, h); ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  Chunk(png, "IHDR", ihdr); Chunk(png, "IDAT", z); Chunk(png, "IEND", {});
  return true;
}

// ---------------------------------------------------------------- JPEG
static const uint8_t kZig[64] = {0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
                                 35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const uint8_t kDcLumBits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t kDcChrBits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t kDcVals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t kAcLumBits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t kAcLumVals[162] = {
  0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
  0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
  0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
  0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
  0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
  0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
  0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
static const uint8_t kAcChrBits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t kAcChrVals[162] = {
  0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
  0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
  0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
  0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
  0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
  0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
  0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
static const uint8_t kQLum[64] = {16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,
                                  18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
static const uint8_t kQChr[64] = {17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,
                                  99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
struct Huff { uint16_t code[256]; uint8_t len[256]; };
static void BuildHuff(const uint8_t* bits, const uint8_t* vals, Huff& h) {
  memset(&h, 0, sizeof(h)); int k = 0; uint16_t code = 0;
  for (int l = 1; l <= 16; ++l) { for (int i = 0; i < bits[l - 1]; ++i) { h.code[vals[k]] = code; h.len[vals[k]] = (uint8_t)l; ++k; ++code; } code <<= 1; }
}
struct JBits {
  std::vector<uint8_t>& o; uint32_t acc = 0; int n = 0;
  explicit JBits(std::vector<uint8_t>& out) : o(out) {}
  void put(uint32_t v, int cnt) {   // MSB-first, with 0xFF byte stuffing
    for (int i = cnt - 1; i >= 0; --i) { acc = (acc << 1) | ((v >> i) & 1); if (++n == 8) { o.push_back((uint8_t)acc); if (acc == 0xFF) o.push_back(0); acc = 0; n = 0; } }
  }
  void flush() { while (n) put(1, 1); }
};
static inline int BitLen(int v) { int a = v < 0 ? -v : v, n = 0; while (a) { ++n; a >>= 1; } return n; }
static void EncodeBlock(JBits& jb, const float* blk, const float* qdiv, int& prevDc, const Huff& dc, const Huff& ac) {
  static float cs[8][8]; static bool init = false;
  if (!init) { for (int u = 0; u < 8; ++u) for (int x = 0; x < 8; ++x) cs[u][x] = (u ? 0.5f : 0.35355339f) * cosf((2 * x + 1) * u * 3.14159265f / 16.0f); init = true; }
  float tmp[64], co[64];
  for (int y = 0; y < 8; ++y) for (int u = 0; u < 8; ++u) { float s = 0; for (int x = 0; x < 8; ++x) s += blk[y * 8 + x] * cs[u][x]; tmp[y * 8 + u] = s; }
  for (int u = 0; u < 8; ++u) for (int v = 0; v < 8; ++v) { float s = 0; for (int y = 0; y < 8; ++y) s += tmp[y * 8 + u] * cs[v][y]; co[v * 8 + u] = s; }
  int q[64];
  for (int k = 0; k < 64; ++k) { int idx = kZig[k]; float f = co[idx] / qdiv[idx]; q[k] = (int)(f < 0 ? f - 0.5f : f + 0.5f); }
  int diff = q[0] - prevDc; prevDc = q[0];
  int nb = BitLen(diff); jb.put(dc.code[nb], dc.len[nb]); if (nb) jb.put(diff < 0 ? (diff - 1) & ((1 << nb) - 1) : diff, nb);
  int run = 0;
  for (int k = 1; k < 64; ++k) {
    if (q[k] == 0) { ++run; continue; }
    while (run >= 16) { jb.put(ac.code[0xF0], ac.len[0xF0]); run -= 16; }
    int b = BitLen(q[k]), sym = (run << 4) | b; jb.put(ac.code[sym], ac.len[sym]);
    jb.put(q[k] < 0 ? (q[k] - 1) & ((1 << b) - 1) : q[k], b); run = 0;
  }
  if (run) jb.put(ac.code[0], ac.len[0]);
}
static bool PhotoEncodeJpg(const uint8_t* rgb, int w, int h, int quality, std::vector<uint8_t>& o) {
  if (!rgb || w <= 0 || h <= 0 || w > 65535 || h > 65535) return false;
  if (quality < 1) quality = 1; if (quality > 100) quality = 100;
  int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
  uint8_t ql[64], qc[64]; float dl[64], dcq[64];
  for (int i = 0; i < 64; ++i) {
    int a = (kQLum[i] * sc + 50) / 100, b = (kQChr[i] * sc + 50) / 100;
    ql[i] = (uint8_t)(a < 1 ? 1 : a > 255 ? 255 : a); qc[i] = (uint8_t)(b < 1 ? 1 : b > 255 ? 255 : b); dl[i] = ql[i]; dcq[i] = qc[i];
  }
  Huff hdl, hal, hdc, hac; BuildHuff(kDcLumBits, kDcVals, hdl); BuildHuff(kAcLumBits, kAcLumVals, hal); BuildHuff(kDcChrBits, kDcVals, hdc); BuildHuff(kAcChrBits, kAcChrVals, hac);
  o.clear();
  auto w16 = [&](int v) { o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); };
  o.push_back(0xFF); o.push_back(0xD8);
  static const uint8_t app0[] = {0xFF,0xE0,0,16,'J','F','I','F',0,1,1,0,0,1,0,1,0,0}; o.insert(o.end(), app0, app0 + sizeof(app0));
  o.push_back(0xFF); o.push_back(0xDB); w16(2 + 65 * 2);
  o.push_back(0); for (int k = 0; k < 64; ++k) o.push_back(ql[kZig[k]]);
  o.push_back(1); for (int k = 0; k < 64; ++k) o.push_back(qc[kZig[k]]);
  o.push_back(0xFF); o.push_back(0xC0); w16(17); o.push_back(8); w16(h); w16(w); o.push_back(3);
  o.push_back(1); o.push_back(0x11); o.push_back(0); o.push_back(2); o.push_back(0x11); o.push_back(1); o.push_back(3); o.push_back(0x11); o.push_back(1);
  auto dht = [&](int cls, const uint8_t* bits, const uint8_t* vals) { int n = 0; for (int i = 0; i < 16; ++i) n += bits[i]; o.push_back(0xFF); o.push_back(0xC4); w16(3 + 16 + n); o.push_back((uint8_t)cls); o.insert(o.end(), bits, bits + 16); o.insert(o.end(), vals, vals + n); };
  dht(0x00, kDcLumBits, kDcVals); dht(0x10, kAcLumBits, kAcLumVals); dht(0x01, kDcChrBits, kDcVals); dht(0x11, kAcChrBits, kAcChrVals);
  static const uint8_t sos[] = {0xFF,0xDA,0,12,3,1,0x00,2,0x11,3,0x11,0,63,0}; o.insert(o.end(), sos, sos + sizeof(sos));
  JBits jb(o); int pY = 0, pB = 0, pR = 0; float Y[64], Cb[64], Cr[64];
  for (int by = 0; by < h; by += 8) for (int bx = 0; bx < w; bx += 8) {
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
      int sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1;
      const uint8_t* p = rgb + ((size_t)sy * w + sx) * 3; float r = p[0], g = p[1], b = p[2];
      Y[y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
      Cb[y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
      Cr[y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
    }
    EncodeBlock(jb, Y, dl, pY, hdl, hal); EncodeBlock(jb, Cb, dcq, pB, hdc, hac); EncodeBlock(jb, Cr, dcq, pR, hdc, hac);
  }
  jb.flush(); o.push_back(0xFF); o.push_back(0xD9);
  return true;
}

}  // namespace photoenc
