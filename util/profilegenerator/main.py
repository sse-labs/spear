import argparse
from pathlib import Path

from profilegenerator.instruction import Instruction
from profilegenerator.generator import Generator

worklist = [
    Instruction.complex_instruction('_noise', '', '', ''),
    Instruction("add", "i32", ["42", "311"]),
    Instruction("fadd", "float", ["42.0", "311.0"]),
    Instruction("fdiv", "float", ["42.0", "311.0"]),
    Instruction.complex_instruction('call', 'call void @_Z3foov()\n  call void asm sideeffect "", "~{memory}"()', 'define void @_Z3foov() #0 {\n  ret void\n}', ''),
    Instruction.complex_instruction('getelementptr', 'getelementptr [4 x i8], [4 x i8]* @array, i32 0, i32 2\n  call void asm sideeffect "", "r"(i8* COUNTER)', '@array = global [4 x i8] zeroinitializer\n@ptr_sink = global i8* null', 'store volatile i8* %1, i8** @ptr_sink',"", True),
    Instruction.complex_instruction('load', 'load volatile i8, i8* @global_src', '@global_src = global i8 17', '', "",True),
    Instruction("mul", "i32", ["42", "3"]),
    Instruction("fmul", "float", ["42.0", "3.0"]),
    Instruction("or", "i32", ["42", "311"]),
    Instruction("and", "i32", ["42", "311"]),
    Instruction("xor", "i32", ["42", "311"]),
    Instruction("br", "i32", []),
    Instruction("frem", "float", ["42.0", "3.0"]),
    Instruction("urem", "i32", ["42", "3"]),
    Instruction("sdiv", "i32", ["42", "3"]),
    Instruction.complex_instruction("select", 'select i1 true, i64 17, i64 42\n  call void asm sideeffect "", "r"(i64 COUNTER)', "@global = global i64 0", "store volatile i64 %1, i64* @global","", True),
    Instruction.complex_instruction("sext", 'sext i32 257 to i64\n  call void asm sideeffect "", "r"(i64 COUNTER)', "@global = global i64 0", "store volatile i64 %1, i64* @global","", True),
    Instruction.complex_instruction("zext", 'zext i32 257 to i64\n  call void asm sideeffect "", "r"(i64 COUNTER)', "@global = global i64 0", "store volatile i64 %1, i64* @global", "", True),
    Instruction("icmp ne", "i32", ["252", "42"], "i1"),
    Instruction("icmp sge", "i32", ["252", "42"], "i1"),
    Instruction("icmp sgt", "i32", ["252", "42"], "i1"),
    Instruction("icmp sle", "i32", ["252", "42"], "i1"),
    Instruction("icmp slt", "i32", ["252", "42"], "i1"),
    Instruction("icmp uge", "i32", ["252", "42"], "i1"),
    Instruction("icmp ule", "i32", ["252", "42"], "i1"),
    Instruction("icmp ult", "i32", ["252", "42"], "i1"),
    Instruction("shl", "i32", ["42", "1"]),
    Instruction("lshr", "i32", ["42", "1"]),
    Instruction("srem", "i32", ["42", "3"]),
    Instruction.complex_instruction('store', 'store volatile i8 17, i8* @global_dst', '@global_dst = global i8 0', '',"", False),
    Instruction("sub", "i32", ["42", "311"]),
    Instruction("fsub", "float", ["42.0", "311.0"]),
    Instruction("udiv", "i32", ["42", "3"]),
]

def main():
    parser = argparse.ArgumentParser(description="Process a path input.")
    parser.add_argument("path", type=Path, help="Path to a file or directory")
    parser.add_argument("repetitions", type=int, help="Amount of instruction repetitions")

    args = parser.parse_args()
    input_path = args.path
    reps = args.repetitions

    # Always create the directory if needed
    input_path.mkdir(parents=True, exist_ok=True)


    if reps > 0:
        gen = Generator(worklist, input_path, reps)
        gen.generate()
    else:
        print("Repetitions must be greater than 0")

if __name__ == "__main__":
    main()