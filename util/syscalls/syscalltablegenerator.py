#!/usr/bin/env python3

#  Copyright (c) 2026 Maximilian Krebs
#  All rights reserved.

"""
This script generates a C++ header file containing a mapping of syscall IDs to their names, based on the definitions
in a given unistd.h header file. The generated header provides two functions: one for getting the syscall name
from its ID, and another for getting the syscall ID from its name.
"""

import re
import sys
from pathlib import Path

def main():
    if len(sys.argv) != 3:
        print("usage: generate_syscall_table.py <unistd_header> <output_header>", file=sys.stderr)
        sys.exit(1)

    input_file = Path(sys.argv[1])
    output_file = Path(sys.argv[2])

    if not input_file.exists():
        print(f"error: input file not found: {input_file}", file=sys.stderr)
        sys.exit(1)

    # Parse the syscall names from the unistd.h header. We look for lines that match the pattern #define __NR_<name> <id>.
    pattern = re.compile(r'^\s*#define\s+__NR_([A-Za-z0-9_]+)\s+([0-9]+)\s*$')

    entries = {}
    max_id = -1

    with input_file.open("r", encoding="utf-8", errors="ignore") as f:
        # We encounter a definition of each line of the file
        for line in f:
            m = pattern.match(line)
            if not m:
                continue

            name = m.group(1)
            syscall_id = int(m.group(2))

            entries[syscall_id] = name
            max_id = max(max_id, syscall_id)

    if max_id < 0:
        print(f"error: no syscall definitions found in {input_file}", file=sys.stderr)
        sys.exit(1)

    output_file.parent.mkdir(parents=True, exist_ok=True)

    # Generate the C++ header file with the syscall mapping
    with output_file.open("w", encoding="utf-8") as out:
        out.write("// Auto-generated. Do not edit.\n")
        out.write("#pragma once\n\n")
        out.write("#include <array>\n")
        out.write("#include <string_view>\n\n")
        out.write(f"inline constexpr int kMaxSyscallId = {max_id};\n\n")

        # We create an array of strings where the index corresponds to the syscall ID. If a syscall ID is not defined,
        # we leave it as an empty string.
        out.write("inline constexpr std::array<std::string_view, kMaxSyscallId + 1> kSyscallNames = {\n")

        for i in range(max_id + 1):
            name = entries.get(i, "")
            out.write(f'    "{name}",\n')

        out.write("};\n\n")

        # Create the helper functions
        out.write("inline constexpr std::string_view getSyscallName(int id) {\n")
        out.write("    if (id < 0 || id > kMaxSyscallId) {\n")
        out.write('        return \"unknown\";\n')
        out.write("    }\n")
        out.write("    if (kSyscallNames[id].empty()) {\n")
        out.write('        return \"unknown\";\n')
        out.write("    }\n")
        out.write("    return kSyscallNames[id];\n")
        out.write("}\n")

        out.write("inline int getSyscallId(std::string_view name) {\n")
        out.write("    for (int i = 0; i <= kMaxSyscallId; ++i) {\n")
        out.write("        if (kSyscallNames[i] == name) {\n")
        out.write("            return i;\n")
        out.write("        }\n")
        out.write("    }\n")
        out.write("    return -1;\n")
        out.write("}\n")

if __name__ == "__main__":
    main()