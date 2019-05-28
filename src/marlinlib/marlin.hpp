#pragma once
#include <x86intrin.h>
#include <util/dedupvector.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <stack>

#include <memory>
#include <algorithm>

#include <util/distribution.hpp>
#include <cassert>


namespace MarlinWiP {

	class Dictionary {

		friend class Encoder;
		friend class Decoder;
		
		//Marlin only encodes a subset of the possible source symbols.
		//Marlin symbols are sorted by probability in descending order, 
		//so the Marlin Symbol 0 is always corresponds to the probable alphabet symbol.
		typedef uint8_t SourceSymbol;
		typedef uint8_t MarlinSymbol; 
		
		static std::map<std::string, double> updateConf(
			const std::map<SourceSymbol, double> &symbols, 
			std::map<std::string, double> conf) {

//			conf.emplace("S",3);
//			conf.emplace("maxWordSize",7);
			
			conf.emplace("K",8);
			conf.emplace("O",2);
			
			conf.emplace("debug",1);
			conf.emplace("purgeProbabilityThreshold",1e-5);
			conf.emplace("iterations",3);
			conf.emplace("minMarlinSymbols", std::max(1U<<size_t(conf.at("O")),8U));
			conf.emplace("maxMarlinSymbols",(1U<<size_t(conf.at("K")))-1);
				
			if (not conf.count("S")) {
				conf["S"] = 0;
				double best = Dictionary(symbols, conf).efficiency;
				for (int s=1; s<6; s++) {
					conf["S"] = s;
					double e = Dictionary(symbols, conf).efficiency;
					if (e<=best) {
						conf["S"] = s-1;
						break;
					}
					best = e;
				}
			}
			
			if (not conf.count("maxWordSize")) {
				conf["maxWordSize"] = 15;
				double e15 = Dictionary(symbols, conf).efficiency;
				conf["maxWordSize"] = 7;
				double e7 = Dictionary(symbols, conf).efficiency;
				conf["maxWordSize"] = 3;
				double e3 = Dictionary(symbols, conf).efficiency;
				if (e7>1.0001*e3) {
					conf["maxWordSize"] = 7;
				}
				if (e15>1.0001*e7) {
					conf["maxWordSize"] = 15;
				}
			}
			
			return conf;
		}	

		// The Alphabet Class acts as a translation layer between SourceSymbols and MarlinSymbols.
		class Alphabet {

			struct SymbolAndProbability {
				SourceSymbol sourceSymbol;
				double p;
				constexpr bool operator<(const SymbolAndProbability &rhs) const {
					if (p!=rhs.p) return p>rhs.p; // Descending in probability
					return sourceSymbol<rhs.sourceSymbol; // Ascending in symbol index
				}
			};
			
			static double calcEntropy(const std::map<SourceSymbol, double> &symbols) {
				
				double distEntropy=0;
				for (auto &&s : symbols)
					if (s.second>0.)
						distEntropy += -s.second*std::log2(s.second);
				return distEntropy;
			}
			
		public:
		
			const std::map<SourceSymbol, double> symbols;
			const size_t shift;
			const double sourceEntropy;
			
			double rareSymbolProbability;
			std::vector<SymbolAndProbability> marlinSymbols;
			
			Alphabet(std::map<SourceSymbol, double> symbols_, std::map<std::string, double> conf) : 
				symbols(symbols_),
				shift(conf.at("S")),
				sourceEntropy(calcEntropy(symbols)) {
				
				// Group symbols by their high bits
				std::map<SourceSymbol, double> symbolsShifted;
				for (auto &&symbol : symbols)
					symbolsShifted[symbol.first>>shift] += symbol.second;
				
				for (auto &&symbol : symbolsShifted)
					marlinSymbols.push_back(SymbolAndProbability({SourceSymbol(symbol.first<<shift), symbol.second}));
					
				std::stable_sort(marlinSymbols.begin(),marlinSymbols.end());
				
				rareSymbolProbability = 0;
				while (marlinSymbols.size()>conf.at("minMarlinSymbols") and 
					  (marlinSymbols.size()>conf.at("maxMarlinSymbols") or
					  rareSymbolProbability<conf.at("purgeProbabilityThreshold"))) {
					
					rareSymbolProbability += marlinSymbols.back().p;
//					marlinSymbols.front().p +=  marlinSymbols.back().p;
					marlinSymbols.pop_back();
				}
			}
		};
					

		struct Word : std::vector<SourceSymbol> {
			
			using std::vector<SourceSymbol>::vector;
			
			double p = 0;
			MarlinSymbol state = 0;

			friend std::ostream& operator<< (std::ostream& stream, const Word& word) {
				for (auto &&s : word) 
					if (s<=26) 
						stream << char('a'+s); 
					else 
						stream << " #" << uint(s);
				return stream;
			}
		};
		

		struct Node;
		typedef std::shared_ptr<Node> SNode;	
		struct Node : std::vector<SNode> {
			double p=0;
			size_t sz=0;
		};
		


		SNode buildTree(std::vector<double> Pstates) const {

			// Normalizing the probabilities makes the algorithm more stable
			double factor = 1e-10;
			for (auto &&p : Pstates) factor += p;
			for (auto &&p : Pstates) p/=factor;
			for (auto &&p : Pstates) if (std::abs(p-1.)<0.0001) p=1.;
			for (auto &&p : Pstates) if (std::abs(p-0.)<0.0001) p=0.;


			std::vector<double> PN;
			for (auto &&a : alphabet.marlinSymbols) PN.push_back(a.p);
			PN.back() += alphabet.rareSymbolProbability;
			for (size_t i=PN.size()-1; i; i--)
				PN[i-1] += PN[i];

			std::vector<double> Pchild(PN.size());
			for (size_t i=0; i<PN.size(); i++)
				Pchild[i] = alphabet.marlinSymbols[i].p/PN[i];
			
			auto cmp = [](const SNode &lhs, const SNode &rhs) { return lhs->p<rhs->p;};
			std::priority_queue<SNode, std::vector<SNode>, decltype(cmp)> pq(cmp);

			// DICTIONARY INITIALIZATION
			SNode root = std::make_shared<Node>();
			
			// Include empty word
			pq.push(root);
			root->p = 1;
			
			for (size_t c=0; c<alphabet.marlinSymbols.size(); c++) {			
					
				root->push_back(std::make_shared<Node>());
				double sum = 0;
				for (size_t t = 0; t<=c; t++) sum += Pstates[t]/PN[t];
				root->back()->p = sum * alphabet.marlinSymbols[c].p;
				root->p -= root->back()->p;
				root->back()->sz = 1;
				pq.push(root->back());
			}
				
			// DICTIONARY GROWING
			size_t retiredNodes=0;
			while (not pq.empty() and (pq.size() + retiredNodes < (1U<<K))) {
					
				SNode node = pq.top();
				pq.pop();
				
				// retire words larger than maxWordSize that are meant to be extended by a symbol different than zero.
				if (node->sz >= maxWordSize and not node->empty()) {
					retiredNodes++;
					continue;
				}

				if (node->sz == 255) {
					retiredNodes++;
					continue;
				}
				
				if (node->size() == alphabet.marlinSymbols.size()) {
					retiredNodes++;
					continue;					
				}
				
				double p = node->p * Pchild[node->size()];
				node->push_back(std::make_shared<Node>());
				node->back()->p = p;
				node->back()->sz = node->sz+1;
				pq.push(node->back());
				node->p -= p;
				pq.push(node);
			}

			// Renormalize probabilities.
			{
				std::queue<SNode> q(std::deque<SNode>{ root });
				double sum=0, num=0;
				while (not q.empty()) {
					sum += q.front()->p; num++;
					q.front()->p *= factor;
					for (auto &&child : *q.front()) 
						q.push(child);
					q.pop();
				}
				std::cerr << sum << " sum - num " << num << std::endl;
			}
			return root;
		}


		
		std::vector<Word> buildChapterWords( const SNode root) const {
		
			std::vector<Word> ret;
			
			std::stack<std::pair<SNode, Word>> q;
			Word rootWord;
			rootWord.p = root->p;
			q.emplace(root, rootWord);
			
			while (not q.empty()) {
				SNode n = q.top().first;
				Word w = q.top().second;
				q.pop();
				ret.push_back(w);
				for (size_t i = 0; i<n->size(); i++) {
					
					Word w2 = w;
					w2.push_back(alphabet.marlinSymbols[i].sourceSymbol);
					w2.p = n->at(i)->p;
					w2.state = n->at(i)->size();
					
					assert(n->at(i)->sz == w2.size());
					q.emplace(n->at(i), w2);
				}
			}
			
			std::cout << ret.size() << std::endl;
			return ret;
		}


		
		std::vector<Word> arrangeAndFuse( const std::vector<SNode> chapters) const {

			std::vector<Word> ret;
			for (auto &&chapter : chapters) {
				
				std::vector<Word> sortedDictionary = buildChapterWords(chapter);
				
				auto cmp = [](const Word &lhs, const Word &rhs) { 
					if (lhs.state != rhs.state) return lhs.state<rhs.state;
					if (std::abs(lhs.p-rhs.p)/(lhs.p+rhs.p) > 1e-10) return lhs.p > rhs.p;
					return lhs<rhs;
				};
				// Note the +1, we keep the empty word in the first position.
				std::stable_sort(sortedDictionary.begin()+1, sortedDictionary.end(), cmp);
				
				std::vector<Word> w(1U<<K,Word());
				for (size_t i=0,j=0,k=0; i<sortedDictionary.size(); j+=(1U<<O)) {
					
					if (j>=w.size()) 
						j=++k;

					w[j] = sortedDictionary[i++];
				}
				ret.insert(ret.end(), w.begin(), w.end());
			}
			return ret;
		}
		

