# Tools

## Building
Simply run `make` to build all of the tools here. They are simple C programs that rely on no external libraries.


## pin\_predictor
For testing purposes, the `pin_predictor` tool can be used to output every combination of every possible seed. Useful for jumping through to specific stages, verifying the PRNG transformation works as expected, etc.

It is just run with `./pin_predictor` and will output the 4 PIN associated with each stage for every seed from 0x00 through 0xFF. Note that seed 0x00 is invalid during runtime of the Demo itself. All four of the stages have the same pin of `0000`.


## string\_packer
In order to fit all of the text in to the PIC that is in there the characters all needed to be packed two to a single word. The PIC12F1572 uses 14-bit instruction words. With 7-bit ASCII this allows for storing two characters in each instruction word. The full 14-bit value of the instruction word can be arbitrarily loaded with assembly directives. However, when using strings in C, they are packed as a single character per word.

This tool was created to take a listing of plaintext via stdin (all of which are maintained in the `texts/` directory) and output very specifically formatted text that can be added to the project as an assembly include. The expected input format is:

```
<name_of_array>
Text to put in.\r\n
...
```

And the tool will output via stdout:

```
PSECT strings,class=CODE,local,delta=2
GLOBAL _<name_of_array>
_<name_of_array>:
DW      0x... ; "XX"
...
```

The \<name\_of\_array\> names are critical as they must align with expected names in the C sources.

The `string_packer` tool will accept the most common escape sequences, `\n`, `\r`, `\t`, and `\\`. If an invalid escape is used, the program will fail an spit out the byte that it tripped on.

In the assembly output, the two represented characters will be appended to the line as a comment, for example:

`DW      0x2A68 ; "Th"`

Since all strings must end on a `\0` byte, `string_packer` adds either a single 0x00 to the lower byte of an instruction word, or a full instruction word of 0x0000. Adding a `\0` at the end of the plaintext is not needed and will cause errors. Example output of this process:

```
...
DW      0x2E80 ; "]\0"
...
DW      0x0000 ; "\0\0"
...
```

The firmware on the PIC has a decode function to be able to print these packed strings. Since the `string_packer` added `\0` bytes at the end of each string, the print function in the PIC will read byte by byte until it encounters a `\0` byte like a normal string print function.

The `string_packer` tool can be run with `./string_packer < textfile.txt >> strings.s`. The order of the text files to be processed does not matter, but it is most logical to keep them in order that they are printed.
