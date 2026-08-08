#ifndef ISLAND_ICON_PIPELINE_SHA256_H_
#define ISLAND_ICON_PIPELINE_SHA256_H_

#include <array>
#include <cstddef>
#include <span>
#include <string>

std::string Sha256(std::span<const unsigned char> bytes);

#endif