		// Debug functions		
		void print(std::vector<Word> dictionary) const {

			if (conf.at("debug")<3) return;
			if (conf.at("debug")<4 and dictionary.size()/(1U<<O) > 40) return;

			for (size_t i=0; i<dictionary.size()/(1U<<O); i++) { 
				
				for (size_t k=0; k<(1U<<O); k++) {
					
					auto idx = i + (k* (dictionary.size()/(1U<<O)));
					auto &&w = dictionary[idx];
					printf(" %02lX %01ld %2d %01.2le ",idx,i%(1U<<O),w.state,w.p);
					for (size_t j=0; j<8; j++) {
						if (j<w.size()) {
							char a = 'a';
							for (size_t x=0; alphabet.marlinSymbols[x].sourceSymbol != w[j]; x++, a++);
							putchar(a);
						} else {
							putchar(' ');
						}
					}
				}
				putchar('\n');
			}		
			putchar('\n');
		}



		void print(std::vector<std::vector<double>> Pstates) const {
			
			if (conf.at("debug")<3) return;
			for (size_t i=0; i<Pstates[0].size() and i<4; i++) { 
				
				printf("S: %02ld",i);
				for (size_t k=0; k<Pstates.size() and k<8; k++) 
						 printf(" %01.3lf",Pstates[k][i]);
				putchar('\n');
			}		
			putchar('\n');
		}



		double calcEfficiency(std::vector<Word> dictionary) const {
		
			double meanLength = 0;
			for (auto &&w : dictionary)
					meanLength += w.p * w.size();
			
			double shannonLimit = alphabet.sourceEntropy;
			
			// The decoding algorithm has 4 steps:
			double meanBitsPerSymbol = 0;                           // a memset
			meanBitsPerSymbol += (K/meanLength)*(1-alphabet.rareSymbolProbability);                      // Marlin VF
			meanBitsPerSymbol += alphabet.shift;                    // Raw storing of lower bits
			meanBitsPerSymbol += 2*K*alphabet.rareSymbolProbability;// Recovering rare symbols

			return shannonLimit / meanBitsPerSymbol;
		}

		

		std::vector<Word> buildDictionary() const {
			
			std::vector<std::vector<double>> Pstates;
			for (size_t k=0; k<(1U<<O); k++) {
				std::vector<double> PstatesSingle(alphabet.marlinSymbols.size(), 0.);
				PstatesSingle[0] = 1./(1U<<O);
				Pstates.push_back(PstatesSingle);
			}
			
			std::vector<SNode> dictionaries;
			for (size_t k=0; k<(1U<<O); k++)
				dictionaries.push_back(buildTree(Pstates[k]));
				
			std::vector<Word> ret = arrangeAndFuse(dictionaries);
				
			print(ret);
			
			size_t iterations = conf.at("iterations");
				
			while (iterations--) {

				// UPDATING STATE PROBABILITIES
				{
					for (auto &&pk : Pstates)
						for (auto &&p : pk)
							p = 0.;

					for (size_t i=0; i<ret.size(); i++)
						Pstates[i%(1U<<O)][ret[i].state] += ret[i].p;
				}
				
				print(Pstates);

				dictionaries.clear();
				for (size_t k=0; k<(1U<<O); k++)
					dictionaries.push_back(buildTree(Pstates[k]));
				
				ret = arrangeAndFuse(dictionaries);
				
				print(ret);
				if (conf.at("debug")>2) printf("Efficiency: %3.4lf\n", calcEfficiency(ret));		
			}
			if (conf.at("debug")>1) for (auto &&c : conf) std::cout << c.first << ": " << c.second << std::endl;
			if (conf.at("debug")>0) printf("Efficiency: %3.4lf\n", calcEfficiency(ret));

			return ret;
		}

	public:
		
		const std::map<std::string, double> conf;
		const Alphabet alphabet;
		const size_t K                = conf.at("K");           // Non overlapping bits of codeword.
		const size_t O                = conf.at("O");           // Bits that overlap between codewprds.
		const size_t maxWordSize      = conf.at("maxWordSize"); // Maximum number of symbols per word.
		const std::vector<Word> words = buildDictionary();      // All dictionary words.
		const double efficiency       = calcEfficiency(words);  // Theoretical efficiency of the dictionary.

		Dictionary( const std::map<SourceSymbol, double> &symbols,
			std::map<std::string, double> conf_ = std::map<std::string, double>()) 
			: conf(updateConf(symbols, conf_)), alphabet(symbols, conf) {}
		
		
		// Turns the vector into a map and uses the previous constructor
		Dictionary( const std::vector<double> &symbols,
			std::map<std::string, double> conf_ = std::map<std::string, double>()) 
			: Dictionary(
			[&symbols](){
				std::map<SourceSymbol, double> ret;
				for (size_t i=0; i<symbols.size(); i++)
					ret.emplace(SourceSymbol(i), symbols[i]);
				return ret;
			}()
			, conf_) {}
	};

	
	class Encoder { 
		
		using SourceSymbol = Dictionary::SourceSymbol;
		using MarlinSymbol = Dictionary::MarlinSymbol;
		using Word = Dictionary::Word;

		typedef uint32_t JumpIdx;
		// Structured as:
		// FLAG_NEXT_WORD
		// Where to jump next		
		
		constexpr static const size_t FLAG_NEXT_WORD = 1UL<<(8*sizeof(JumpIdx)-1);
		
		class JumpTable {

			constexpr static const size_t unalignment = 8; // Too much aligned reads break cache
			const size_t alphaStride;  // Bit stride of the jump table corresponding to the word dimension
			const size_t wordStride;  // Bit stride of the jump table corresponding to the word dimension
		public:

			std::vector<JumpIdx> table;		
		
			JumpTable(size_t keySize, size_t overlap, size_t nAlpha) :
				alphaStride(std::ceil(std::log2(nAlpha))),
				wordStride(keySize+overlap),
				table(((1<<wordStride)+unalignment)*(1<<alphaStride),JumpIdx(-1))
				{}
			
			template<typename T0, typename T1>
			JumpIdx &operator()(const T0 &word, const T1 &nextLetter) { 
				return table[(word&((1<<wordStride)-1))+(nextLetter*((1<<wordStride)+unalignment))];
			}

			template<typename T0, typename T1>
			constexpr JumpIdx operator()(const T0 &word, const T1 &nextLetter) const { 
				return table[(word&((1<<wordStride)-1))+(nextLetter*((1<<wordStride)+unalignment))];
			}
		};
		JumpTable jumpTable;

		const size_t shift;
		const size_t nMarlinSymbols;
		std::array<MarlinSymbol, 1U<<(sizeof(SourceSymbol)*8)> Source2JumpTableShifted;
		MarlinSymbol Source2JumpTable(SourceSymbol ss) const {
			return Source2JumpTableShifted[ss>>shift];
		}

	public:
		
		Encoder(const Dictionary &dict, const std::map<std::string, double> &) :
			jumpTable(dict.K, dict.O, dict.alphabet.marlinSymbols.size()),
			shift(dict.alphabet.shift),
			nMarlinSymbols(dict.alphabet.marlinSymbols.size()) { 

			Source2JumpTableShifted.fill(nMarlinSymbols);
			for (size_t i=0; i<dict.alphabet.marlinSymbols.size(); i++)
				Source2JumpTableShifted[dict.alphabet.marlinSymbols[i].sourceSymbol>>shift] = i;

			
			const size_t NumSections = 1<<dict.O;
			const size_t SectionSize = 1<<dict.K;
			std::vector<std::map<Word, size_t>> positions(NumSections);

			// Init the mapping (to know where each word goes)
			for (size_t k=0; k<NumSections; k++)
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++)
					positions[k][dict.words[i]] = i;

			// Link each possible word to its continuation
			for (size_t k=0; k<NumSections; k++) {
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++) {
					auto word = dict.words[i];
					size_t wordIdx = i;
					while (not word.empty()) {
						SourceSymbol lastSymbol = word.back();						
						word.pop_back();
						if (not positions[k].count(word)) throw(std::runtime_error("SHOULD NEVER HAPPEN"));
						size_t parentIdx = positions[k][word];
						jumpTable(parentIdx, Source2JumpTable(lastSymbol)) = wordIdx;
						wordIdx = parentIdx;
					}
				}
			}
						
