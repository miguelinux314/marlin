/***********************************************************************

pgmIO: simple I/O support for 8-bit PGM P5 (binary) files

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

#include "pgmIO.hpp"

#include <iostream>
#include <sstream>
#include <fstream>

namespace marlin {

void readPGM(const std::string &input_path, std::vector<uint8_t> &out, size_t &width, size_t &height) {
	std::ifstream infile(input_path);
	if (! infile.good()) {
		std::stringstream ss;
		ss << "Error! Cannot read input file " << input_path;
		throw std::runtime_error(ss.str());
	}

	std::string readLine;
	// Read header
	while (true) {
		getline(infile, readLine);
		if (readLine[0] == '#') {
			continue;
		}
		if (readLine.compare("P5") != 0) {
			throw std::runtime_error("Error! Only P5 PGM files are supported by readPGM");
		}
		break;
	}

	// Read dimensions
	while (true) {
		getline(infile, readLine);
		if (readLine[0] == '#') {
			continue;
		}
		std::stringstream ss(readLine);
		ss >> width >> height;
		if (width <= 0 || height <= 0) {
			std::cerr << "Read with=" << width << "; read height=" << height << std::endl;
			throw std::runtime_error("Error! Invalid dimensions specified in file");
		}
		break;
	}

	// Read maximum value
	while (true) {
		getline(infile, readLine);
		if (readLine[0] == '#') {
			continue;
		}
		std::stringstream ss(readLine);
		int max_value;
		ss >> max_value;
		if (max_value > 255 or max_value < 1) {
			throw std::runtime_error("Error! Invalid maximum value. "
							"Only 255 (8-bit images) is currently supported");
		}
		break;
	}

	// Read actual data
	size_t pos_before = infile.tellg();
	out.resize(width*height);
	uint8_t* out_data = out.data();
	for (size_t r=0; r<height; r++) {
		for (size_t c=0; c<width; c++) {
			infile.read((char*) out_data, 1);
			out_data++;
		}
	}
	size_t pos_after = infile.tellg();
	if (pos_after - pos_before != width*height) {
		std::stringstream ss;
		ss << "Error! Read " << pos_after - pos_before << "bytes; "
		   << "expected " << width*height << std::endl;
		throw std::runtime_error(ss.str());
	}
}

void writePGM(std::vector<uint8_t>& data, const std::string& output_path, size_t width, size_t height) {
	if (data.size() != width*height) {
		throw std::runtime_error("Error! data.size() does not match width*height");
	}

	std::ofstream outfile(output_path);
	if (! outfile.good()) {
		throw std::runtime_error("Error! Cannot open output_path for writing");
	}

	outfile << "P5\n" << width << " " << height << "\n255\n";
	outfile.write((char*)data.data(), width*height);
	if (!outfile.good()) {
		throw std::runtime_error("Error! Cannot write all data to output_path");
	}
	outfile.close();
}

}