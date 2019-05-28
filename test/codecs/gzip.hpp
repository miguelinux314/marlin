#pragma once
#include "codec.hpp"

struct Gzip : public CODEC8withPimpl { Gzip(int level=1); };
