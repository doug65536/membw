# Memory bandwidth benchmark

Run these commands in terminal:

# It is going to create a directory named membw where you are
git clone https://github.com/doug65536/membw.git
cd membw
make

# To run it
make run

# To produce a windows executable from linux:
sudo apt install mingw-w64-common mingw-w64-tools binutils-mingw-w64 g++-mingw-w64-x86-64

# Produce membw.exe
make build-windows

You can build for linux, windows, x86_64 or arm (NEON).
It probably builds on almost any architecture, but it
will use horrible scalar instructions for measurement.
It really does make a big difference.

This code allows the CPU to fully take advantage of the prefetcher, the memory access pattern is very predictable and the memory controller has a lot of information to work with.