			//Link between inner dictionaries
			for (size_t k=0; k<NumSections; k++)
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++)
					for (size_t j=0; j<dict.alphabet.marlinSymbols.size(); j++)
						if (jumpTable(i,j)==JumpIdx(-1)) // words that are not parent of anyone else.
							jumpTable(i,j) = positions[i%NumSections][Word(1,dict.alphabet.marlinSymbols[j].sourceSymbol)] + FLAG_NEXT_WORD;
							
		}
		
		size_t operator()(const uint8_t * const i8start, const uint8_t * const i8end, uint8_t * const o8start, uint8_t * const o8end) const {
			
			assert(o8end-o8start >= i8end-i8start);
			assert( (i8end-i8start)%8 == 0 );
			if (i8start==i8end) return 0;

			// Fast check to see if all the block is made of a single symbol
			{
				const uint8_t *i8test = i8start+1;
				while (i8test!=i8end and *i8test==i8start[0]) i8test++;
				if (i8test==i8end) {
					*o8start = i8start[0];
					return 1;
				}
			}

			uint8_t *o8 = o8start;
			const uint8_t *i8 = i8start;
			
			// Encode Marlin, with rare symbols preceded by an empty word
			{
				
				ssize_t maxTargetSize = std::max(0UL, (i8end-i8start)-((i8end-i8start)*shift/8));
				
				JumpIdx j = 0;
				while (i8<i8end and maxTargetSize>8) {				
					
					SourceSymbol ss = *i8++;
					
					MarlinSymbol ms = Source2JumpTable(ss);
					bool rare = ms==nMarlinSymbols;
					if (rare) {
						if (j) *o8++ = j; // Finish current word, if any;
						*o8++ = j = 0;
						*o8++ = (ss>>shift)<<shift;
						maxTargetSize-=3;
						continue;
					}
					
					JumpIdx jOld = j;
					j = jumpTable(j, ms);
					
					if (j & FLAG_NEXT_WORD) {
						*o8++ = jOld & 0xFF;
						maxTargetSize--;
					}
				}
				if (j) *o8++ = j;
				if (maxTargetSize <= 8) { // Just encode the block uncompressed.
					memcpy(o8start, i8start, i8end-i8start);
					return i8end-i8start;
				}
			}
			
			// Encode residuals
			if (shift) {
				uint64_t mask=0;
				for (size_t i=0; i<8; i++)
					mask |= ((1ULL<<shift)-1)<<(8ULL*i);
				
				const uint64_t *i64    = (const uint64_t *)i8start;
				const uint64_t *i64end = (const uint64_t *)i8end;

				while (i64 != i64end) {
					*(uint64_t *)o8 = _pext_u64(*i64++, mask);
					o8 += shift;
				}
			}
			return o8-o8start;
		}
	};
	

	class Decoder {

		using SourceSymbol = Dictionary::SourceSymbol;
		using MarlinSymbol = Dictionary::MarlinSymbol;
		
		const size_t shift;
		const size_t O;
		const size_t maxWordSize;
		
		std::vector<SourceSymbol> decoderTable;
		const SourceSymbol * const D;
		const SourceSymbol mostCommonSourceSymbol;

		
		template<typename T, size_t CO>
		size_t decode8(const uint8_t * const i8start, const uint8_t * const i8end, uint8_t * const o8start, uint8_t * const o8end) const {
			
			      uint8_t *o8 = o8start;
			const uint8_t *i8 = i8start;
			
			// Special case, same size! this means the block is uncompressed.
			if (i8end-i8start == o8end-o8start) {
				memcpy(o8start,i8start,i8end-i8start);
				return o8end-o8start;
			}

			// Special case, size 1! this means the block consists all of just one symbol.
			if (i8end-i8start == 1) {
				memset(o8start,*i8start,o8end-o8start);
				return o8end-o8start;
			}

			memset(o8start,mostCommonSourceSymbol,o8end-o8start);
//			return o8end-o8start;
			
			// Decode the Marlin Section
			{

				const uint8_t *endMarlin = i8end - (o8end-o8start)*shift/8;

//				const uint32_t overlappingMask = (1<<(8+O))-1;
				constexpr const uint32_t overlappingMask = (1<<(8+CO))-1;
//				constexpr const T clearSizeMask = T(-1)>>8;
				constexpr const T clearSizeMask = 0;
				uint64_t value = 0;

				while (i8<endMarlin-9) {
					
					uint32_t v32 = (*(const uint32_t *)i8);
/*					if (((v32 - 0x01010101UL) & ~v32 & 0x80808080UL)) { // Fast test for zero

						uint8_t in = *i8++;
						if (in==0) {
							*o8++ = *i8++;
							value = (value<<8) + 0;
						} else {
							value = (value<<8) + in;
							T v = ((const T *)D)[value & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}
						
						in = *i8++;
						if (in==0) {
							*o8++ = *i8++;
							value = (value<<8) + 0;
						} else {
							value = (value<<8) + in;
							T v = ((const T *)D)[value & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}
						
						in = *i8++;
						if (in==0) {
							*o8++ = *i8++;
							value = (value<<8) + 0;
						} else {
							value = (value<<8) + in;
							T v = ((const T *)D)[value & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}
						
						in = *i8++;
						if (in==0) {
							*o8++ = *i8++;
							value = (value<<8) + 0;
						} else {
							value = (value<<8) + in;
							T v = ((const T *)D)[value & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}
						
					} else { // Has no zeroes! hurray!*/
						i8+=4;
						//clearSizeMask = 0;
						value = (value<<32) +  v32; //__builtin_bswap32(v32);
						{
							T v = ((const T *)D)[(value>>24) & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
							
						}

						{
							T v = ((const T *)D)[(value>>16) & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}

						{
							T v = ((const T *)D)[(value>>8) & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}

						{
							T v = ((const T *)D)[value & overlappingMask];
							*((T *)o8) = v & clearSizeMask;
							o8 += v >> ((sizeof(T)-1)*8);
						}
					//}
				}
				
				while (i8<endMarlin) {
					uint8_t in = *i8++;
					if (in==0) {
						*o8++ = *i8++;
					} else {
						value = (value<<8) + in;
						const T *v = &((const T *)D)[value & overlappingMask];
						memcpy(o8, v, std::min(sizeof(T)-1,size_t(*v >> ((sizeof(T)-1)*8))));
						o8 += *v >> ((sizeof(T)-1)*8);
					}
				}				
				//if (endMarlin-i8 != 0) std::cerr << " {" << endMarlin-i8 << "} "; // SOLVED! PROBLEM IN THE CODE
				//if (o8end-o8 != 0) std::cerr << " [" << o8end-o8 << "] "; // SOLVED! PROBLEM IN THE CODE
			}

			// Decode residuals
			if (shift) {
				uint64_t mask=0;
				for (size_t i=0; i<8; i++)
					mask |= ((1ULL<<shift)-1)<<(8ULL*i);
				
				uint64_t *o64    = (uint64_t *)o8start;
				uint64_t *o64end = (uint64_t *)o8end;

				while (o64 != o64end) {
					*o64++ += _pdep_u64(*(const uint64_t *)i8, mask);
					i8 += shift;
				}
			}
			return o8end-o8start;
		}
		
	public:
		
		
		Decoder(const Dictionary &dict, const std::map<std::string, double> &) :
			shift(dict.alphabet.shift),
			O(dict.O),
			maxWordSize(dict.maxWordSize),
			decoderTable(dict.words.size()*(maxWordSize+1)),
			D(decoderTable.data()),
			mostCommonSourceSymbol(dict.alphabet.marlinSymbols.front().sourceSymbol) {
				
			
			SourceSymbol *d = &decoderTable.front();
			for (size_t i=0; i<dict.words.size(); i++) {
				for (size_t j=0; j<maxWordSize; j++)
					*d++ = (dict.words[i].size()>j ? dict.words[i][j] : SourceSymbol(0));
				*d++ = dict.words[i].size();
			}
		}
		
		size_t operator()(const uint8_t * const i8start, const uint8_t * const i8end, uint8_t * const o8start, uint8_t * const o8end) const {
			
			if (maxWordSize==3) {
				switch (O) {
					case   0: return decode8<uint32_t,0>(i8start, i8end, o8start, o8end);
					case   1: return decode8<uint32_t,1>(i8start, i8end, o8start, o8end);
					case   2: return decode8<uint32_t,2>(i8start, i8end, o8start, o8end);
					case   3: return decode8<uint32_t,3>(i8start, i8end, o8start, o8end);
					case   4: return decode8<uint32_t,4>(i8start, i8end, o8start, o8end);
				}
			}

			if (maxWordSize==7) {
				switch (O) {
					case   0: return decode8<uint64_t,0>(i8start, i8end, o8start, o8end);
					case   1: return decode8<uint64_t,1>(i8start, i8end, o8start, o8end);
					case   2: return decode8<uint64_t,2>(i8start, i8end, o8start, o8end);
					case   3: return decode8<uint64_t,3>(i8start, i8end, o8start, o8end);
					case   4: return decode8<uint64_t,4>(i8start, i8end, o8start, o8end);
				}
			}
			//throw std::runtime_error ("unsupported maxWordSize");	
		}
	};
	

	struct SingleDictionaryCodec {
		
		const double efficiency;
		const Encoder encoder;
		const Decoder decoder;
		SingleDictionaryCodec(const Dictionary &dict, const std::map<std::string, double> &configuration = std::map<std::string, double>()) : 
			efficiency(dict.efficiency),
			encoder(dict, configuration), 
			decoder(dict, configuration) {}
			
		SingleDictionaryCodec(const std::vector<double> &pdf, const std::map<std::string, double> &configuration = std::map<std::string, double>()) : 
			SingleDictionaryCodec(Dictionary(pdf, configuration), configuration) {}

		template<typename TIN, typename TOUT>
		void encode(const TIN &in, TOUT &out) const {
			
			if (out.size() < in.size() + 4) out.resize(in.size() + 4);

			*(uint32_t *)&*out.begin() = uint32_t(in.size());
			size_t sz = 4 + encoder((const uint8_t *)&*in.begin(),(const uint8_t *)&*in.end(),(uint8_t *)&*out.begin()+4,(uint8_t *)&*out.end());
			out.resize(sz);
		}

		template<typename TIN, typename TOUT>
		void decode(const TIN &in, TOUT &out) const { 

			size_t uncompressedSize = *(const uint32_t *)&*in.begin();
			out.resize(uncompressedSize);
			decoder((const uint8_t *)&*in.begin()+4,(const uint8_t *)&*in.end(),(uint8_t *)&*out.begin(),(uint8_t *)&*out.end());
		}
		
				
		std::map<std::string,double> benchmark(const std::vector<double> &pdf, size_t sz = 1<<20) const {
			
			std::map<std::string,double> results;
			
			struct TestTimer {
				timespec c_start, c_end;
				void start() { clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c_start); };
				void stop () { clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c_end); };
				double operator()() { return (c_end.tv_sec-c_start.tv_sec) + 1.E-9*(c_end.tv_nsec-c_start.tv_nsec); }
			};
			TestTimer tEncode, tDecode;
			
			auto testData = Distribution::getResiduals(pdf, sz);
			
			decltype(testData)  compressedData, uncompressedData;
			compressedData  .reserve(8*testData.size());
			uncompressedData.reserve(8*testData.size());

			compressedData.clear();
			encode(testData,compressedData);
			
			// Encoding bechmark
			tEncode.start();
			compressedData.clear();
			encode(testData,compressedData);
			tEncode.stop();			

			size_t encoderTimes = 1+(2/tEncode());
			tEncode.start();
			for (size_t t=0; t<encoderTimes; t++) {
				compressedData.clear();
				encode(testData,compressedData);
			}
			tEncode.stop();
			
			// Decoding benchmark
			uncompressedData.resize(testData.size());
			decode(compressedData,uncompressedData);
			decode(compressedData,uncompressedData);
			decode(compressedData,uncompressedData);

			tDecode.start();
	//		uncompressedData.resize(testData.size());
			decode(compressedData,uncompressedData);
			tDecode.stop();

			size_t decoderTimes = 1+(2/tDecode());
			tDecode.start();
			for (size_t t=0; t<decoderTimes; t++) {
	//			uncompressedData.resize(testData.size());
				decode(compressedData,uncompressedData);
			}
			tDecode.stop();

			// Speed calculation
			results["encodingSpeed"] = encoderTimes*testData.size()/tEncode()/(1<<20);
			results["decodingSpeed"] = decoderTimes*testData.size()/tDecode()/(1<<20);
			std::cerr << "Enc: " << results["encodingSpeed"] << "MiB/s Dec: " << results["decodingSpeed"] << "MiB/s" << std::endl;
			
			// Efficiency calculation
			results["shannonLimit"] = Distribution::entropy(pdf)/std::log2(pdf.size());
			results["empiricalEfficiency"] = results["shannonLimit"] / (compressedData.size()/double(testData.size()));
			
			std::cerr << testData.size() << " " << compressedData.size() << " " << efficiency <<  " " << results["empiricalEfficiency"] << " " << std::endl;
			

			if (testData!=uncompressedData) {

				std::cerr << testData.size() << " " << uncompressedData.size() << std::endl;

				for (size_t i=0; i<10; i++) { std::cerr << int(testData[i]) << " | "; } std::cerr << std::endl;
				for (size_t i=0; i<10; i++) { std::cerr << int(uncompressedData[i]) << " | "; } std::cerr << std::endl;

				for (size_t i=0,j=0; i<100000 and i<testData.size() and i<uncompressedData.size(); i++) {
					j = j*2+int(testData[i]==uncompressedData[i]);
					if (i%16==0)
						std::cerr << "0123456789ABCDEF"[j%16] << (i%(64*16)?"":"\n");
				}
				std::cerr << std::endl;
			}
			
			return results;
		}
	};
}



