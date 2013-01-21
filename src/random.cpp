/*
    Copyright (C) 2012-2013 by Michael Kristofik <kristo605@gmail.com>
    Part of the libsdl-demos project.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    or at your option any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY.
 
    See the COPYING.txt file for more details.
*/
#include "hex_utils.h"
#include "sdl_helper.h"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

// Experiment with Apps Hungarian notation:
// a = hex number as an array index
// h = hex coordinate
// p = pixel coordinate
// r = region number

const Sint16 hexSize = 72;
const Sint16 hMapWidth = 16;
const Sint16 hMapHeight = 9;
const Sint16 hMapSize = hMapWidth * hMapHeight;
const int numRegions = 18;

// Not using enum class because doing math on terrain types is very convenient.
enum Terrain {GRASS, DIRT, SAND, WATER, SWAMP, SNOW, NUM_TERRAINS};

// Return the closest region to the given hex (hx,hy).
int rFindClosest(Sint16 hx, Sint16 hy, const std::vector<Point> &hCenters)
{
    int rClosest = -1;
    Sint16 bestSoFar = std::numeric_limits<Sint16>::max();

    for (int i = 0; i < numRegions; ++i) {
        Sint16 dist = hexDist(Point(hx, hy), hCenters[i]);
        if (dist < bestSoFar) {
            rClosest = i;
            bestSoFar = dist;
        }
    }

    return rClosest;
}

// Compute the centers of mass of each region.
std::vector<Point> hGetCenters(const std::vector<int> &regions)
{
    std::vector<Point> hexSums(numRegions);
    std::vector<int> numHexes(numRegions);
    std::vector<Point> hCenters(numRegions, hInvalid);

    assert(regions.size() == unsigned(hMapSize));
    for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            int region = regions[hy * hMapWidth + hx];
            assert(region >= 0 && region < numRegions);

            auto &hs = hexSums[region];
            hs.first += hx;
            hs.second += hy;
            ++numHexes[region];
        }
    }

    for (int r = 0; r < numRegions; ++r) {
        // The voronoi algorithm sometimes leads to regions being "absorbed" by
        // their neighbors, leaving no hexes left.  Leave the default (invalid)
        // center hex in place for such a region.
        if (numHexes[r] > 0) {
            auto &hc = hCenters[r];
            auto &hs = hexSums[r];
            hc.first = hs.first / numHexes[r];
            hc.second = hs.second / numHexes[r];
        }
    }

    return hCenters;
}

// Use a voronoi diagram to generate a random set of regions.
std::vector<int> generateRegions()
{
    // Start with a set of random center points.  Don't worry if there are
    // duplicates.
    HexGrid grid(hMapWidth, hMapHeight);
    std::vector<Point> hCenters;
    for (int r = 0; r < numRegions; ++r) {
        hCenters.push_back(grid.hexRandom());
    }

    std::vector<int> regions(hMapSize);
    for (int i = 0; i < 4; ++i) {
        // Find the closest center to each hex on the map, number those
        // regions.
        for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
            for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
                regions[hy * hMapWidth + hx] = rFindClosest(hx, hy, hCenters);
            }
        }

        hCenters = hGetCenters(regions);
        // Repeat this process to make more regular-looking regions.
    }

    // Assign each hex to its final region.
    for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            regions[hy * hMapWidth + hx] = rFindClosest(hx, hy, hCenters);
        }
    }

    return regions;
}

// Construct an adjacency list for each region.
std::vector<std::vector<int>> regionNeighbors(const std::vector<int> &regions)
{
    assert(regions.size() == unsigned(hMapSize));

    HexGrid grid(hMapWidth, hMapHeight);
    std::vector<std::vector<int>> ret(numRegions);
    for (int i = 0; i < hMapSize; ++i) {
        int reg = regions[i];
        assert(reg >= 0 && reg < numRegions);

        for (const auto &an : grid.aryNeighbors(i)) {
            int rNeighbor = regions[an];
            // If an adjacent hex is in a different region and we haven't
            // already recorded that region as a neighbor, save it.
            if (rNeighbor != reg && find(std::begin(ret[reg]),
                                         std::end(ret[reg]),
                                         rNeighbor) == std::end(ret[reg])) {
                ret[reg].push_back(rNeighbor);
            }
        }
    }

    return ret;
}

