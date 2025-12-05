import argparse
import os
from pathlib import Path

from profilegenerator.generator import Generator
from enum import Enum

from profilegenerator.definitions import worklists


class Domain(Enum):
    CPU = 1,
    DRAM = 2



def main():
    parser = argparse.ArgumentParser(description="Process a path input.")
    parser.add_argument("path", type=Path, help="Path to a file or directory")
    parser.add_argument("repetitions", type=int, help="Amount of instruction repetitions")

    args = parser.parse_args()
    input_path = args.path
    reps = args.repetitions


    if reps > 0:
        for wi in worklists:
            worklistpath = os.path.join(input_path, wi.subpath)
            # Always create the directory if needed
            input_path.mkdir(parents=True, exist_ok=True)
            gen = Generator(wi.instructions, worklistpath, reps)
            gen.generate()
    else:
        print("Repetitions must be greater than 0")

if __name__ == "__main__":
    main()