class Marlin2018Simple {
	
	// Configuration
	constexpr static const bool enableDedup = true;	
	constexpr static const bool enableVictimDictionary = true;
	constexpr static const double purgeProbabilityThreshold = 1e-2;
	constexpr static const size_t iterationLimit = 5;
	constexpr static const bool debug = false;

	typedef uint8_t Symbol; // storage used to store an input symbol.
	//typedef uint16_t WordIdx; // storage that suffices to store a word index.

	struct SymbolAndProbability {
		Symbol symbol;
		double p;
		bool operator<(const SymbolAndProbability &rhs) const {
			if (p!=rhs.p) return p>rhs.p; // Descending in probability
			return symbol<rhs.symbol; // Ascending in symbol index
		}
	};
	
	struct Alphabet : public std::vector<SymbolAndProbability> {
		
		Alphabet(const std::map<Symbol, double> &symbols) {
			for (auto &&symbol : symbols)
				this->push_back(SymbolAndProbability({symbol.first, symbol.second}));
			std::stable_sort(this->begin(),this->end());
		}
		Alphabet(const std::vector<double> &symbols) {
			for (size_t i=0; i<symbols.size(); i++)
				this->push_back(SymbolAndProbability({Symbol(i), symbols[i]}));
			std::stable_sort(this->begin(),this->end());
		}
	};
	
	struct Word : std::vector<Symbol> {
		using std::vector<Symbol>::vector;
		double p = 0;
		Symbol state = 0;
		
		friend std::ostream& operator<< (std::ostream& stream, const Word& word) {
			stream << "{";
			for (size_t i=0; i<word.size(); i++) {
				if (i) stream << ",";
				stream << int(word[i]);
			}
			stream << "}";
			return stream;
        }
	};

	class Dictionary : public std::vector<Word> {
		
		struct Node;		
		struct Node : std::vector<std::shared_ptr<Node>> {
			double p=0;
			size_t sz=0;
			size_t erased=0;
		};

