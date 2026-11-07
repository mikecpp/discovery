#pragma once

#include <cstddef>

struct EmbeddedAsset {
    const char* path;
    const unsigned char* data;
    std::size_t size;
};

extern const EmbeddedAsset kEmbeddedAssets[];
extern const std::size_t kEmbeddedAssetCount;
