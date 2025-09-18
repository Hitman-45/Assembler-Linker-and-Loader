# Assembler-Linker-and-Loader
Assembler Linker and Loader for Computer System Design

# Project Overview

This project involves breaking down a problem into a logical flow, implementing it in Python, and then converting it to C++ for better performance and system-level execution. The process is divided into three main parts:

How to run it:
```
g++ -std=c++17 -O2 -o vmasm vm_asm.cpp
./vmasm assemble examples/add.vmasm -o build/abc.vmo
./vmasm dump build/abc.vmo
```

### VMLINK

The vmlink tool is responsible for combining multiple assembler-generated object files (.vmo) into a single executable program (.vmc). Each .vmo file contains machine code, a local symbol table, and relocation records for unresolved addresses. When invoked, the linker first reads all input object files and constructs a global symbol table, which maps symbol names to their final addresses in the merged program. It then performs consistency checks, ensuring that no symbol is defined more than once and reporting errors for undefined references. After validation, the linker processes all relocation entries, patching the correct addresses wherever labels or external references were used. Finally, it concatenates the machine code sections from each object file, adjusted with relocations, and writes the resulting binary to a .vmc executable. In essence, vmlink resolves inter-file dependencies, ensures symbol correctness, and produces a single runnable program for the virtual machine environment.

### Work Done As of 6th-Sep-2025.

- Removed the outer while(changes != 0) loop → Your code was repeatedly scanning the array, which was unnecessary and could lead to inefficiency. Now, the logic processes collisions in a single pass using a stack.

- Introduced a stack (vector<int> st) → This directly models the collision process: push if no collision, pop if collision, and resolve until stable.

- Handled equal-size collisions (num1 + num2 == 0) → Both asteroids get destroyed correctly, which your code didn’t handle cleanly before.

- Simplified direction check → Only when a positive asteroid is followed by a negative one, collisions are considered. This avoids unnecessary checks.

- Returned the stack as result → Ensures only surviving asteroids are output in the correct order.

- Effect: The modified code runs in O(n) time with a clean stack-based simulation, handles all edge cases (like multiple collisions in a row or equal magnitudes), and produces the correct surviving asteroid configuration.

### Work Done As of 25th-Aug-2025.

## Part 1 – Flowchart Design (CS22B034 Lavkush)

- **Flowchart Diagram**: Designed a flowchart that represents the logical flow of the problem.
- **Sequential Breakdown**: Broke down the process into sequential steps:
  - Start
  - Input
  - Processing
  - Output
  - End
- **Control Flow**: Ensured that the flowchart captures control flow, decision-making, and iterations, if any.
- **Visual Representation**: Represented the problem visually to make the later coding steps clear and structured.

## Part 2 – Python Implementation (CS22B005 Shankhesh)

- **Python Translation**: Translated the flowchart logic into Python code.
- **Function Design**: Wrote functions to represent each step of the flowchart.
- **Input Handling**: Ensured proper handling of inputs, conditions, and outputs.
- **Validation**: Validated the Python implementation by testing it against example inputs.
- **Prototype**: The Python version served as a prototype implementation for the logic.

## Part 3 – Conversion to C++ (CS22B043 Preet)

- **Conversion to C++**: Converted the Python program into C++.
- **Standard I/O**: Used standard input/output (`cin` / `cout`) instead of Python I/O.
- **C++ Syntax**: Applied C++ syntax for loops, conditionals, and functions while maintaining identical logic.
- **Main Function**: Structured the program with `main()` and modularized functions to reflect the flowchart steps.
- **System-Level Performance**: This conversion made the solution faster and more suitable for system-level execution.
