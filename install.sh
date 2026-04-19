#!/bin/bash

INSTALL_DIR="/usr/bin"
RESSOURCE_DIR="/etc/spear"
PROFILE_EXECTUON_COUNT=100000

: "${C:=/usr/bin/clang-17}"
: "${CXX:=/usr/bin/clang++-17}"

# Check the user id of the executing user -> ensures privileged rights
if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

# ---------------------------
# Check required CLI tools
# ---------------------------
required_tools=("llvm-as-17" "llc-17" "clang++-17" "cmake" "make" "python3" "z3" "phasar-cli" "bpftool")

for tool in "${required_tools[@]}"; do
  echo "Checking for dependency $tool..."
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: Dependency '$tool' is not installed or not in PATH."
    exit 1
  fi
done

# ---------------------------
# Check nlohmann/json
# ---------------------------
echo "Checking for headers of nlohmann/json..."

cat <<EOF | g++ -std=c++17 -xc++ - -o /dev/null >/dev/null 2>&1
#include <nlohmann/json.hpp>
int main() { return 0; }
EOF

if [[ $? -ne 0 ]]; then
  echo "Error: nlohmann/json is not installed or not reachable by the compiler."
  echo "Hint: install 'nlohmann-json3-dev' (Debian/Ubuntu)"
  exit 1
fi

# ---------------------------
# Install SPEAR
# ---------------------------
mkdir -p build
cd build || exit

echo "[1/6] Compiling SPEAR"
cmake .. -DCMAKE_C_COMPILER="$C" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld"
make -j"$(nproc)"

echo "[2/6] Copying spear to /usr/bin"
install spear "$INSTALL_DIR"

cd .. || exit

echo "[3/6] Create SPEAR directory"
mkdir -p "$RESSOURCE_DIR"

echo "[4/6] Copy SPEAR configuration file to /etc/spear"
cp defaultconfig.json "$RESSOURCE_DIR"/defaultconfig.json

echo "[5/6] Generate Profile Programs"
mkdir -p "$RESSOURCE_DIR"/profile/cpu
python3 util/profilegenerator/main.py "$RESSOURCE_DIR"/profile/cpu "$PROFILE_EXECTUON_COUNT"

echo "[6/6] Compiling profiling programs"
./util/llvmToBinary/irToBinary.sh "$RESSOURCE_DIR"/profile/cpu

echo "[DONE] SPEAR was installed successfully!"