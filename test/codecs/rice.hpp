#pragma once
#include "codec.hpp"
#include "../../src/distribution.hpp"

struct Rice : public CODEC8withPimpl { Rice(Distribution::Type distType = Distribution::Laplace); };