// Assign a terrain type to each region using the given adjacency list.
std::vector<int> assignRegionTerrains(const std::vector<std::vector<int>> &adj)
{
    std::vector<int> terrain(numRegions, -1);

    // Greedy coloring.  Each region gets a different terrain from its
    // neighbors, using the lowest number available.
    for (int r = 0; r < numRegions; ++r) {
        // For each neighboring region, save which terrains have been assigned.
        std::bitset<NUM_TERRAINS> assignedTerrains;
        for (auto rNeighbor : adj[r]) {
            assert(rNeighbor >= 0 && rNeighbor < numRegions);
            if (terrain[rNeighbor] > -1) {
                assignedTerrains[terrain[rNeighbor]] = true;
            }
        }
        if (!assignedTerrains.all()) {
            for (int t = 0; t < NUM_TERRAINS; ++t) {
                if (!assignedTerrains[t]) {
                    terrain[r] = t;
                    break;
                }
            }
        }
        else {
            terrain[r] = 0;
        }
    }

    return terrain;
}

// Note: doesn't do bounds checking so we can overdraw the map edges.
void sdlBlitAt(const SdlSurface &surf, Sint16 hx, Sint16 hy)
{
    if (hx % 2 == 0) {
        sdlBlit(surf, hx * hexSize * 0.75, hy * hexSize);
    }
    else {
        sdlBlit(surf, hx * hexSize * 0.75, (hy + 0.5) * hexSize);
    }
}

// This one does do bounds checking because it doesn't make sense to have an
// out-of-range array index.
void sdlBlitAt(const SdlSurface &surf, int aIndex)
{
    assert(aIndex >= 0 && aIndex < hMapSize);
    HexGrid grid(hMapWidth, hMapHeight);
    auto hex = grid.hexFromAry(aIndex);
    sdlBlitAt(surf, hex.first, hex.second);
}

int getEdge(int terrainFrom, int terrainTo)
{
    if ((terrainFrom == WATER && terrainTo != WATER) ||
        (terrainFrom != WATER && terrainTo == WATER) ||
        (terrainFrom == SAND && terrainTo != SAND) ||
        (terrainFrom != SAND && terrainTo == SAND)) {
        return SAND;
    }
    else if ((terrainFrom == DIRT && terrainTo == GRASS) ||
             (terrainFrom == GRASS && terrainTo == DIRT)) {
        return GRASS;
    }
    else if (terrainFrom != terrainTo) {
        return DIRT;
    }

    return -1;
}

