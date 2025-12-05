
from .instruction import Instruction
from .worklistitem import WorklistItem

cpu_worklist = WorklistItem( [
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
], "cpu")

dram_worklist = WorklistItem( [
    Instruction.complex_instruction('load', 'load i64, i64* %ptr, align 8\n  call void asm sideeffect "", "r"(i64 COUNTER)', '@buffer = global [134217728 x i8] zeroinitializer, align 64', '', ' %base_i8 = getelementptr inbounds [134217728 x i8], [134217728 x i8]* @buffer, i64 0, i64 0\n  %ptr = bitcast i8* %base_i8 to i64*', True),
    Instruction.complex_instruction('store', 'store i64 %val, i64* %ptr, align 8', '@buffer = global [134217728 x i8] zeroinitializer, align 64\n  @store_value = constant i64 1311768467463790320', '', ' %base_i8 = getelementptr inbounds [134217728 x i8], [134217728 x i8]* @buffer, i64 0, i64 0\n %ptr = bitcast i8* %base_i8 to i64*\n  %val = load i64, i64* @store_value\n', False),

], "dram")


worklists = [ cpu_worklist, dram_worklist ]