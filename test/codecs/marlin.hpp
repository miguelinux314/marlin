#pragma once
#include "codec.hpp"
#include "../../src/distribution.hpp"

struct Marlin : public CODEC8withPimpl { 

	enum Type { TUNSTALL, MARLIN };	
	Marlin(Distribution::Type distType = Distribution::Laplace, Type dictType = MARLIN, size_t dictSize = 12, size_t numDict = 11);	
};
