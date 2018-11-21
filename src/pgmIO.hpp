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

#ifndef PGMIO_HPP
#define PGMIO_HPP

#include <string>
#include <vector>

namespace marlin {

/**
 * Read a PGM (P5, binary) file into out and save its width and height.
 *
 * Only 8-bit images are supported at this point.
 */
void readPGM(const std::string& input_path, std::vector<uint8_t>& out, size_t& width, size_t& height);

/**
 * Write a PGM (P5, binary) into output_path and save its width and height.
 *
 * Only 8-bit images are supported at this point.
 */
void writePGM(std::vector<uint8_t>& data, const std::string& output_path, size_t width, size_t height);

}


#endif /* PGMIO_HPP */
