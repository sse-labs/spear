from .instruction import Instruction

from typing import List

class WorklistItem:
    instructions: List[Instruction]
    subpath: str

    def __init__(self, instructions, subpath):
        self.instructions = instructions
        self.subpath = subpath