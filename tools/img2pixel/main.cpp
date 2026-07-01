#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <vector>

struct Pixel { uint8_t r, g, b, a; };

static std::string toHex(const Pixel& p) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X", p.r, p.g, p.b, p.a);
    return std::string(buf);
}

static float colorDist(const Pixel& a, const Pixel& b) {
    float dr = (float)a.r - b.r;
    float dg = (float)a.g - b.g;
    float db = (float)a.b - b.b;
    return sqrtf(dr*dr + dg*dg + db*db);
}

// BFS flood-fill from every border pixel to mark connected dark/transparent pixels as transparent.
// "Dark" = max(R,G,B) < threshold — catches pure black and near-black backgrounds while leaving
// dark ship details (engine housings, panel lines) that aren't connected to the image edge.
static void removeBackground(std::vector<Pixel>& px, int w, int h, int threshold = 30) {
    std::vector<bool> visited(w * h, false);
    std::queue<int> q;

    auto enqueue = [&](int x, int y) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        int idx = y * w + x;
        if (visited[idx]) return;
        const Pixel& p = px[idx];
        bool dark = p.r < threshold && p.g < threshold && p.b < threshold;
        if (p.a == 0 || dark) {
            visited[idx] = true;
            q.push(idx);
        }
    };

    for (int x = 0; x < w; x++) { enqueue(x, 0); enqueue(x, h - 1); }
    for (int y = 0; y < h; y++) { enqueue(0, y); enqueue(w - 1, y); }

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        px[idx].a = 0;
        int x = idx % w, y = idx / w;
        enqueue(x + 1, y); enqueue(x - 1, y);
        enqueue(x, y + 1); enqueue(x, y - 1);
    }
}

// Nearest-neighbor downsample — preserves pixel art crispness.
static std::vector<Pixel> downsample(const std::vector<Pixel>& src, int sw, int sh, int dw, int dh) {
    std::vector<Pixel> dst(dw * dh);
    for (int dy = 0; dy < dh; dy++)
        for (int dx = 0; dx < dw; dx++)
            dst[dy * dw + dx] = src[(dy * sh / dh) * sw + (dx * sw / dw)];
    return dst;
}

struct Cluster { uint64_t rS = 0, gS = 0, bS = 0; int n = 0; };

// Quantized histogram clustering: 4 bits per channel → 4096 buckets.
// Returns {primary, secondary} color centroids.
// Near-black pixels (all channels < 40) are excluded so panel lines don't win.
static std::pair<Pixel, Pixel> findPaletteColors(const std::vector<Pixel>& grid) {
    std::map<uint32_t, Cluster> buckets;

    for (const auto& p : grid) {
        if (p.a < 128) continue;
        if (p.r < 40 && p.g < 40 && p.b < 40) continue;
        uint32_t key = ((uint32_t)(p.r >> 4) << 8) | ((uint32_t)(p.g >> 4) << 4) | (p.b >> 4);
        auto& c = buckets[key];
        c.rS += p.r; c.gS += p.g; c.bS += p.b; c.n++;
    }

    std::vector<std::pair<int, Pixel>> sorted;
    sorted.reserve(buckets.size());
    for (auto& [key, c] : buckets) {
        Pixel cent{ (uint8_t)(c.rS / c.n), (uint8_t)(c.gS / c.n), (uint8_t)(c.bS / c.n), 255 };
        sorted.push_back({ c.n, cent });
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){ return a.first > b.first; });

    if (sorted.empty()) return { {200,200,200,255}, {100,50,50,255} };

    Pixel primary = sorted[0].second;
    Pixel secondary{ 100, 100, 100, 255 };

    // Secondary must be sufficiently distinct from primary (>80 units in RGB space).
    for (size_t i = 1; i < sorted.size(); i++) {
        if (colorDist(sorted[i].second, primary) >= 80.0f) {
            secondary = sorted[i].second;
            break;
        }
    }

    return { primary, secondary };
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: img2pixel <image> <width> <height> [--mode raw|palette] [--out <file>]\n"
                  << "  raw     (default) every pixel is RRGGBBAA hex\n"
                  << "  palette primary pixels become \"##\", secondary become \"**\"\n"
                  << "  --out   write output to <file> instead of stdout\n";
        return 1;
    }

    const char* path = argv[1];
    int dstW = std::atoi(argv[2]);
    int dstH = std::atoi(argv[3]);

    if (dstW <= 0 || dstH <= 0) {
        std::cerr << "Width and height must be positive integers.\n";
        return 1;
    }

    std::string mode    = "raw";
    std::string outPath = "";
    for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc)
            mode = argv[++i];
        else if (arg == "--out" && i + 1 < argc)
            outPath = argv[++i];
    }

    if (mode != "raw" && mode != "palette") {
        std::cerr << "Invalid mode \"" << mode << "\". Use raw or palette.\n";
        return 1;
    }

    std::ofstream fileOut;
    if (!outPath.empty()) {
        fileOut.open(outPath);
        if (!fileOut.is_open()) {
            std::cerr << "Failed to open output file: " << outPath << "\n";
            return 1;
        }
    }
    std::ostream& out = outPath.empty() ? std::cout : fileOut;

    int srcW, srcH, ch;
    uint8_t* data = stbi_load(path, &srcW, &srcH, &ch, 4);
    if (!data) {
        std::cerr << "Failed to load: " << path << "\n";
        return 1;
    }

    std::vector<Pixel> pixels(srcW * srcH);
    for (int i = 0; i < srcW * srcH; i++)
        pixels[i] = { data[i*4], data[i*4+1], data[i*4+2], data[i*4+3] };
    stbi_image_free(data);

    removeBackground(pixels, srcW, srcH);
    std::vector<Pixel> grid = downsample(pixels, srcW, srcH, dstW, dstH);

    if (mode == "raw") {
        out << "[\n";
        for (int y = 0; y < dstH; y++) {
            out << "  [";
            for (int x = 0; x < dstW; x++) {
                out << "\"" << toHex(grid[y * dstW + x]) << "\"";
                if (x < dstW - 1) out << ",";
            }
            out << "]";
            if (y < dstH - 1) out << ",";
            out << "\n";
        }
        out << "]\n";
    } else {
        auto [primary, secondary] = findPaletteColors(grid);
        const float TOL = 60.0f;

        out << "{\n"
            << "  \"primaryColor\": \"" << toHex(primary) << "\",\n"
            << "  \"secondaryColor\": \"" << toHex(secondary) << "\",\n"
            << "  \"pixelGrid\": [\n";

        for (int y = 0; y < dstH; y++) {
            out << "    [";
            for (int x = 0; x < dstW; x++) {
                const Pixel& p = grid[y * dstW + x];
                std::string val;
                if (p.a < 128)
                    val = "\"00000000\"";
                else if (colorDist(p, primary) < TOL)
                    val = "\"#\"";
                else if (colorDist(p, secondary) < TOL)
                    val = "\"+\"";
                else
                    val = "\"" + toHex(p) + "\"";
                out << val;
                if (x < dstW - 1) out << ",";
            }
            out << "]";
            if (y < dstH - 1) out << ",";
            out << "\n";
        }

        out << "  ]\n}\n";
    }

    return 0;
}
