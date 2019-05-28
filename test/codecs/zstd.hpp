#pragma once
#include "codec.hpp"

struct Zstd : public CODEC8withPimpl { Zstd(int level = 1); };
