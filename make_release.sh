#!/bin/bash
# make_release.sh: make a clean build of the project in ./Release
# 
# Options:
#   -v: Be verbose when compiling and linking
#   -s: Make a static release that does not depend on the BMI2 instruction set (suitable for the AMD Jaguar architecture)
# 
# MIT License
# 
# Copyright (c) 2018 Manuel Martinez Torres, portions by Miguel Hernández-Cabronero
# 
# Marlin: A Fast Entropy Codec
# 
# MIT License
# 
# Copyright (c) 2018 Manuel Martinez Torres
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

cd ext
make
cd ..

rm -rf Release
mkdir Release
cd Release

verbo=""
static=""
while (( "$#" )); do
    if [ "$1" == "-v" ]; then
        verbo="-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON"
    fi
    
    if [ "$1" == "-s" ]; then
        static="-DCMAKE_STATIC:BOOL=ON"
    fi
    
    shift
done

cmake -DCMAKE_BUILD_TYPE=Release ${verbo} ${static} ..
make

