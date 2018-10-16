/***********************************************************************

Marlin: A Fast Entropy Codec

MIT License

Copyright (c) 2018 Manuel Martinez Torres

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

***********************************************************************/

#include <marlin.h>

#include <cstring>
#include <algorithm>

#define   LIKELY(condition) (__builtin_expect(static_cast<bool>(condition), 1))
#define UNLIKELY(condition) (__builtin_expect(static_cast<bool>(condition), 0))

using namespace marlin;

namespace {

template<typename TSource, typename MarlinIdx>
struct TCompress : TMarlin<TSource,MarlinIdx> {
	
	using typename TMarlin<TSource, MarlinIdx>::Word;
	using typename TMarlin<TSource, MarlinIdx>::CompressorTableIdx;
	using typename TMarlin<TSource, MarlinIdx>::MarlinSymbol;

	using TMarlin<TSource, MarlinIdx>::K;
	using TMarlin<TSource, MarlinIdx>::O;
	using TMarlin<TSource, MarlinIdx>::shift;
	using TMarlin<TSource, MarlinIdx>::maxWordSize;
	using TMarlin<TSource, MarlinIdx>::isSkip;
	using TMarlin<TSource, MarlinIdx>::conf;
	
	using TMarlin<TSource, MarlinIdx>::words;
	using TMarlin<TSource, MarlinIdx>::sourceAlphabet;
	using TMarlin<TSource, MarlinIdx>::marlinAlphabet;
	using TMarlin<TSource, MarlinIdx>::compressorTablePointer;
	


	constexpr static const size_t FLAG_NEXT_WORD = 1UL<<(8*sizeof(CompressorTableIdx)-1);

	__attribute__ ((target ("bmi2")))
	ssize_t shift8(View<const TSource> src, View<uint8_t> dst) const {
		
		uint64_t mask=0;
		for (size_t i=0; i<8; i++)
			mask |= ((1ULL<<shift)-1)<<(8ULL*i);

		const uint64_t *i64    = reinterpret_cast<const uint64_t *>(src.start);
		const uint64_t *i64end = reinterpret_cast<const uint64_t *>(src.end);

		uint8_t *o8 = dst.start;
		
		while (i64 != i64end) {
			*reinterpret_cast<uint64_t *>(o8) = _pext_u64(*i64++, mask);
			o8 += shift;
		}
		return o8 - dst.start;
	}
	
	class JumpTable {

		const size_t alphaStride;  // Bit stride of the jump table corresponding to the word dimension
		const size_t wordStride;  // Bit stride of the jump table corresponding to the word dimension
	public:
		
		JumpTable(size_t keySize, size_t overlap, size_t nAlpha) :
			alphaStride(std::ceil(std::log2(nAlpha))),
			wordStride(keySize+overlap) {}
			
		template<typename T>
		void initTable(T &table) {
			table = T(((1<<wordStride))*(1<<alphaStride),CompressorTableIdx(-1));
		}
		
		template<typename T, typename T0, typename T1>
		inline T &operator()(T *table, const T0 &word, const T1 &nextLetter) const { 
			auto v = (word&((1<<wordStride)-1))+(nextLetter<<wordStride);
//			auto v = ((word&((1<<wordStride)-1))<<alphaStride)+nextLetter;
			return table[v];
		}
	};

