# Symbol Information

Opsian needs to know symbol information about a running process in order to provide  useful information to users about what is taking up time in their process. There are three key pieces of information that we need to know:

 * Function + Ocaml Module names
 * Line numbers
 * Source code files
 * Optionally binary files if we're missing debug symbols.

## Debug Information Types

### ELF

ELF defines the file format for linux object code and libraries. It includes a symbol table with useful information for function names, source file names and line numbers. Unfortunately Ocaml doesn't seem to generate ELF symbol information.

### DWARF

DWARF is a debugging information format that complements Elf. Dwarf information is produced by the Ocaml compiler and is our primary source of symbol information.  

## Libraries

We don't want to write a custom parser for dwarf or elf information so we use a combination of libbacktrace and libdl to obtain this information. 

### libbacktrace

BSD License, maintained, pure C library that can lookup both dwarf and elf information. Works ok in testing for native code libraries with both C and C++. Its pcinfo function doesn't lookup Ocaml function names but syminfo works so we end up using both.

### libdl

If we can't find debug symbols we use libdl in order to find and report the binary file name that the function is defined within. This can be used to help inform users of the correct distro package to install symbols from.

### elfutils / libdwfl

This is a good library and seemed to work ok but had licensing issues for us as we link our profiler against customer codebases and want non-copyleft libraries so as to avoid our customers being forced to open source all of their code.

### libdwarf

Old but maintained library. License ok. Operates at too low a level to be ideal for us. Example that looks up symbol information from addresses was 1200 lines of code.

### LLVM

LLVM has libraries for reading and writing symbol information. The license would work for us but because its all written in C++ using this library would block our efforts to minimise dependencies and migrate to a pure C codebase.
