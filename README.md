# DMIprint

A minimal SMBIOS (aka. DMI) decoder, purposed to print the "designator" of
PCIe slots in the system.

Explicitly barebone: won't decode anything else.

Build:

Just use your C compiler:
```
    gcc -o dmiprint -O2 src/dmiprint.c
```

