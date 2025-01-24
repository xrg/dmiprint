#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <endian.h>
#include <sys/mman.h>


int do_debug = 0;

#define DEBUG(format, ...) \
    { if (do_debug) fprintf(stderr, "DEBUG:" format "\n" , ##__VA_ARGS__); }

struct sm_header {
    char len;
    char version[3];

    uint32_t st_len;
    uint64_t st_addr;
};

struct sm_entry {
    unsigned char type;
    unsigned int len;
    uint16_t handle;

    const void *start;

    const char *strings[256];
};

struct pci_bdf {
    int all;
    uint16_t segment;
    unsigned char bus, dev, fn;
};

struct sm_header read_entrypoint(int fd) {
    int rlen = 0;
    struct sm_header ret = {0};
    char buf[64];
    char *p = buf;
    unsigned char ep_len = 0;

    int mode64bit = 0;

    if (read(fd, buf, 8) < 8) {
        DEBUG("entry point too short");
        goto BAD_HEADER;
    }

    if (memcmp(p, "_SM", 3)){
        DEBUG("Bad preamble");
        goto BAD_HEADER;
    }

    p += 3;

    if (*p == '_') {
        // 32-bit entry point
        DEBUG("Got 32-bit entry point");
        mode64bit = 0;
        ++p;
    } else if ((*p == '3') && (*(++p) == '_')) {
        // 64-bit entry point
        DEBUG("Got 64-bit entry point");
        mode64bit = 1;
        ++p;
    }
    else goto BAD_HEADER;

    // next byte is checksum, ignore
    ++p;

    ep_len = *p++;
    if (mode64bit && (ep_len < 0x18)) {
        fprintf(stderr, "ERROR: Entry point is too small for 64-bit: %x\n", ep_len);
        goto BAD_HEADER;
    }
    else if (!mode64bit && (ep_len < 0x1e)) {
        fprintf(stderr, "ERROR: Entry point too small for 32-bit: %x\n", ep_len);
        goto BAD_HEADER;
    }
    else if (ep_len >= 0x24) {
        fprintf(stderr, "ERROR: Entry point too large: %x\n", ep_len);
        goto BAD_HEADER;
    }

    // now, read the remaining bytes into buffer (had 8 already)
    if (read(fd, buf + 8, ep_len - 8) < (ep_len - 8)) {
        fprintf(stderr, "ERROR: cannot read remaining entry point\n");
        goto BAD_HEADER;
    }
    DEBUG("ep pos = %d, len=%d", (p - buf), ep_len);
#if 0
    if (do_debug) {
        for (char *d=buf; d<buf+ep_len; d++)
            fprintf(stderr, "%02x ", *d);
        fprintf(stderr, "\n");
    }
#endif

    ret.version[0] = *p++;
    ret.version[1] = *p++;
    if (mode64bit)
        ret.version[2] = *p++;

    if (!mode64bit) {
        p = buf + 0x16;
        ret.st_len = le16toh(*((uint16_t*) p) );
        p = buf + 0x18;
        ret.st_addr = le32toh(*((uint32_t*) p) );
    } else {
        p = buf + 0x0c;
        ret.st_len = le32toh(*((uint32_t*) p));
        p = buf + 0x10;
        ret.st_addr = le64toh(*((uint64_t*) p));
    }

    DEBUG("Got entry point, len=%d, addr=0x%lx", ret.st_len, ret.st_addr);
    return ret;

BAD_HEADER:
    fprintf(stderr, "ERROR: Bad SMBIOS entry point\n");
    exit(2);
}

/** Parse the SMBIOS struct table, looking for the one to decode
 *
 * This is by no means a full decoder; rather a very specialized
 * one to just do type 9 structs.
 *
 * Using mmap to directly access the [file] contents.
 */
int decode_dmi(int fd, ssize_t len, const struct pci_bdf qbdf) {
    void* ptable = NULL;
    const char* pend = NULL;
    const char* pcur = NULL;
    struct sm_entry entry;
    int n, o;
    int ret = 2;

    static const char PCI_SLOT_TYPES[] = {
        0x06, 0x0e, 0x12, 0x1f, 0x20, 0x21, 0x22, 0x23,
    };

    if ((ptable = malloc(len+1)) == NULL) {
        fprintf(stderr, "ERROR: cannot alloc\n");
        close(fd);
        return EXIT_FAILURE;
    }

    pend = (char*) ptable + len;
    pcur = (char*) ptable;
    while ((n = read(fd, (void*)pcur, pend - pcur)) > 0) {
        pcur += n;
        if (pcur >= pend) break;
    }
    close(fd);

    if (pcur < pend) {
        fprintf(stderr, "ERROR: cannot read SMBIOS file %s\n", strerror(errno));
        free(ptable);
        return EXIT_FAILURE;
    }

    pcur = (char*) ptable;
    while (pcur + 4 < pend) {
        memset(&entry, 0, sizeof(entry));
        entry.start = pcur;
        entry.type = *pcur++;
        entry.len = *((unsigned char*) pcur++);
        entry.handle = le16toh(*((uint16_t*) pcur));
        if (entry.len < 4) {
            fprintf(stderr, "ERROR: entry too short pos=%x (%x)\n",
                    (pcur - (char*)ptable), entry.handle);
            goto BAD_TABLE;
        }

        pcur = (char*) entry.start + entry.len;
        DEBUG("Got table type=%d , handle=0x%04x, len=%d", entry.type, entry.handle, entry.len);

        if (pcur >= pend) {
            fprintf(stderr, "ERROR: entry overflow at handle %04x\n", entry.handle);
            goto BAD_TABLE;
        }

        // now, jump to string area
        for(n=0;n<255;n++) {
            if (pcur >= pend) {
                fprintf(stderr, "ERROR: string table overflow at handle %d\n", entry.handle);
                goto BAD_TABLE;
            }
            if (*pcur) {
                entry.strings[n] = pcur++;
                while ((pcur < pend) && *pcur++);
            }
            else if (n)
            {
                // zero, terminate
                ++pcur;
                break;
            }
            else ++pcur;
        }

        // ok, we have the struct, now what??

        if (entry.type == 9) {
            // System Slot
            int slot_name_pos = *((unsigned char*) entry.start + 0x04);
            unsigned char slot_type = *((unsigned char*) entry.start + 0x05);
            const char* slot_name = NULL;
            if (slot_name_pos && slot_name_pos < 255)
                slot_name = entry.strings[slot_name_pos-1];

            if (entry.len < 0x10) {
                // old smbios, no BDF fields
                continue;
            }
            if ((slot_type >=0xA5 && slot_type <=0xb6) || slot_type == 0x06 || slot_type == 0x0e
                || slot_type == 0x12 || (slot_type >= 0x1f && slot_type <= 0x23)) {

                unsigned char * p = ((unsigned char*) entry.start) + 0x0d;
                struct pci_bdf bdf = {0};
                bdf.segment = le16toh(*((uint16_t*) p));
                p += 2;
                bdf.bus = *(p++);
                bdf.dev = *p >> 3;
                bdf.fn = *p & 0x7;

                DEBUG("PCI slot '%s' found! S.BDF = %04x.%02x:%02x.%x",
                      slot_name, bdf.segment, bdf.bus, bdf.dev, bdf.fn);

                if (qbdf.all) {
                    printf("%04x.%02x:%02x.%x\t%s\n",
                           bdf.segment, bdf.bus, bdf.dev, bdf.fn, slot_name);
                    /* with --all any type 9 entry will be a good result */
                    ret = 0;
                }
                else if ((bdf.bus == qbdf.bus) && (bdf.segment == qbdf.segment)
                    && (bdf.dev == qbdf.dev) && (bdf.fn == qbdf.fn)) {
                    printf("%s", slot_name);
                    ret = 0;
                    break;
                }
            } else {
                DEBUG("Slot 0x%x found at %04x : %s", slot_type,
                    ((char*)entry.start - (char*)ptable), slot_name);
            }
        }
    }

    free(ptable);
    return ret;

    BAD_TABLE:
    free(ptable);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    const char *ep_fname = "/sys/firmware/dmi/tables/smbios_entry_point";
    const char *dmi_fname = "/sys/firmware/dmi/tables/DMI";
    struct sm_header header = {0};

    static struct option long_options[] = {
        {"entry-point", required_argument, 0,  'e' },
        {"dmi-table", required_argument, 0, 'd'},
        {"pci", required_argument, 0, 'p'},
        {"all", no_argument, 0, 'a'},
        {"verbose", no_argument, 0, 'v' },
        {0,         0,           0,  0 }
    };
    int option_index = 0;
    int opt;

    int fd = -1;
    struct pci_bdf qbdf = {0};

    while ((opt = getopt_long(argc, argv, "e:d:p:av", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'e':
            ep_fname = optarg;
            break;
        case 'd':
            dmi_fname = optarg;
            break;
        case 'v':
            do_debug = 1;
            break;
        case 'a':
            qbdf.all = 1;
            break;
        case 'p':
            if (qbdf.all) {
                fprintf(stderr, "Option -p is incompatible with -a\n");
                exit(EXIT_FAILURE);
            }
            if (sscanf(optarg, "%4hx:%2hhx:%2hhx.%hhx",
                    &qbdf.segment, &qbdf.bus, &qbdf.dev, &qbdf.fn) < 4) {
                fprintf(stderr, "ERROR: invalid sBDF: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-e smbios_entry_point] [-d DMI] {-p PCI_BDF|-a}\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // read entry point
    DEBUG("Opening entry-point at %s", ep_fname);
    if ((fd = open(ep_fname, O_RDONLY)) > 0) {
        header = read_entrypoint(fd);
        close(fd);
        fd = -1;
    } else {
        fprintf(stderr, "ERROR: cannot open entry-point: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    DEBUG("Entry point is SMBIOS %d.%d.%d , len=%d",
            header.version[0], header.version[1], header.version[2],
            header.st_len);

    fd = open(dmi_fname, O_RDONLY);
    if (fd > 0) {
        return decode_dmi(fd, header.st_len, qbdf);
    } else {
        fprintf(stderr, "ERROR: cannot open DMI structure file: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return 0;
}