	ssize_t compressMarlin8 (
		View<const TSource> src, 
		View<uint8_t> dst, 
		std::vector<size_t> &unrepresentedSymbols) const 
	{
		
		MarlinIdx unrepresentedSymbolToken = marlinAlphabet.size();
		JumpTable jump(K, O, unrepresentedSymbolToken+1);	
		
		std::array<MarlinIdx, 1U<<(sizeof(TSource)*8)> source2marlin;
		source2marlin.fill(unrepresentedSymbolToken);
		for (size_t i=0; i<marlinAlphabet.size(); i++)
			source2marlin[marlinAlphabet[i].sourceSymbol>>shift] = i;

			  uint8_t *out   = dst.start;
		const TSource *in    = src.start;

		CompressorTableIdx j = 0; 

		
		//We look for the word that sets up the machine state.
		{		
			TSource ss = *in++;
			
			MarlinIdx ms = source2marlin[ss>>shift];
			if (ms==unrepresentedSymbolToken) {
				unrepresentedSymbols.push_back(in-src.start-1);
				ms = 0; // 0 must be always the most probable symbol;
				//printf("%04x %02x\n", in-src.start-1, ss);
			}
			
			for (size_t i=0; i<size_t(1<<K); i++) {
				
				if (words[i].size()==1 and words[i].front() == ms) {
					j = i;
					break;
				}
			}
		}
		
		const uint8_t shift_local = shift;
		while (in<src.end) {
			
			MarlinIdx ms = source2marlin[(*in++)>>shift_local];
			if (ms==unrepresentedSymbolToken) {
				unrepresentedSymbols.push_back(in-src.start-1);
				ms = 0; // 0 must be always the most probable symbol;
				//printf("%04x %02x\n", in-src.start-1, ss);
			}
			
			*out = j & 0xFF;
			j = jump(compressorTablePointer, j, ms);
			
			if (j & FLAG_NEXT_WORD) {
				out++;
			}

			if (dst.end-out<16) return -1;	// TODO: find the exact value
		}


		//if (not (j & FLAG_NEXT_WORD)) 
		*out++ = j & 0xFF;
		
		return out - dst.start;
	}

	ssize_t compressMarlinFast(
		View<const TSource> src, 
		View<uint8_t> dst, 
		std::vector<size_t> &unrepresentedSymbols) const 
	{
		
		MarlinIdx unrepresentedSymbolToken = marlinAlphabet.size();
		JumpTable jump(K, O, unrepresentedSymbolToken+1);	
		
		std::array<MarlinIdx, 1U<<(sizeof(TSource)*8)> source2marlin;
		source2marlin.fill(unrepresentedSymbolToken);
		for (size_t i=0; i<marlinAlphabet.size(); i++)
			source2marlin[marlinAlphabet[i].sourceSymbol>>shift] = i;

			  uint8_t *out   = dst.start;
		const TSource *in    = src.start;

		CompressorTableIdx j = 0; 

		
		//We look for the word that sets up the machine state.
		{		
			TSource ss = *in++;
			
			MarlinIdx ms = source2marlin[ss>>shift];
			if (ms==unrepresentedSymbolToken) {
				unrepresentedSymbols.push_back(in-src.start-1);
				ms = 0; // 0 must be always the most probable symbol;
				//printf("%04x %02x\n", in-src.start-1, ss);
			}
			
			for (size_t i=0; i<size_t(1<<K); i++) {
				
				if (words[i].size()==1 and words[i].front() == ms) {
					j = i;
					break;
				}
			}
		}

		uint32_t value = 0;
		 int32_t valueBits = 0;
		
		while (in<src.end) {
			
			if (dst.end-out<16) return -1;	// TODO: find the exact value
			
			TSource ss = *in++;
			
			MarlinIdx ms = source2marlin[ss>>shift];
			if (ms==unrepresentedSymbolToken) {
				unrepresentedSymbols.push_back(in-src.start-1);
				ms = 0; // 0 must be always the most probable symbol;
				//printf("%04x %02x\n", in-src.start-1, ss);
			}
			
			CompressorTableIdx jOld = j;
			j = jump(compressorTablePointer, j, ms);
			
			if (j & FLAG_NEXT_WORD) {
				
				value |= ((jOld | FLAG_NEXT_WORD) ^ FLAG_NEXT_WORD) << (32 - K - valueBits);
				valueBits += K;
			}
			
			while (valueBits>8) {
				*out++ = value >> 24;
				value = value << 8;
				valueBits -= 8;
			}
		}

		value |= ((j | FLAG_NEXT_WORD) ^ FLAG_NEXT_WORD) << (32 - K - valueBits);
		valueBits += K;
		
		while (valueBits>0) {
			*out++ = value >> 24;
			value = value << 8;
			valueBits -= 8;
		}
		
		return out - dst.start;
	}	
	