		std::shared_ptr<Node> buildTree(std::vector<double> Pstates, bool isVictim) const {

			std::vector<double> PN;
			for (auto &&a : alphabet) PN.push_back(a.p);
			for (size_t i=alphabet.size()-1; i; i--)
				PN[i-1] += PN[i];

			std::vector<double> Pchild(alphabet.size());
			for (size_t i=0; i<alphabet.size(); i++)
				Pchild[i] = alphabet[i].p/PN[i];

			
			auto cmp = [](const std::shared_ptr<Node> &lhs, const std::shared_ptr<Node> &rhs) { 
				return lhs->p<rhs->p;};		
//				return lhs->p*(1+std::pow(lhs->sz,1)) < rhs->p*(1+std::pow(rhs->sz,1));	};

			std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, decltype(cmp)> pq(cmp);
			size_t retiredNodes=0;
			
			bool enableVictimDict = Marlin2018Simple::configuration("enableVictim", Marlin2018Simple::enableVictimDictionary);
			
			double ppThres = Marlin2018Simple::purgeProbabilityThreshold/(1U<<keySize);

			// DICTIONARY INITIALIZATION
			std::shared_ptr<Node> root = std::make_shared<Node>();
			root->erased = true;
			
			auto pushAndPrune = [this,&pq,&retiredNodes,&root, isVictim, ppThres, enableVictimDict](std::shared_ptr<Node> node) {
				if (isVictim or 
					(not enableVictimDict) or 
					node->p>ppThres
					//or node->size()>0
					) {
					if (node->sz<maxWordSize) {
						pq.push(node);
					} else {
						retiredNodes++;
					}
				} else {
					node->erased = true;
					root->p += node->p;
				}
			};

			
			// Include empty word
			pq.push(root); // Does not do anything, only uses a spot
			
			long double factor = 0.;
			for (auto &&p : Pstates) factor += p;
			for (auto &&p : Pstates) p/=factor;
			for (auto &&p : Pstates) if (std::abs(p-1.)<0.0001) p=1.;
			for (auto &&p : Pstates) if (std::abs(p-0.)<0.0001) p=0.;
			

			for (size_t c=0; c<alphabet.size(); c++) {			
					
				root->push_back(std::make_shared<Node>());
				double sum = 0;
				for (size_t t = 0; t<=c; t++) sum += Pstates[t]/PN[t];
				root->back()->p = sum * alphabet[c].p;
				//if (c==0) std::cerr << "pp" << 
				root->back()->sz = 1;
				
				//if (isVictim or (not Marlin2018Simple::enableVictimDictionary) or root->back()->p>Marlin2018Simple::purgeProbabilityThreshold)
				pushAndPrune(root->back());
			}
				
			// DICTIONARY GROWING
			while (not pq.empty() and (pq.size() + retiredNodes < (1U<<keySize))) {
					
				std::shared_ptr<Node> node = pq.top();
				pq.pop();
				
				double p = node->p * Pchild[node->size()];
				node->push_back(std::make_shared<Node>());
				node->back()->p = p;
				node->back()->sz = node->sz+1;

				node->p -= p;
				pushAndPrune(node->back());
					
				if (false or (node->size()<alphabet.size()-1)) {

					pushAndPrune(node);
						
				} else {
					node->erased = true;
					node->p = 0;

					node->push_back(std::make_shared<Node>());
					node->back()->p = node->p;
					node->back()->sz = node->sz+1;
					pushAndPrune(node->back());
				}
			}

			{
				std::stack<std::shared_ptr<Node>> q;
				q.emplace(root);
				while (not q.empty()) {
					std::shared_ptr<Node> n = q.top();
					q.pop();
					n->p *= factor;
					for (size_t i = 0; i<n->size(); i++)
						q.emplace(n->at(i));
				}
			}
			return root;
			
			
		}
		
		std::vector<Word> buildWords( const std::shared_ptr<Node> root) const {
		
			std::vector<Word> ret;
			
			std::stack<std::pair<std::shared_ptr<Node>, Word>> q;
			Word rootWord; rootWord.p = root->p;
			q.emplace(root, rootWord);
			while (not q.empty()) {
				std::shared_ptr<Node> n = q.top().first;
				Word w = q.top().second;
				q.pop();
				if (not n->erased) ret.push_back(w);
				for (size_t i = 0; i<n->size(); i++) {
					
					Word w2 = w;
					w2.push_back(alphabet[i].symbol);
					w2.p = n->at(i)->p;
					w2.state = n->at(i)->size();
					
					assert(n->at(i)->sz == w2.size());
					q.emplace(n->at(i), w2);
				}
			}
			return ret;
		}
		
		std::vector<Word> arrangeAndFuse( const std::vector<std::shared_ptr<Node>> nodes, size_t victimIdx ) const {

			std::vector<Word> ret;
			for (size_t n = 0; n<nodes.size(); n++) {
				
				std::vector<Word> sortedDictionary = buildWords(nodes[n]);
				auto cmp = [](const Word &lhs, const Word &rhs) { 
					if (lhs.state != rhs.state) return lhs.state<rhs.state;
					if (std::abs(lhs.p-rhs.p)/(lhs.p+rhs.p) > 1e-10) return lhs.p > rhs.p;
					return lhs<rhs;
				};
				std::stable_sort(sortedDictionary.begin(), sortedDictionary.end(), cmp);
				
				if (Marlin2018Simple::configuration("shuffle",false))
					std::random_shuffle(sortedDictionary.begin(), sortedDictionary.end());
					
				
				std::vector<Word> w(1<<keySize);
				for (size_t i=0,j=0,k=0; i<sortedDictionary.size(); j+=(1<<overlap)) {
					
					if (j>=w.size()) 
						j=++k;
						
					if (victimIdx==j) {
						w[j] = Word();
						w[j].p = nodes[n]->p;
					} else {
						w[j] = sortedDictionary[i++];
					}
				}
				ret.insert(ret.end(), w.begin(), w.end());
			}
			return ret;
		}
		
		// Debug functions
		
		void print(std::vector<Word> dictionary) {

			if (dictionary.size()>40) return;

			for (size_t i=0; i<dictionary.size()/(1U<<overlap); i++) { 
				
				for (size_t k=0; k<(1U<<overlap); k++) {
					
					auto idx = i + (k* (dictionary.size()/(1U<<overlap)));
					auto &&w = dictionary[idx];
					printf(" %02lX %01ld %2d %01.2le ",idx,i%(1<<overlap),w.state,w.p);
					for (size_t j=0; j<8; j++) putchar(j<w.size()?'a'+w[j]:' ');
				}
				putchar('\n');
			}		
			putchar('\n');
		}

		static void print(std::vector<std::vector<double>> Pstates) {
			
			for (size_t i=0; i<Pstates[0].size() and i<4; i++) { 
				
				printf("S: %02ld",i);
				for (size_t k=0; k<Pstates.size() and k<8; k++) 
						 printf(" %01.3lf",Pstates[k][i]);
				putchar('\n');
			}		
			putchar('\n');
		}
			
		
	public:

		const Alphabet alphabet;
		const size_t keySize;     // Non overlapping bits of the word index in the big dictionary.
		const size_t overlap;     // Bits that overlap between keys.s
		const size_t maxWordSize; // Maximum number of symbols that a word in the dictionary can have.

		double calcEfficiency() const {
		
			double meanLength = 0;
			for (auto &&w : *this)
				meanLength += w.p * w.size();
			
			std::vector<double> P;
			for (auto &&a: alphabet) P.push_back(a.p);
			double shannonLimit = Distribution::entropy(P)/std::log2(P.size());

			return shannonLimit / (keySize / (meanLength*std::log2(P.size())));
		}
		
		Dictionary(const Alphabet &alphabet_, size_t keySize_, size_t overlap_, size_t maxWordSize_)
			: alphabet(alphabet_), keySize(keySize_), overlap(overlap_), maxWordSize(maxWordSize_) {
			
			std::vector<std::vector<double>> Pstates;
			for (auto k=0; k<(1<<overlap); k++) {
				std::vector<double> PstatesSingle(alphabet.size(), 0.);
				PstatesSingle[0] = 1./(1<<overlap);
				Pstates.push_back(PstatesSingle);
			}
			
			int victimDictionary = 0;
			
			std::vector<std::shared_ptr<Node>> dictionaries;
			for (auto k=0; k<(1<<overlap); k++)
				dictionaries.push_back(buildTree(Pstates[k], k==victimDictionary) );
				
			*(std::vector<Word> *)this = arrangeAndFuse(dictionaries,victimDictionary);
				
			if (Marlin2018Simple::configuration("debug", Marlin2018Simple::debug)) print(*this);
			
			size_t iterations= Marlin2018Simple::configuration("iterations", Marlin2018Simple::iterationLimit);
				
			while (iterations--) {

				// UPDATING STATE PROBABILITIES
				{
					for (auto k=0; k<(1<<overlap); k++)
						Pstates[k] = std::vector<double>(alphabet.size(), 0.);

					for (size_t i=0; i<size(); i++)
						Pstates[i%(1<<overlap)][(*this)[i].state] += (*this)[i].p;
				}
				
				// Find least probable subdictionary
				{
					double minP = 1.1;
					for (size_t i=0; i<Pstates.size(); i++) {
						double sumProb = 0.;
						for (auto &&ps : Pstates[i])
							sumProb += ps;
						if (sumProb > minP) continue;
						minP = sumProb;
						victimDictionary = i;
					}
				}
				if (Marlin2018Simple::configuration("debug", Marlin2018Simple::debug)) print(Pstates);

				dictionaries.clear();
				for (auto k=0; k<(1<<overlap); k++)
					dictionaries.push_back(buildTree(Pstates[k], k==victimDictionary) );
				
				*(std::vector<Word> *)this = arrangeAndFuse(dictionaries,victimDictionary);
				
				if (Marlin2018Simple::configuration("debug", Marlin2018Simple::debug)) print(*this);
				if (Marlin2018Simple::configuration("debug", Marlin2018Simple::debug)) printf("Efficiency: %3.4lf\n", calcEfficiency());		
			}
			if (Marlin2018Simple::configuration("debug", Marlin2018Simple::debug)) printf("Efficiency: %3.4lf\n", calcEfficiency());				
		}			
	};
	const Dictionary dictionary;
	
	struct Encoder { 

		typedef uint32_t JumpIdx;
		// Structured as:
		// FLAG_NEXT_WORD
		// FLAG_INSERT_EMPTY_WORD
		// Current Dictionary
		// Where to jump next
		
