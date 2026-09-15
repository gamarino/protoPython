#!/bin/bash
cd "$(dirname "$0")/../build_release"
./src/runtime/protopy -c 'import tokenize'
./src/runtime/protopy -c 'import annotationlib'
./src/runtime/protopy -c 'import codecs'
./src/runtime/protopy -c 'import token'