	ssize_t compressMarlinReference(
		View<const TSource> src, 
		View<uint8_t> dst, 
		std::vector<size_t> &unrepresentedSymbols) const 
	{
		
		MarlinIdx unrepresentedSymbolToken = marlinAlphabet.size();
		
		std::array<MarlinIdx, 1U<<(sizeof(TSource)*8)> source2marlin;
		source2marlin.fill(unrepresentedSymbolToken);
		for (size_t i=0; i<marlinAlphabet.size(); i++)
			source2marlin[marlinAlphabet[i].sourceSymbol>>shift] = i;

			  uint8_t *out   = dst.start;
		const TSource *in    = src.start;
		
		std::vector< std::map<Word, size_t> > wordMaps(1<<O);
		for (size_t i=0; i<words.size(); i++) 
			wordMaps[i>>K][words[i]] = i&((1<<K)-1);
		
		uint32_t value = 0, chapter = 0;
		 int32_t valueBits = 0;
		Word word;
		
		auto emitWord = [&](){
			value |= wordMaps[chapter][word] << (32 - K - valueBits);
			valueBits += K;
			chapter = wordMaps[chapter][word] & ((1<<O)-1);
			word.clear();
		};
		
		while (in<src.end) {
			
			if (dst.end-out<16) return -1;	// TODO: find the exact value		
			
			TSource ss = *in++;
			MarlinIdx ms = source2marlin[ss>>shift];
			
			if (ms==unrepresentedSymbolToken) {
				unrepresentedSymbols.push_back(in-src.start-1);
				ms = 0; // 0 must be always the most probable symbol;
			}
			
			word.push_back(ms);
			
			if (wordMaps[chapter].count(word) == 0) {
				
				word.pop_back();
				emitWord();
				word.push_back(ms);
			}

			while (valueBits>8) {
				*out++ = value >> 24;
				value = value << 8;
				valueBits -= 8;
			}		
			
		}
		if (word.size())
			emitWord();
		
		while (valueBits>0) {
			*out++ = value >> 24;
			value = value << 8;
			valueBits -= 8;
		}
		
		return out - dst.start;
	}

	std::unique_ptr<std::vector<CompressorTableIdx>> buildCompressorTable() const {

		MarlinIdx unrepresentedSymbolToken = marlinAlphabet.size();

		auto ret = std::make_unique<std::vector<CompressorTableIdx>>();
		JumpTable jump(K, O, unrepresentedSymbolToken+1);
		jump.initTable(*ret);
		
		std::array<MarlinIdx, 1U<<(sizeof(MarlinIdx)*8)> source2marlin;
		source2marlin.fill(unrepresentedSymbolToken);
		for (size_t i=0; i<marlinAlphabet.size(); i++)
			source2marlin[marlinAlphabet[i].sourceSymbol>>shift] = i;
		
		const size_t NumChapters = 1<<O;
		const size_t ChapterSize = 1<<K;
		std::vector<std::map<Word, size_t>> positions(NumChapters);

		// Init the mapping (to know where each word goes)
		for (size_t k=0; k<NumChapters; k++)
			for (size_t i=k*ChapterSize; i<(k+1)*ChapterSize; i++)
				positions[k][words[i]] = i;
				
		// Link each possible word to its continuation
		for (size_t k=0; k<NumChapters; k++) {
			for (size_t i=k*ChapterSize; i<(k+1)*ChapterSize; i++) {
				auto word = words[i];
				size_t wordIdx = i;
				while (word.size() > 1) {
					TSource lastSymbol = word.back();						
					word.pop_back();
					if (not positions[k].count(word)) throw(std::runtime_error("This word has no parent. SHOULD NEVER HAPPEN!!!"));
					size_t parentIdx = positions[k][word];
					jump(&ret->front(), parentIdx, lastSymbol) = wordIdx;
					wordIdx = parentIdx;
				}
			}
		}
					
		//Link between inner dictionaries
		for (size_t k=0; k<NumChapters; k++)
			for (size_t i=k*ChapterSize; i<(k+1)*ChapterSize; i++)
				for (size_t j=0; j<marlinAlphabet.size(); j++)
					if (jump(&ret->front(),i,j) == CompressorTableIdx(-1)) // words that are not parent of anyone else.
						jump(&ret->front(),i,j) = positions[i%NumChapters][Word(1,j)] + FLAG_NEXT_WORD;
											
		return ret;
	}

