#pragma once
#include <string>
#include "raylib.h"

struct MatDef {
    const char* id;
    const char* displayName;
    Color       hudColor;
    int         sellValue = 5;
};

inline const MatDef* AllMaterials(int* countOut = nullptr) {
    static const MatDef kAll[] = {
        { "iron",      "Iron",      { 170, 158, 144, 255 },  2 },
        { "carbon",    "Carbon",    {  72,  72,  72, 255 },  2 },
        { "silica",    "Silica",    { 205, 192, 162, 255 },  3 },
        { "titanium",  "Titanium",  { 148, 168, 192, 255 },  4 },
        { "cobalt",    "Cobalt",    {  80, 130, 210, 255 },  5 },
        { "tungsten",  "Tungsten",  { 126, 126, 140, 255 },  6 },
        { "crystite",  "Crystite",  {  56, 210, 196, 255 },  8 },
        { "xenonite",  "Xenonite",  { 170,  82, 228, 255 }, 12 },
        { "voidstone", "Voidstone", {  72,  26,  96, 255 }, 15 },
    };
    if (countOut) *countOut = 9;
    return kAll;
}

inline const MatDef* FindMaterial(const std::string& id) {
    int count;
    const MatDef* all = AllMaterials(&count);
    for (int i = 0; i < count; ++i)
        if (id == all[i].id) return &all[i];
    return nullptr;
}
