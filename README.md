# Capote
A portable SuperForth compiler designed with performance and interoperability in mind.

## TO INSTALL
- Download [the latest release](https://github.com/TheRealMichaelWang/Capote/releases/download/1.0/capote.exe)
- Ensure you have GCC installed
  - If you are using windows [install these prebuilt binaries](https://gnutoolchains.com/download/)
- Copy and paste [these examples](https://github.com/TheRealMichaelWang/Capote/tree/main/examples) into `capote.exe`'s working directory.
  - It is recommended that only the examples with the `.sf` extension be run. The `.txt` files are just some standard library files.

## TO RUN
Type the following
```
capote -s [sourceFile] -o [outputCFile]
```
where `[sourceFile]` is your superforth source file, 
and `[outputCFile]` is your outputted c file.

Then type
```
gcc [outputCFile] -Ofast
./a
```
