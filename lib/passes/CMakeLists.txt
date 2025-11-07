
add_llvm_library(Energy MODULE
         ../../../main/include/JSON-Handler/JSONHandler.cpp
        ../../../main/include/JSON-Handler/JSONHandler.h
        ../../../main/include/Rapl-reader/PowercapReader.cpp
        ../../../main/include/Rapl-reader/PowercapReader.h
        ../../../main/include/LLVM-Handler/InstructionCategory.cpp
        ../../../main/include/LLVM-Handler/InstructionCategory.h
        ../../../main/include/LLVM-Handler/EnergyFunction.h
        ../../../main/include/LLVM-Handler/EnergyFunction.cpp
        ../../../main/include/LLVM-Handler/LLVMHandler.cpp
        ../../../main/include/LLVM-Handler/LLVMHandler.h
        ../../../main/include/Profiler/Profiler.cpp
        ../../../main/include/Profiler/Profiler.h
        ../../include/ProgramGraph/ProgramGraph.cpp
        ../../include/ProgramGraph/ProgramGraph.h
        ../../../main/include/LoopTree/LoopTree.cpp
        ../../../main/include/LoopTree/LoopTree.h
        ../../include/ProgramGraph/LoopNode.cpp
        ../../include/ProgramGraph/Node.cpp
        ../../include/ProgramGraph/Edge.cpp
        ../../../main/include/FunctionTree/FunctionTree.cpp
        ../../../main/include/FunctionTree/FunctionTree.h
       energy.cpp
        )

target_compile_options(Energy PUBLIC -fexceptions)