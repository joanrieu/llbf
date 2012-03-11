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
`-version` Display the version of this program

Examples
--------

Run a program:
`llbf program.bf -o program.ll && lli program.ll`

Run a program that doesn't need input:
`llbf program.bf | lli`

Compile a program to a native executable:
`llbf program.bf | llc -o program.s && clang program.s -o program`
