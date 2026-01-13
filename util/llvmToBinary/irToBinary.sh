#!/bin/bash

function compileFile() {
  filename=$(basename -- "$1")
  path=$(dirname -- "$1")
  extension="${filename##*.}"
  filename="${filename%.*}"

  mkdir -p "$path/compiled"

  echo "Generating LLVM IR"
  echo "Compiling $path/$filename.ll down to binary..."

  echo "Generating bytecode: $filename.bc"
  llvm-as "$path/$filename.ll" -o "$path/compiled/$filename.bc"

  echo "Generating object file: $filename.o"
  llc \
    -O0 \
    -fast-isel \
    -regalloc=fast \
    --dwarf64 \
    -filetype=obj \
    "$path/compiled/$filename.bc" \
    -o "$path/compiled/$filename.o"

  echo "Generating final binary: $path/compiled/$filename"

  clang++ \
    -O0 \
    -g \
    -fno-builtin \
    -fno-inline \
    -fno-vectorize \
    -fno-slp-vectorize \
    -fno-unroll-loops \
    "$path/compiled/$filename.o" \
    -o "$path/compiled/$filename"

  rm "$path/compiled/$filename.bc"
  rm "$path/compiled/$filename.o"
}

input="$1"

# --- Argument handling: directory, file, or filename ---
if [[ -d "$input" ]]; then
  # Input is a directory → compile all .ll files
  for f in "$input"/*.ll; do
    compileFile "$f"
  done

elif [[ -f "$input" ]]; then
  # Input is an actual file path → compile this .ll file
  compileFile "$input"

elif [[ -f "$input.ll" ]]; then
  # Input is a bare filename (like "select") → compile select.ll
  compileFile "$input.ll"

else
  echo "Error: '$input' is not a directory, file, or valid .ll source."
  exit 1
fi
