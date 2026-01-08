from typing import List
from .instruction import Instruction
from pathlib import Path
from .util import Util

class Generator:
    instlist: List[Instruction]
    baseloc: str
    repetitions: int

    def __init__(self, instlist, baseloc, reps):
        self.instlist = instlist
        self.baseloc = baseloc
        self.repetitions = reps

    def generate(self):
        for inst in self.instlist:
            opcode = inst.get_opcode()
            ty = inst.get_type()
            args = inst.get_args()
            tdefault = Util.get_type_default(ty)

            filename = Path(self.baseloc) / f"{opcode}.ll"

            if opcode == "br":
                with open(filename, "w") as f:
                    f.write(f'@global = global i32 0\n\n')
                    f.write('define i32 @main() #0 {\n')
                    f.write('entry:\n')
                    f.write('  br label %block0\n\n')

                    # Generate unrolled blocks
                    for i in range(0, self.repetitions + 1):
                        f.write(f'block{i}:\n')
                        f.write(f'  %v{i} = and i32 42, 311\n')
                        f.write(f'  br i1 true, label %then{i}, label %else{i}\n\n')

                        f.write(f'then{i}:\n')
                        f.write(f'  store volatile i32 %v{i}, i32* @global\n')
                        # jump to next block or exit
                        if i < self.repetitions:
                            f.write(f'  br label %block{i+1}\n\n')
                        else:
                            f.write(f'  br label %end\n\n')

                        f.write(f'else{i}:\n')
                        f.write(f'  store volatile i32 %v{i}, i32* @global\n')
                        if i < self.repetitions:
                            f.write(f'  br label %block{i+1}\n\n')
                        else:
                            f.write(f'  br label %end\n\n')

                    f.write('end:\n')
                    f.write('  ret i32 0\n')
                    f.write('}\n')

                continue  # IMPORTANT: skip normal codegen path

            if opcode == "frem":
                with open(filename, "w") as f:
                    # Two globals used for volatile loads
                    f.write(f'@c42 = global {ty} {args.split(",")[0]}\n')
                    f.write(f'@c3  = global {ty} {args.split(",")[1]}\n')

                    f.write('define i32 @main() #0 {\n')
                    f.write('entry:\n')

                    # Emit N+1 frem operations, each with fresh volatile loads
                    for i in range(0, self.repetitions + 1):
                        f.write(f'  %x{i} = load volatile {ty}, {ty}* @c42\n')
                        f.write(f'  %y{i} = load volatile {ty}, {ty}* @c3\n')
                        f.write(f'  %r{i} = frem {ty} %x{i}, %y{i}\n')
                        f.write(f'  call void asm sideeffect "", "x"({ty} %r{i})\n\n')

                    # Optional: store result to avoid DCE (not needed, but harmless)
                    f.write(f'  store volatile {ty} %r0, {ty}* @c42\n')
                    f.write('  ret i32 0\n')
                    f.write('}\n')

                continue  # IMPORTANT: skip normal codegen path

            # -----------------------------------------------------------------
            # NORMAL BEHAVIOR FOR OTHER OPCODES
            # -----------------------------------------------------------------
            if not inst.is_complex:
                with open(filename, "w") as f:
                    if inst.sideeffecttype is not None:
                        f.write(f'@global = global {inst.sideeffecttype} {tdefault}\n')
                    else:
                        f.write(f'@global = global {ty} {tdefault}\n')

                    f.write('define i32 @main() #0 {\n')
                    f.write('entry:\n')

                    for i in range(0, self.repetitions + 1):
                        f.write(f'  %{i} = {opcode} {ty} {args}\n')

                        if inst.sideeffecttype is not None:
                            f.write(f'  call void asm sideeffect "", "r"({inst.sideeffecttype} %{i})\n')
                        else:
                            f.write(f'  call void asm sideeffect "", "r"({ty} %{i})\n')

                    if inst.sideeffecttype is not None:
                        f.write(f'  store volatile {inst.sideeffecttype} %1, {inst.sideeffecttype}* @global\n')
                    else:
                        f.write(f'  store volatile {ty} %1, {ty}* @global\n')

                    f.write('  ret i32 0\n')
                    f.write('}\n')

            else:
                with open(filename, "w") as f:
                    f.write(f'{inst.cpx_header_block}\n\n')
                    f.write('define i32 @main() #0 {\n')
                    f.write('entry:\n')

                    f.write(inst.cpx_pretext)

                    for i in range(0, self.repetitions + 1):
                        block = inst.cpx_exec_block
                        if inst.cpx_use_counter:
                            if "COUNTER" in block:
                                block = block.replace("COUNTER", f"%{i}")
                            f.write(f'  %{i} = {block}\n')
                        else:
                            f.write(f'  {block}\n')

                    f.write(f'  {inst.cpx_footer_block}\n')
                    f.write('  ret i32 0\n')
                    f.write('}\n')
