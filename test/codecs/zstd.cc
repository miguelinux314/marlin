#include <zstd.h>
#include <memory>
#include "zstd.hpp"

using namespace std;

class ZstdPimpl : public CODEC8AA {
	
	int level;
	ZSTD_CCtx* cctx;
	ZSTD_DCtx* dctx;
	
	std::string name() const { return std::string("Zstd")+char('0'+level); }

	void   compress(const AlignedArray8 &in, AlignedArray8 &out) const {

		out.resize(ZSTD_compressCCtx(cctx, out.begin(), out.capacity(), in.begin(), in.size(), level));
	}

	void uncompress(const AlignedArray8 &in, AlignedArray8 &out) const {

		out.resize(ZSTD_decompressDCtx(dctx, out.begin(), out.capacity(), in.begin(), in.size()));
	}

public:
	ZstdPimpl(int level_) : level(level_) {
		cctx = ZSTD_createCCtx();
		dctx = ZSTD_createDCtx();
	}
	
	~ZstdPimpl() {
		ZSTD_freeCCtx(cctx);
		ZSTD_freeDCtx(dctx);
	}
};

Zstd::Zstd(int level) : CODEC8withPimpl(new ZstdPimpl(level)) {}
