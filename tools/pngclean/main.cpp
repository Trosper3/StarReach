#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

struct Pixel { uint8_t r, g, b, a; };

// BFS flood-fill from every border pixel.
// Any pixel connected to the image edge whose RGB channels are all below
// `threshold` is considered background and made fully transparent.
// Dark pixels enclosed inside the main image are untouched.
static void removeBackground(std::vector<Pixel>& px, int w, int h, int threshold) {
    std::vector<bool> visited(w * h, false);
    std::queue<int> q;

    auto enqueue = [&](int x, int y) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        int idx = y * w + x;
        if (visited[idx]) return;
        const Pixel& p = px[idx];
        bool isBackground = (p.a == 0) ||
                            (p.r < threshold && p.g < threshold && p.b < threshold);
        if (isBackground) {
            visited[idx] = true;
            q.push(idx);
        }
    };

    for (int x = 0; x < w; x++) { enqueue(x, 0); enqueue(x, h - 1); }
    for (int y = 0; y < h; y++) { enqueue(0, y); enqueue(w - 1, y); }

    while (!q.empty()) {
        int idx = q.front(); q.pop();
        px[idx] = { 0, 0, 0, 0 };
        int x = idx % w, y = idx / w;
        enqueue(x + 1, y);
        enqueue(x - 1, y);
        enqueue(x, y + 1);
        enqueue(x, y - 1);
    }
}

// Finds all connected components of non-transparent pixels, then removes any
// component whose center sits in the bottom-right corner region. The main
// subject (largest component) is always kept regardless of position.
static void removeCornerIcons(std::vector<Pixel>& px, int w, int h, float cornerFraction) {
    std::vector<int> labels(w * h, -1);

    struct Component {
        std::vector<int> pixels;
        int sumX = 0, sumY = 0;
    };
    std::vector<Component> components;

    for (int i = 0; i < w * h; i++) {
        if (px[i].a == 0 || labels[i] != -1) continue;

        Component comp;
        std::queue<int> q;
        int label = (int)components.size();
        labels[i] = label;
        q.push(i);

        while (!q.empty()) {
            int idx = q.front(); q.pop();
            comp.pixels.push_back(idx);
            int x = idx % w, y = idx / w;
            comp.sumX += x;
            comp.sumY += y;

            auto tryPush = [&](int nx, int ny) {
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) return;
                int nidx = ny * w + nx;
                if (px[nidx].a == 0 || labels[nidx] != -1) return;
                labels[nidx] = label;
                q.push(nidx);
            };
            tryPush(x + 1, y); tryPush(x - 1, y);
            tryPush(x, y + 1); tryPush(x, y - 1);
        }

        components.push_back(std::move(comp));
    }

    if (components.empty()) return;

    // Largest component = main subject, always preserved
    int mainIdx = 0;
    for (int i = 1; i < (int)components.size(); i++)
        if (components[i].pixels.size() > components[mainIdx].pixels.size())
            mainIdx = i;

    int cornerX = (int)(w * (1.0f - cornerFraction));
    int cornerY = (int)(h * (1.0f - cornerFraction));

    int removed = 0;
    for (int i = 0; i < (int)components.size(); i++) {
        if (i == mainIdx) continue;
        const Component& c = components[i];

        // Use centroid to decide if it's in the corner region
        int cx = c.sumX / (int)c.pixels.size();
        int cy = c.sumY / (int)c.pixels.size();

        if (cx >= cornerX && cy >= cornerY) {
            for (int idx : c.pixels)
                px[idx] = { 0, 0, 0, 0 };
            removed++;
            std::cout << "  Removed corner icon: " << c.pixels.size()
                      << " pixels (centroid " << cx << "," << cy << ")\n";
        }
    }

    if (removed == 0)
        std::cout << "  No corner icons found.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: pngclean <input.png> <output.png> [options]\n"
                  << "\n"
                  << "Options:\n"
                  << "  --threshold <n>   RGB cutoff for background black (default 30).\n"
                  << "                    Raise if faint dark fringe remains.\n"
                  << "  --corner <f>      Fraction of image to treat as corner region\n"
                  << "                    (default 0.25 = bottom-right 25%x25%).\n"
                  << "  --no-corner       Skip corner icon removal.\n";
        return 1;
    }

    const char* inputPath  = argv[1];
    const char* outputPath = argv[2];

    int   threshold      = 30;
    float cornerFraction = 0.25f;
    bool  doCorner       = true;

    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--threshold" && i + 1 < argc)
            threshold = std::atoi(argv[++i]);
        else if (arg == "--corner" && i + 1 < argc)
            cornerFraction = (float)std::atof(argv[++i]);
        else if (arg == "--no-corner")
            doCorner = false;
    }

    int w, h, ch;
    uint8_t* data = stbi_load(inputPath, &w, &h, &ch, 4);
    if (!data) {
        std::cerr << "Failed to load: " << inputPath << "\n";
        return 1;
    }

    std::vector<Pixel> pixels(w * h);
    for (int i = 0; i < w * h; i++)
        pixels[i] = { data[i*4], data[i*4+1], data[i*4+2], data[i*4+3] };
    stbi_image_free(data);

    std::cout << "Removing background (threshold=" << threshold << ")...\n";
    removeBackground(pixels, w, h, threshold);

    if (doCorner) {
        std::cout << "Checking bottom-right corner (fraction=" << cornerFraction << ")...\n";
        removeCornerIcons(pixels, w, h, cornerFraction);
    }

    int result = stbi_write_png(outputPath, w, h, 4,
                                reinterpret_cast<uint8_t*>(pixels.data()),
                                w * 4);
    if (!result) {
        std::cerr << "Failed to write: " << outputPath << "\n";
        return 1;
    }

    std::cout << "Saved " << w << "x" << h << " PNG to: " << outputPath << "\n";
    return 0;
}
