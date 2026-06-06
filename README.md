# elf-loader

This elf-loader supports loading and executing: static, pie and shared objects.

It supports X86-64, with common reallocations.

It doesn't support Environment vars for now.

It is used in the following matter: ./compiled-program "script path" {\*\*any number of parameters for the script}

Example - compile the elf loader with the make file and simple-read.c from examples : "./elf-loader ./simple-read {some file path e.g. ./LICENSE}"