		constexpr static const size_t FLAG_NEXT_WORD = 1UL<<(8*sizeof(JumpIdx)-1);
		constexpr static const size_t FLAG_INSERT_EMPTY_WORD = 1UL<<(8*sizeof(JumpIdx)-2);
		
		class JumpTable {

			constexpr static const size_t unalignment = 8;
			const size_t alphaStride;  // Bit stride of the jump table corresponding to the word dimension
			const size_t wordStride;  // Bit stride of the jump table corresponding to the word dimension
			size_t nextIntermediatePos = 1<<(wordStride-1);	
			
			std::shared_ptr<DedupVector<JumpIdx>> dv;

		public:

			std::vector<JumpIdx> table;		
			const JumpIdx *data;
		
			JumpTable(size_t keySize, size_t overlap, size_t nAlpha) :
				alphaStride(std::ceil(std::log2(nAlpha))),
				wordStride(keySize+overlap+1), // Extra bit for intermediate nodes.
				table(((1<<wordStride)+unalignment)*(1<<alphaStride),JumpIdx(-1)),
				data(table.data())
				{}
			
			template<typename T0, typename T1>
			JumpIdx &operator()(const T0 &word, const T1 &nextLetter) { 
//				if ((word&((1<<wordStride)-1))+(nextLetter<<wordStride) < 0) std::cerr << "Underrun" << std::endl;
//				if ((word&((1<<wordStride)-1))+(nextLetter<<wordStride) >= table.size()) std::cerr << "Overrun" << std::endl;

//				return table[((word&((1<<wordStride)-1))<<alphaStride) + nextLetter];

//				return table[(word&((1<<wordStride)-1))+(nextLetter<<wordStride)];
				return table[(word&((1<<wordStride)-1))+(nextLetter*((1<<wordStride)+unalignment))];
			}

			template<typename T0, typename T1>
			JumpIdx operator()(const T0 &word, const T1 &nextLetter) const { 
//				if ((word&((1<<wordStride)-1))+(nextLetter<<wordStride) < 0) std::cerr << "Underrun" << std::endl;
//				if ((word&((1<<wordStride)-1))+(nextLetter<<wordStride) >= table.size()) std::cerr << "Overrun" << std::endl;
//				return table[((word&((1<<wordStride)-1))<<alphaStride) + nextLetter];

//				return data[((word&((1<<wordStride)-1))<<alphaStride) + nextLetter];

//				return data[(word&((1<<wordStride)-1))+(nextLetter<<wordStride)];
				return data[(word&((1<<wordStride)-1))+(nextLetter*((1<<wordStride)+unalignment))];
			}
			
			size_t getNewPos() { return nextIntermediatePos++; }
			
			bool isIntermediate(size_t pos) const { return pos & (1<<(wordStride-1)); }
			
			void dedup() {
				dv = std::make_shared<DedupVector<JumpIdx>>(table);
				data = (*dv)();
			}
			
			void clean(JumpIdx start, const Dictionary &dict) {
				
				size_t NumSections = 1<<dict.overlap;
				size_t SectionSize = 1<<dict.keySize;
			
				// Deduplicate
				if (true) {
					for (size_t k=0; k<NumSections; k++) {
						bool ok = false;
						for (size_t k2=0; (not ok) and (k2<k); k2++) {
							ok = true;
							for (size_t i=0; ok and (i<SectionSize); i++)
								ok = (dict[k*SectionSize+i] == dict[k2*SectionSize+i]);
								
							if (true and ok) {
								for (auto &v : table)
									if (((v>>dict.keySize)&((1<<dict.overlap)-1)) == k)
										v = ((v&(FLAG_NEXT_WORD + FLAG_INSERT_EMPTY_WORD)) | 
											(k2<<dict.keySize) | 
											(v&((1<<dict.keySize)-1)));
							}
						}
					}
				}
			
				// Zero unreachable
				if (true) {
					std::vector<bool> reachable(1<<wordStride,false);
					reachable[start&((1U<<wordStride)-1)] = true;
					for (auto &&v : table) 
						reachable[v&((1U<<wordStride)-1)] = true;
					
					for (size_t i=0; i<(1U<<wordStride); i++)
						if (not reachable[i])
							for (size_t j=0; j<(1U<<alphaStride); j++)
								(*this)(i,j) = -1;
				}
				
				{
					size_t unreachable = 0;
					for (auto &&v : table)
						if (v==JumpIdx(-1)) unreachable++;
						
					std::cerr << table.size() << " " << unreachable << " " << (100.*unreachable)/table.size() << std::endl;
				}

				{
					
					size_t emptySections=0;
					for (size_t k=0; k<NumSections; k++) {
						bool empty = true;
						for (size_t i=0; empty and (i<(1U<<SectionSize)); i++)
							for (size_t j=0; empty and j<(1U<<alphaStride); j++)
								empty = ((*this)(k*NumSections+i,j) == JumpIdx(-1));
						
						if (empty)
							emptySections++;
					}
						
					std::cerr << NumSections << " " << emptySections << " " << (100.*emptySections)/NumSections << std::endl;
				}
				
				//dedup();
			}
		};
		JumpTable jumpTable;
		
		JumpIdx start;
		std::vector<JumpIdx> emptyWords; //emptyWords are pointers to the victim dictionary.
		const Dictionary dict;

		Encoder(const Dictionary &dict_) :
			jumpTable(dict_.keySize, dict_.overlap, dict_.alphabet.size()),
			dict(dict_) { 
			
			size_t NumSections = 1<<dict.overlap;
			size_t SectionSize = 1<<dict.keySize;
			std::vector<std::map<Word, size_t>> positions(NumSections);

			// Init the mapping (to know where each word goes)
			for (size_t k=0; k<NumSections; k++)
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++)
					positions[k][dict[i]] = i;

			// Link each possible word to its continuation
			for (size_t k=0; k<NumSections; k++) {
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++) {
					Word word = dict[i];
					size_t wordIdx = i;
//					std::cerr << "start " << k << " " << i << " " << dict.size() << " " << word << std::endl;
					while (not word.empty()) {
						auto lastSymbol = word.back();						
						word.pop_back();
						size_t parentIdx;
						if (positions[k].count(word)) {
							parentIdx = positions[k][word];
						} else {
							std::cerr << (i%SectionSize) << "SHOULD NEVER HAPPEN" << std::endl;
							parentIdx = jumpTable.getNewPos();
						}
						jumpTable(parentIdx, lastSymbol) = wordIdx;
						wordIdx = parentIdx;
					}
				}
			}
						
			//Link between inner dictionaries
			for (size_t k=0; k<NumSections; k++) {
				for (size_t i=k*SectionSize; i<(k+1)*SectionSize; i++) {
					for (size_t j=0; j<dict.alphabet.size(); j++) {
						if (jumpTable(i,j)==JumpIdx(-1)) {
							if (positions[i%(1<<dict.overlap)].count(Word(1,Symbol(j)))) {
								jumpTable(i, j) = positions[i%(1<<dict.overlap)][Word(1,Symbol(j))] +
									FLAG_NEXT_WORD;
							} else { 
//								std::cerr << "k" << i << " " << j << std::endl;
								jumpTable(i, j) = positions[i%(1<<dict.overlap)][Word()] +
									FLAG_NEXT_WORD + 
									FLAG_INSERT_EMPTY_WORD;
							}
						}
					}
				}
			}
			
			
			// Fill list of empty words
			emptyWords.resize(NumSections,JumpIdx(-1));
			for (size_t k=0; k<NumSections; k++)
				emptyWords[k] = positions[k][Word()];

			// Get Starting Positions (not encoded)
			size_t victim = 0;
			while (not dict[victim].empty()) 
				victim++;
			victim = victim % (1<<dict.overlap);

			start = victim*SectionSize;
			while (not dict[start].empty()) 
				start++;