	ssize_t compress(View<const TSource> src, View<uint8_t> dst) const {
		
		//memcpy(dst.start,src.start,src.nBytes()); return src.nBytes();

		// Assertions
		if (dst.nBytes() < src.nBytes()) return -1; //TODO: Real error codes
		
		// Special case: empty! Nothing to compress.
		if (src.nElements()==0) return 0;

		// Special case: the entire block is made of one symbol!
		{
			size_t count = 0;
			for (size_t i=0; i<src.nElements() and src.start[i] == src.start[0]; i++)
				count++;
			
			if (count==src.nElements()) {
				reinterpret_cast<TSource *>(dst.start)[0] = src.start[0];
				return sizeof(TSource);
			}
		}

		// Special case: if srcSize is not multiple of 8, we force it to be.
		size_t padding = 0;
		while (src.nBytes() % 8 != 0) {
			
			*reinterpret_cast<TSource *&>(dst.start)++ = *src.start++;			
			padding += sizeof(TSource);
		}

		size_t residualSize = src.nElements()*shift/8;


		std::vector<size_t> unrepresentedSymbols;		
		// This part, we encode the number of unrepresented symbols in a byte.
		// We are optimistic and we hope that no unrepresented symbols are required.
		*dst.start = 0;
		
		// Valid portion available to encode the marlin message.
		View<uint8_t> marlinDst = marlin::make_view(dst.start+1,dst.end-residualSize);
		ssize_t marlinSize = -1;
		if (false) {
			marlinSize = compressMarlinReference(src, marlinDst, unrepresentedSymbols);
		} else if (K==8) {
			marlinSize = compressMarlin8(src, marlinDst, unrepresentedSymbols);
		} else {
			marlinSize = compressMarlinFast(src, marlinDst, unrepresentedSymbols);
		}
		
		size_t unrepresentedSize = unrepresentedSymbols.size() * ( sizeof(TSource) + (
			src.nElements() < 0x100 ? sizeof(uint8_t) :
			src.nElements() < 0x10000 ? sizeof(uint16_t) :
			src.nElements() < 0x100000000ULL ? sizeof(uint32_t) :sizeof(uint64_t)
			));
		
		
		//if (unrepresentedSize) printf("%d \n", unrepresentedSize);
		// If not worth encoding, we store raw.
		if (marlinSize < 0 	// If the encoded size is negative means that Marlin could not provide any meaningful compression, and the whole stream will be copied.
			or unrepresentedSymbols.size() > 255 
			or 1 + marlinSize + unrepresentedSize + residualSize > src.nBytes()) {

			memcpy(dst.start,src.start,src.nBytes());
			return padding + src.nBytes();
		}
		
		
		*dst.start++ = unrepresentedSymbols.size();
		dst.start += marlinSize;
		
		
		// Encode unrepresented symbols
		for (auto &s : unrepresentedSymbols) {
			if (src.nElements() < 0x100) {
				*reinterpret_cast<uint8_t *&>(dst.start)++ = s;	
			} else if (src.nElements() < 0x10000) {
				*reinterpret_cast<uint16_t *&>(dst.start)++ = s;	
			} else if (src.nElements() < 0x100000000ULL) {
				*reinterpret_cast<uint32_t *&>(dst.start)++ = s;	
			} else {
				*reinterpret_cast<uint64_t *&>(dst.start)++ = s;	
			}
			*reinterpret_cast<TSource *&>(dst.start)++ = src.start[s];	
		}
		
		// Encode residuals
		shift8(src, dst);
		
		return padding + 1 + marlinSize + unrepresentedSize + residualSize; 
	}

};
}

template<typename TSource, typename MarlinIdx>
auto TMarlin<TSource,MarlinIdx>::buildCompressorTable() const -> std::unique_ptr<std::vector<CompressorTableIdx>> {
	return static_cast<const TCompress<TSource,MarlinIdx> *>(this)->buildCompressorTable();
}

template<typename TSource, typename MarlinIdx>
ssize_t TMarlin<TSource,MarlinIdx>::compress(View<const TSource> src, View<uint8_t> dst) const {
	return static_cast<const TCompress<TSource,MarlinIdx> *>(this)->compress(src,dst);
}

////////////////////////////////////////////////////////////////////////
//
// Explicit Instantiations
#include "instantiations.h"
INSTANTIATE_MEMBER(buildCompressorTable() const -> std::unique_ptr<std::vector<CompressorTableIdx>>)	
INSTANTIATE_MEMBER(compress(View<const typename TMarlin::TSource_Type> src, View<uint8_t> dst) const -> ssize_t)	
