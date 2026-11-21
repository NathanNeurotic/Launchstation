#ifndef LIBRARIES_LZARI_H
#define LIBRARIES_LZARI_H

// Compress in to out using LZARI. Returns final compressed size, or -1 after
// output exhaustion/input failure; a failed call may leave partial output.
int lzari(unsigned char *in, int insz, unsigned char *out, int outsz);
// Deompress in to out using LZARI. Return final decompressed size.
int unlzari(unsigned char *in, int insz, unsigned char *out, int outsz);

#endif