			jumpTable.clean(start, dict);
		}
		
		template<class TIN, typename TOUT, typename std::enable_if<sizeof(typename TIN::value_type)==1,int>::type = 0>		
		void encodeA(const TIN &in, TOUT &out) const {
			
			if (out.size() < 2*in.size()) out.resize(in.size());
			
			uint32_t *o = (uint32_t *)&*out.begin();
			uint32_t *oend = (uint32_t *)&*out.end();
			const uint8_t *i = (const uint8_t *)&in.front();
			const uint8_t *iend = i + in.size();
			
			//while (i<iend and iend[-1]==0) iend--;
			
			uint64_t value=0; int32_t bits=0;
			if (i<iend) {
				
				JumpIdx j0 = jumpTable(start, *i++);
				while (i<iend) {


					JumpIdx j1 = jumpTable(j0, *i++);
					
					if (j1 & FLAG_NEXT_WORD) {
						value <<= dict.keySize;
						bits += dict.keySize;
						value += j0 & ((1<<dict.keySize)-1);
						if (bits>=32) {
							if (o==oend) return;
							bits -= 32;
							*o++ = value>>bits;
						}

						if (j1 & FLAG_INSERT_EMPTY_WORD) {
							i--;
							//std::cerr << "We found an empty word!" << std::endl;
						}
					}
					j0=j1;
				}
				assert (not jumpTable.isIntermediate(j0)); //If we end in an intermediate node, we should roll back. Not implemented.
				value <<= dict.keySize;
				bits += dict.keySize;
				value += j0 & ((1<<dict.keySize)-1);

//std::cerr << (j0 & ((1<<dict.keySize)-1)) << " " << bits << std::endl;
				
				while (bits>0) {
					while (bits<32) {
						j0 = emptyWords[j0 % emptyWords.size()];
						value <<= dict.keySize;
						bits += dict.keySize;
						value += j0 & ((1<<dict.keySize)-1);

//std::cerr << (j0 & ((1<<dict.keySize)-1)) << " " << bits << std::endl;
					}
					if (o==oend) return;
					bits -= 32;
					*o++ = value>>bits;
				}
			}
//			std::cerr << out.size() << " " << ((uint8_t *)o-(uint8_t *)&out.front()) << std::endl;
			out.resize((uint8_t *)o-(uint8_t *)&out.front());
		}

		template<class TIN, typename TOUT, typename std::enable_if<sizeof(typename TIN::value_type)==1,int>::type = 0>		
		void operator()(const TIN &in, TOUT &out) const {
			if (dict.keySize==12) 
				encodeA(in,out);
			else
				encodeA(in,out);
		}
	};
	const Encoder encoderFast = Encoder(dictionary);
	
	struct EncoderSlow {
		
		const Dictionary W;
		
		EncoderSlow(const Dictionary &dict) : W(dict) {}
		
		template<typename T, typename IT>
		static bool areEqual(const T &obj, const IT it) {
			for (size_t i=0; i<obj.size(); i++)
				if (obj[i] != it[i])
					return false;
			return true;
		}

		template<class TIN, typename TOUT, typename std::enable_if<sizeof(typename TIN::value_type)==1,int>::type = 0>		
		void operator()(const TIN &in, TOUT &out) const {

			out.resize(in.size());
			out.resize(0);
			
			uint64_t value=0; int32_t bits=0;
			
			size_t lastWord = 0;
			while (not W[lastWord].empty()) 
				lastWord++;
				
			for (size_t i=0, longest=0; i<in.size(); i+=longest) {
				
				if (out.size()>out.capacity()-16) {
					std::cerr << "E!: " << in.size() << " " << out.size() << std::endl;
					return;
				}
				
				size_t remaining = in.size()-i;
				
				size_t best = 0;
				longest = 0;
				for (size_t j=0; j<W.size()>>W.overlap; j++) {
					size_t idx = (lastWord%(1<<W.overlap))*(W.size()>>W.overlap) + j;
					if (W[idx].size()>longest and W[idx].size()<= remaining and areEqual(W[idx],in.begin()+i) ) {
						best = idx;
						longest = W[idx].size();
					}
				}
				
				value <<= W.keySize;
				bits += W.keySize;
				value += best & ((1<<W.keySize)-1);
				lastWord = best;
//				std::cerr << best << ":" << bits << std::endl;

				if (bits>=32) {
					bits-=32;
					out.resize(out.size()+4);
					uint32_t *v = (uint32_t *)&*out.end();
					*--v = value>>bits;
				}
			}
			while (bits) {
				while (bits<32) {
					for (size_t j=0; j<W.size()>>W.overlap; j++) {
						size_t idx = (lastWord%(1<<W.overlap))*(W.size()>>W.overlap) + j;
						if (W[idx].empty()) {
							value <<= W.keySize;
							bits += W.keySize;
							value += idx & ((1<<W.keySize)-1);
							lastWord = idx;
//							std::cerr << idx << ":" << bits << std::endl;
							break;
						}
					}
				}
				bits-=32;
				out.resize(out.size()+4);
				uint32_t *v = (uint32_t *)&*out.end();
				*--v = value>>bits;
			}
			
//			std::cerr << "E: " << in.size() << " " << out.size() << std::endl;
		}
	};
	const EncoderSlow encoderSlow = EncoderSlow(dictionary);

	struct Decoder {

		const size_t keySize;     // Non overlapping bits of the word index in the big dictionary
		const size_t overlap;     // Bits that overlap between keys
		const size_t maxWordSize;
		
		size_t start;
		
		std::shared_ptr<DedupVector<Symbol>> dedupVector;
		
		std::vector<Symbol> decoderTable;
		template<typename T, size_t N, typename TIN, typename TOUT>
		void decodeA(const TIN &in, TOUT &out) const  {
			
			uint8_t *o = (uint8_t *)&out.front();
			const uint32_t *i = (const uint32_t *)in.data();
	
			uint64_t mask = (1<<(keySize+overlap))-1;
			const std::array<T,N>  *DD = dedupVector ? (const std::array<T,N> *)(*dedupVector)() : (const std::array<T,N> *)decoderTable.data();
			uint64_t v32 = start; int32_t c=-keySize;
			
			while (c>=0 or i<(const uint32_t *)&*in.end()) {
				
//				std::cerr << ((v32>>c) & mask) << ":" << c << std::endl;
				//endianmess
				if (c<0) {
					v32 = (v32<<32) + *i++;
					c   += 32;
//				std::cerr << ((v32>>c) & mask) << ":" << c << std::endl;
				}
				{				
					
//				std::cerr << ((v32>>c) & mask) << ":" << c << std::endl;
					const uint8_t *&&v = (const uint8_t *)&DD[(v32>>c) & mask];
					c -= keySize;
					for (size_t n=0; n<N; n++)
						*(((T *)o)+n) = *(((const T *)v)+n);
					o += v[N*sizeof(T)-1];
				}
			}
			out.resize(o-(uint8_t *)&out.front());	
		}
		
		template<typename T, typename TIN, typename TOUT>
		void decode12(const TIN &in, TOUT &out) const {
			
			uint8_t *o = (uint8_t *)&out.front();
			const uint32_t *i = (const uint32_t *)in.data();
			const uint32_t *iend = (const uint32_t *)&*in.end();
			
			uint64_t mask = (1<<(keySize+overlap))-1;
			
			const T *D = dedupVector ? (const T *)(*dedupVector)() : (const T *)decoderTable.data();
			uint64_t value = start; 
			if ( in.size()>12 ) {
				iend-=3;
				while (i<iend) {

					value = (value<<32) + *i++;
					{				
						T v = D[(value>>20 ) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>8) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					value = (value<<32) + *i++;
					{				
						T v = D[(value>>28) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>16) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>4) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					value = (value<<32) + *i++;
					{				
						T v = D[(value>>24) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>12) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>0) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
				}
				iend+=3;
			}
			int32_t c=-keySize;
			while (c>=0 or i<(const uint32_t *)&*in.end()) {
				if (c<0) {
					value = (value<<32) + *i++;
					c   += 32;
				}
				{				
					T v = D[(value>>c) & mask];
					c -= keySize;
					*((T *)o) = v;
					o += v >> ((sizeof(T)-1)*8);
				}
			}
			out.resize(o-(uint8_t *)&out.front());
		}


		template<typename T, typename TIN, typename TOUT>
		void decode16(const TIN &in, TOUT &out) const {
			
			uint8_t *o = (uint8_t *)&out.front();
			const uint32_t *i = (const uint32_t *)in.data();
			const uint32_t *iend = (const uint32_t *)&*in.end();
			
			uint64_t mask = (1<<(keySize+overlap))-1;
			
			const T *D = dedupVector ? (const T *)(*dedupVector)() : (const T *)decoderTable.data();
			uint64_t value = start; 
			if ( in.size()>12 ) {
				iend-=2;
				while (i<iend) {

					value = (value<<32) + *i++;
					{				
						T v = D[(value>>16 ) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>0) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					value = (value<<32) + *i++;
					{				
						T v = D[(value>>16 ) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
					{				
						T v = D[(value>>0) & mask];
						*((T *)o) = v;
						o += v >> ((sizeof(T)-1)*8);
					}
				}
				iend+=2;
			}
			int32_t c=-keySize;
			while (c>=0 or i<(const uint32_t *)&*in.end()) {
				if (c<0) {
					value = (value<<32) + *i++;
					c   += 32;
				}
				{				
					T v = D[(value>>c) & mask];
					c -= keySize;
					*((T *)o) = v;
					o += v >> ((sizeof(T)-1)*8);
				}
			}
			out.resize(o-(uint8_t *)&out.front());
		}
		
		Decoder(const Dictionary &dict) :
			keySize(dict.keySize),
			overlap(dict.overlap),
			maxWordSize(dict.maxWordSize) {
				
			start = 0;
			while (not dict[start].empty()) 
				start++;
				
			decoderTable.resize(dict.size()*(maxWordSize+1));
			for (size_t i=0; i<dict.size(); i++) {

				Symbol *d = &decoderTable[i*(maxWordSize+1)];
				d[maxWordSize] = dict[i].size();
				if (dict[i].size()>maxWordSize) {
					std::cerr << "WHAT?" << i << " " << dict[i].size() << " " << maxWordSize << std::endl;
				}
				assert(dict[i].size()<=maxWordSize);
				for (auto c : dict[i])
					*d++ = c;
			}
			
			if (configuration("dedup", enableDedup))
				dedupVector = std::make_shared<DedupVector<Symbol>>(decoderTable);
		}
		
		template<typename TIN, typename TOUT>
		void operator()(const TIN &in, TOUT &out) const {
			
			if (keySize==12) {
				switch (maxWordSize+1) {
					case   4: return decode12<uint32_t>(in, out);
					case   8: return decode12<uint64_t>(in, out);
				}
			} 
/*			if (keySize==16) {
				switch (maxWordSize+1) {
					case   4: return decode16<uint32_t>(in, out);
					case   8: return decode16<uint64_t>(in, out);
				}
			} */
			switch (maxWordSize+1) {
				case   4: return decodeA<uint32_t, 1>(in, out);
				case   8: return decodeA<uint64_t, 1>(in, out);
				case  16: return decodeA<uint64_t, 2>(in, out);
				case  32: return decodeA<uint64_t, 4>(in, out);
				case  64: return decodeA<uint64_t, 8>(in, out);
				case 128: return decodeA<uint64_t,16>(in, out);
				case 256: return decodeA<uint64_t,32>(in, out);
				case 512: return decodeA<uint64_t,64>(in, out);
				default: throw std::runtime_error ("unsupported maxWordSize");
			}
		}
	};
	const Decoder decoderFast = Decoder(dictionary);

	struct DecoderSlow {
		
		const Dictionary W;
		DecoderSlow(const Dictionary &dict) : W(dict) {}
			
		template<typename TIN, typename TOUT>
		void operator()(const TIN &in, TOUT &out) const  {
			
			out.resize(0);

			const uint32_t *i = (const uint32_t *)&*in.begin();
			const uint32_t *iend = (const uint32_t *)&*in.end();
			
			uint64_t value = 0;
			while (not W[value].empty()) 
				value++;
			uint32_t bits = 0;
			
			while (bits>0 or i<iend) {
				
				if (bits < W.keySize) {
					bits += 32;
					value = (value<<32) + *i++;
				}
				auto idx = (value>>(bits-W.keySize)) & ((1<<(W.keySize+W.overlap))-1);
//				std::cerr << idx << ":" << bits << std::endl;
				bits -= W.keySize;

				if (out.size()+W[idx].size() > out.capacity()) {
					std::cerr << "DECODE_WHAT: " << (out.size()+W[idx].size()) << " " <<  out.capacity() << std::endl;
					break;
				}					

				for (auto c : W[idx]) out.push_back(c);
			}
		}
		
	};	
	const DecoderSlow decoderSlow = DecoderSlow(dictionary);
	
	static std::map<std::string, double> &getConfigurationStructure() {
		static std::map<std::string, double> c;
		return c;
	}

	static double configuration(std::string name, double def) {
		auto &&c = getConfigurationStructure();
		if (not c.count(name)) c[name]=def;
		return c[name];
	}
	
public:

	static void clearConfiguration() { 
		getConfigurationStructure().clear();
	}

	static double configuration(std::string name) { 
		
		auto &&c = getConfigurationStructure();
		if (not c.count(name)) return 0.;
		return c[name];
	}
	
	static void setConfiguration(std::string name, double val) { 
		getConfigurationStructure()[name] = val; 
	}
	
	static double theoreticalEfficiency(const std::vector<double> &pdf, size_t keySize=12, size_t overlap=0, size_t maxWordSize = 1<<20) {
		Dictionary dictionary(pdf, keySize, overlap, maxWordSize);
		return dictionary.calcEfficiency();
	}

	static std::pair<double,size_t> theoreticalEfficiencyAndUniqueWords(const std::vector<double> &pdf, size_t keySize=12, size_t overlap=0, size_t maxWordSize = 1<<20) {
		Dictionary dictionary(pdf, keySize, overlap, maxWordSize);
		
		double efficiency = dictionary.calcEfficiency();
		
		std::sort(dictionary.begin(), dictionary.end());
		size_t uniqueCount = std::unique(dictionary.begin(), dictionary.end()) - dictionary.begin();
		
		return std::make_pair(efficiency, uniqueCount);
	}
	
	const double efficiency;

	Marlin2018Simple (const std::vector<double> &pdf, size_t keySize, size_t overlap, size_t maxWordSize)
		: 
		  dictionary(pdf, keySize, overlap, maxWordSize),
		  efficiency(dictionary.calcEfficiency())  {
	}

    Marlin2018Simple() = delete;
    Marlin2018Simple(const Marlin2018Simple& other) = delete;
    Marlin2018Simple& operator= (const Marlin2018Simple& other) = delete;

    Marlin2018Simple(Marlin2018Simple&& other) noexcept = default;
    Marlin2018Simple& operator= (Marlin2018Simple&& other) noexcept = default;

    /** Destructor */
    ~Marlin2018Simple() noexcept = default;

	std::map<std::string,double> benchmark(const std::vector<double> &pdf, size_t sz = 1<<20) const {
		
		std::map<std::string,double> results;
		
		struct TestTimer {
			timespec c_start, c_end;
			void start() { clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c_start); };
			void stop () { clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c_end); };
			double operator()() { return (c_end.tv_sec-c_start.tv_sec) + 1.E-9*(c_end.tv_nsec-c_start.tv_nsec); }
		};
		TestTimer tEncode, tDecode;
		
		auto testData = Distribution::getResiduals(pdf, sz);
		
		decltype(testData)  compressedData, uncompressedData;
		compressedData  .reserve(8*testData.size());
		uncompressedData.reserve(8*testData.size());

		compressedData.clear();
		encode(testData,compressedData);
		
		// Encoding bechmark
		tEncode.start();
		compressedData.clear();
		encode(testData,compressedData);
		tEncode.stop();			

		size_t encoderTimes = 1+(2/tEncode());
		tEncode.start();
		for (size_t t=0; t<encoderTimes; t++) {
			compressedData.clear();
			encode(testData,compressedData);
		}
		tEncode.stop();
		
		// Decoding benchmark
		uncompressedData.resize(testData.size());
		decode(compressedData,uncompressedData);
		decode(compressedData,uncompressedData);
		decode(compressedData,uncompressedData);

		tDecode.start();
//		uncompressedData.resize(testData.size());
		decode(compressedData,uncompressedData);
		tDecode.stop();

		size_t decoderTimes = 1+(2/tDecode());
		tDecode.start();
		for (size_t t=0; t<decoderTimes; t++) {
//			uncompressedData.resize(testData.size());
			decode(compressedData,uncompressedData);
		}
		tDecode.stop();

		// Speed calculation
		results["encodingSpeed"] = encoderTimes*testData.size()/tEncode()/(1<<20);
		results["decodingSpeed"] = decoderTimes*testData.size()/tDecode()/(1<<20);
		if (configuration("debug",debug)) 
			std::cerr << "Enc: " << results["encodingSpeed"] << "MiB/s Dec: " << results["decodingSpeed"] << "MiB/s" << std::endl;
		
		// Efficiency calculation
		results["shannonLimit"] = Distribution::entropy(pdf)/std::log2(pdf.size());
		results["empiricalEfficiency"] = results["shannonLimit"] / (compressedData.size()/double(testData.size()));
		if (configuration("debug",debug)) 
			std::cerr << testData.size() << " " << compressedData.size() << " " << efficiency <<  " " << results["empiricalEfficiency"] << " " << std::endl;
		

		if (testData!=uncompressedData) {

			std::cerr << testData.size() << " " << uncompressedData.size() << std::endl;

			for (size_t i=0; i<10; i++) { std::cerr << int(testData[i]) << " | "; } std::cerr << std::endl;
			for (size_t i=0; i<10; i++) { std::cerr << int(uncompressedData[i]) << " | "; } std::cerr << std::endl;

			for (size_t i=0,j=0; i<100000 and i<testData.size() and i<uncompressedData.size(); i++) {
				j = j*2+int(testData[i]==uncompressedData[i]);
				if (i%16==0)
					std::cerr << "0123456789ABCDEF"[j%16] << (i%(64*16)?"":"\n");
			}
			std::cerr << std::endl;
		}
		
		return results;
	}
	
		  
	template<typename TIN, typename TOUT>
	void encode(const TIN &in, TOUT &out) const { 
		if (configuration("encoderFast",true))
			encoderFast(in, out);
		else
			encoderSlow(in, out);
	}

	template<typename TIN, typename TOUT>
	void decode(const TIN &in, TOUT &out) const { 
		if (configuration("decoderFast",true))
			decoderFast(in, out); 
		else
			decoderSlow(in, out);
	}
};


