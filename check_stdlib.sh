#!/bin/bash
cd /home/gamarino/Documentos/proyectos/protoPython/build
./src/runtime/protopy -c 'import tokenize'
./src/runtime/protopy -c 'import annotationlib'
./src/runtime/protopy -c 'import codecs'
./src/runtime/protopy -c 'import token'
