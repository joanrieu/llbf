llbf
====

Description
-----------

`llbf` is a Brainfuck compiler with JIT support based on LLVM.

Usage
-----

`llbf [options] <input file>`

Options
-------

`-help` Display available options  
`-o <filename>` Specify output filename  
`-run` Run the program  
`-S` Write output in LLVM intermediate language (instead of bitcode)  
`-f` Enable binary output on terminals  

Examples
--------

Run a program:

    llbf -run program.bf

Compile a program to native assembly language (you can create an executable by running your usual compiler on the .s file):

    llbf program.bf | llc -o program.s