extern "C" int SDL_main(int, char **)  // 2-arg form is required by SDL
{
    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        std::cerr << "Error initializing SDL: " << SDL_GetError();
        return EXIT_FAILURE;
    }
    atexit(SDL_Quit);

    if (IMG_Init(IMG_INIT_PNG) < 0) {
        std::cerr << "Error initializing SDL_image: " << IMG_GetError();
        return EXIT_FAILURE;
    }
    atexit(IMG_Quit);

    // Have to do this prior to SetVideoMode.
    auto icon = make_surface(IMG_Load("../img/icon.png"));
    if (icon != nullptr) {
        SDL_WM_SetIcon(icon.get(), nullptr);
    }
    else {
        std::cerr << "Warning: error loading icon file: " << IMG_GetError();
    }

    screen = SDL_SetVideoMode(882, 684, 0, SDL_SWSURFACE);
    if (screen == nullptr) {
        std::cerr << "Error setting video mode: " << SDL_GetError();
        return EXIT_FAILURE;    
    }
    SDL_WM_SetCaption("Random Map Test", "");

    std::vector<SdlSurface> tiles;
    tiles.push_back(sdlLoadImage("../img/grass.png"));
    tiles.push_back(sdlLoadImage("../img/dirt.png"));
    tiles.push_back(sdlLoadImage("../img/desert.png"));
    tiles.push_back(sdlLoadImage("../img/water.png"));
    tiles.push_back(sdlLoadImage("../img/swamp.png"));
    tiles.push_back(sdlLoadImage("../img/snow.png"));

    std::vector<SdlSurface> edges;
    edges.push_back(sdlLoadImage("../img/grass-n.png"));
    edges.push_back(sdlLoadImage("../img/grass-ne.png"));
    edges.push_back(sdlLoadImage("../img/grass-se.png"));
    edges.push_back(sdlLoadImage("../img/grass-s.png"));
    edges.push_back(sdlLoadImage("../img/grass-sw.png"));
    edges.push_back(sdlLoadImage("../img/grass-nw.png"));
    edges.push_back(sdlLoadImage("../img/dirt-n.png"));
    edges.push_back(sdlLoadImage("../img/dirt-ne.png"));
    edges.push_back(sdlLoadImage("../img/dirt-se.png"));
    edges.push_back(sdlLoadImage("../img/dirt-s.png"));
    edges.push_back(sdlLoadImage("../img/dirt-sw.png"));
    edges.push_back(sdlLoadImage("../img/dirt-nw.png"));
    edges.push_back(sdlLoadImage("../img/beach-n.png"));
    edges.push_back(sdlLoadImage("../img/beach-ne.png"));
    edges.push_back(sdlLoadImage("../img/beach-se.png"));
    edges.push_back(sdlLoadImage("../img/beach-s.png"));
    edges.push_back(sdlLoadImage("../img/beach-sw.png"));
    edges.push_back(sdlLoadImage("../img/beach-nw.png"));

    auto regions = generateRegions();
    auto adjacencyList = regionNeighbors(regions);
    auto regionTerrain = assignRegionTerrains(adjacencyList);

    // Assign terrain to each hex.
    std::vector<int> terrain(hMapSize, -1);
    for (auto i = 0u; i < regions.size(); ++i) {
        terrain[i] = regionTerrain[regions[i]];
    }

    // Draw the map.
    HexGrid grid(hMapWidth, hMapHeight);
    for (Sint16 hx = 0; hx < hMapWidth; ++hx) {
        for (Sint16 hy = 0; hy < hMapHeight; ++hy) {
            auto aPos = grid.aryFromHex(hx, hy);
            auto terrainIndex = terrain[aPos];
            sdlBlitAt(tiles[terrainIndex], hx, hy);
            for (auto dir : Dir()) {
                auto aNeighbor = grid.aryGetNeighbor(aPos, dir);
                if (aNeighbor == -1) continue;
                auto edgeTerrain = getEdge(terrainIndex, terrain[aNeighbor]);
                if (edgeTerrain >= 0) {
                    int edgeIndex = edgeTerrain * 6 + int(dir);
                    sdlBlitAt(edges[edgeIndex], hx, hy);
                }
            }
        }
    }

    // Overdraw so we don't get jagged edges, copying from neighboring hexes.

    // left edge
    for (Sint16 hy = -1; hy < hMapHeight; ++hy) {
        auto aMirror = grid.aryFromHex(0, std::min(hy + 1, hMapHeight - 1));
        auto terrainIndex = terrain[aMirror];
        sdlBlitAt(tiles[terrainIndex], -1, hy);
        // Mirrored hex is to the southeast, so we compute the edge against its
        // north neighbor.
        //  /N\   N = neighbor
        // O\_/   O = overdraw area
        //  /M\   M = mirrored hex
        //  \_/
        auto aNeighbor = grid.aryGetNeighbor(aMirror, Dir::N);
        if (aNeighbor == -1) continue;
        auto edgeTerrain1 = getEdge(terrainIndex, terrain[aNeighbor]);
        if (edgeTerrain1 >= 0) {
            int edgeIndex = edgeTerrain1 * 6 + int(Dir::NE);
            sdlBlitAt(edges[edgeIndex], -1, hy);
        }
        // Might have to draw the edge the other way too.
        auto edgeTerrain2 = getEdge(terrain[aNeighbor], terrainIndex);
        if (edgeTerrain2 >= 0) {
            int edgeIndex = edgeTerrain2 * 6 + int(Dir::SW);
            sdlBlitAt(edges[edgeIndex], aNeighbor);
        }
    }
    // top edge
    for (Sint16 hx = 1; hx < hMapWidth; hx += 2) {
        auto aMirror = grid.aryFromHex(hx, 0);
        auto terrainIndex = terrain[aMirror];
        sdlBlitAt(tiles[terrainIndex], hx, -1);
        // Mirrored hex is to the south, so we compute the edge against its
        // northwest and northeast neighbors.
        //  _ O _
        // /N\_/N\   N = neighbors
        // \_/M\_/   O = overdraw area
        //   \_/     M = mirrored hex
        auto aNeighbor1 = grid.aryGetNeighbor(aMirror, Dir::NW);
        if (aNeighbor1 != -1) {
            auto edgeTerrain1 = getEdge(terrainIndex, terrain[aNeighbor1]);
            if (edgeTerrain1 >= 0) {
                int edgeIndex1 = edgeTerrain1 * 6 + int(Dir::SW);
                sdlBlitAt(edges[edgeIndex1], hx, -1);
            }
            auto edgeTerrain2 = getEdge(terrain[aNeighbor1], terrainIndex);
            if (edgeTerrain2 >= 0) {
                int edgeIndex2 = edgeTerrain2 * 6 + int(Dir::NE);
                sdlBlitAt(edges[edgeIndex2], aNeighbor1);
            }
        }
        auto aNeighbor2 = grid.aryGetNeighbor(aMirror, Dir::NE);
        if (aNeighbor2 != -1) {
            auto edgeTerrain3 = getEdge(terrainIndex, terrain[aNeighbor2]);
            if (edgeTerrain3 >= 0) {
                int edgeIndex3 = edgeTerrain3 * 6 + int(Dir::SE);
                sdlBlitAt(edges[edgeIndex3], hx, -1);
            }
            auto edgeTerrain4 = getEdge(terrain[aNeighbor2], terrainIndex);
            if (edgeTerrain4 >= 0) {
                int edgeIndex4 = edgeTerrain4 * 6 + int(Dir::NW);
                sdlBlitAt(edges[edgeIndex4], aNeighbor2);
            }
        }
    }
    // right edge
    for (Sint16 hy = 0; hy < hMapHeight + 1; ++hy) {
        auto aMirror = grid.aryFromHex(hMapWidth - 1,
                                       std::min<int>(hy, hMapHeight - 1));
        auto terrainIndex = terrain[aMirror];
        sdlBlitAt(tiles[terrainIndex], hMapWidth, hy);
        // Mirrored hex is to the southwest, so we compute the edge against its
        // north neighbor.
        //  _
        // /N\    N = neighbor
        // \_/O   O = overdraw area
        // /M\    M = mirrored hex
        // \_/
        auto aNeighbor = grid.aryGetNeighbor(aMirror, Dir::N);
        if (aNeighbor == -1) continue;
        auto edgeTerrain1 = getEdge(terrainIndex, terrain[aNeighbor]);
        if (edgeTerrain1 >= 0) {
            int edgeIndex = edgeTerrain1 * 6 + int(Dir::NW);
            sdlBlitAt(edges[edgeIndex], hMapWidth, hy);
        }
        auto edgeTerrain2 = getEdge(terrain[aNeighbor], terrainIndex);
        if (edgeTerrain2 >= 0) {
            int edgeIndex = edgeTerrain2 * 6 + int(Dir::SE);
            sdlBlitAt(edges[edgeIndex], aNeighbor);
        }
    }
    // bottom edge
    for (Sint16 hx = 0; hx < hMapWidth; hx += 2) {
        auto aMirror = grid.aryFromHex(hx, hMapHeight - 1);
        auto terrainIndex = terrain[aMirror];
        sdlBlitAt(tiles[terrainIndex], hx, hMapHeight);
        // Mirrored hex is to the north, so we compute the edge against its
        // southwest and southeast neighbors.
        //    _
        //  _/M\_    N = neighbors
        // /N\_/N\   O = overdraw area
        // \_/O\_/   M = mirrored hex
        auto aNeighbor1 = grid.aryGetNeighbor(aMirror, Dir::SW);
        if (aNeighbor1 != -1) {
            auto edgeTerrain1 = getEdge(terrainIndex, terrain[aNeighbor1]);
            if (edgeTerrain1 >= 0) {
                int edgeIndex1 = edgeTerrain1 * 6 + int(Dir::NW);
                sdlBlitAt(edges[edgeIndex1], hx, hMapHeight);
            }
            auto edgeTerrain2 = getEdge(terrain[aNeighbor1], terrainIndex);
            if (edgeTerrain2 >= 0) {
                int edgeIndex2 = edgeTerrain2 * 6 + int(Dir::SE);
                sdlBlitAt(edges[edgeIndex2], aNeighbor1);
            }
        }
        auto aNeighbor2 = grid.aryGetNeighbor(aMirror, Dir::SE);
        if (aNeighbor2 != -1) {
            auto edgeTerrain3 = getEdge(terrainIndex, terrain[aNeighbor2]);
            if (edgeTerrain3 >= 0) {
                int edgeIndex3 = edgeTerrain3 * 6 + int(Dir::NE);
                sdlBlitAt(edges[edgeIndex3], hx, hMapHeight);
            }
            auto edgeTerrain4 = getEdge(terrain[aNeighbor2], terrainIndex);
            if (edgeTerrain4 >= 0) {
                int edgeIndex4 = edgeTerrain4 * 6 + int(Dir::SW);
                sdlBlitAt(edges[edgeIndex4], aNeighbor2);
            }
        }
    }

    SDL_UpdateRect(screen, 0, 0, 0, 0);

    bool isDone = false;
    SDL_Event event;
    while (!isDone) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                isDone = true;
            }
        }
        SDL_Delay(1);
    }

    // TODO: unit tests?
    int count = 0;
    for (const auto &hNeighbors : adjacencyList) {
        std::cout << count << ": ";
        for_each(std::begin(hNeighbors), std::end(hNeighbors),
                 [] (int hn) { std::cout << hn << ','; });
        std::cout << '\n';
        ++count;
    }

    // Check that we always draw an edge between two different terrains and
    // that we never draw an edge between two terrains that are the same.
    for (int i = 0; i < NUM_TERRAINS; ++i) {
        for (int j = 0; j < NUM_TERRAINS; ++j) {
            if (i == j) {
                assert(getEdge(i, j) == -1);
            }
            else {
                auto edge = getEdge(i, j);
                assert(edge >= 0 && edge < NUM_TERRAINS);
            }
        }
    }

    return EXIT_SUCCESS;
}
