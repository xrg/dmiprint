# DMIprint

A minimal SMBIOS (aka. DMI) decoder, purposed to print the "designator" of
PCIe slots in the system.
Slot types of PCI (currently, as in SMBIOS(tm) v3.8.0) are hard-coded there.

Explicitly barebone: won't decode anything else.

Build:

```
    cd src
    make dmiprint
```

