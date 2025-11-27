from typing import List
from .util import Util


class Instruction:
    opcode: str
    type: str
    args: List[str]
    sideeffecttype: str = None
    is_complex: bool
    cpx_exec_block: str
    cpx_header_block: str
    cpx_pretext: str
    cpx_footer_block: str
    cpx_use_counter: bool

    def __init__(self, opcode: str, type: str, args: List[str], sideeffecttype: str = None):
        self.opcode = opcode
        self.type = type
        self.args = args
        self.is_complex = False

        if sideeffecttype is not None:
            self.sideeffecttype = sideeffecttype


    @classmethod
    def complex_instruction(cls, opcode: str,  executionblock: str, header: str, footer: str, pretext: str = "", preceed_with_counter: bool = False):
        inst = cls(opcode, "", [])
        inst.is_complex = True
        inst.cpx_exec_block = executionblock
        inst.cpx_header_block = header
        inst.cpx_footer_block = footer
        inst.cpx_pretext = pretext
        inst.cpx_use_counter = preceed_with_counter
        return inst


    def get_opcode(self):
        return self.opcode

    def get_type(self):
        return self.type

    def get_args(self):
        return Util.to_comma_separated(self.args)