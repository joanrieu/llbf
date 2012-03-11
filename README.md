llbf
====

Description
-----------

`llbf` is a Brainfuck to LLVM IR compiler.

Usage
-----

`llbf [options] <input file>`

Options
-------

`-help` Display available options  
`-o=<filename>` Specify output filename  
`-run` Run the program  
`-S` Write output in LLVM intermediate language (instead of bitcode)  
`-version` Display the version of this program  

Examples
--------

Run a program:
`llbf -run program.bf`

Compile a program to native assembly language (you can create an executable by running your usual compiler on the .s file):
`llbf program.bf | llc -o program.s`
