#!/bin/bash



function compileFile() {
  filename=$(basename -- "$1")
  path=$(dirname -- "$1")
  extension="${filename##*.}"
  filename="${filename%.*}"



  mkdir -p "$path/compiled"

  echo "Generating the llvm-IR"
  echo "Compiling the file $path/$filename.ll downto binary..."

  echo "Generating the bytcodefile $filename.bc"
  llvm-as "$path/$filename.ll" -o "$path/compiled/$filename.bc"
  echo "Generating the objectfile $filename.o"
  llc -O0 --dwarf64 -filetype=obj "$path/compiled/$filename.bc"
  echo "Generating the binary $path/compiled/$filename"

  clang++ -O0 -g "$path/compiled/$filename.o" -o "$path/compiled/$filename"

  rm "$path/compiled/$filename.bc"
  rm "$path/compiled/$filename.o"
}


for f in $1/*.ll
do
  compileFile "$f"
done
