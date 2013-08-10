/*
 *  RelF - Relative Forth
 *  Based on SOD32 by L.C. Benschop
 *  Copyright 2001 - 2005, Kirill Timofeev, kt97679@gmail.com
 *  The program is released under the GNU General Public License version 2.
 *  There is NO WARRANTY.
 */

#include <stdio.h>
/*
#define BIG_ENDIAN
*/
#define  UNS8 unsigned char  /*     Virtual    */
#define INT32 long           /*     machine    */
#define UNS32 unsigned long  /* internal types */

#define MEMSIZE 64 * 1024    /* how much memory do we allocate for VM */

/*
 *  Macroses for memory access
 */

#define CELL(reg) (*(UNS32*)(reg)) /* cell (word) memory access macros */

#ifdef BIG_ENDIAN                         /* byte memory access macros */
    #define BYTE(reg) (*(UNS8*)(reg))     /* it depends on the order   */
#else                                     /* of bytes in the word      */
    #define BYTE(reg) (*(UNS8*)(reg ^ 3)) /* VM is 32-bit, big endian  */
#endif                                    /* (like mc680x0)            */

/*
 *  Macroses for stack access
 */

#define RS       CELL(rp)         /* top of return stack               */
#define DS0      CELL(sp)         /* top of data stack                 */
#define DS1      CELL(sp + 4)     /* 2nd element on data stack         */
#define DS2      CELL(sp + 8)     /* 3d element on data stack          */
#define DS3      CELL(sp + 12)    /* 4th element on data stack         */
#define PUSH(x)  sp -= 4; DS0 = x /* pushes x to data stack            */
#define RPUSH(x) rp -= 4; RS = x  /* pushes x to return stack          */
#define FP       (FILE *)DS0      /* top of data stack is file pointer */
#define DONEXT    t = CELL(ip); ip += 4; if (t & 1) { goto **(UNS32 *)(vmops + t); } else { goto vmdolist; }


/*
 *  VM memory. +3 is necessary to be able to allocate MEMSIZE bytes from
 *  4-aligned address in order to have native word access.
 */

UNS8 mem[MEMSIZE + 3];

/*
 *  1st 4-aligned address in mem array. This is base address of the system.
 */

UNS8 *base;

/* VM registers and related variables */

UNS32  ip; /* instruction pointer               */
UNS32  rp; /* return stack pointer              */
UNS32  sp; /* data stack pointer                */
UNS32   t; /* variable for temporary storage    */
UNS32 ior; /* I/O result used by I/O primitives */

/*
 *  Perform byte swap of 32-bit words in region
 *  of memory of virtual machine.
 */

swap_mem(UNS32 start, UNS32 len) {
#ifndef BIG_ENDIAN
    UNS32 i, m;

    for (i = start & (-4); i < len + start; i += 4) {
        m = CELL(i);
        m = (m >> 16) | (m << 16);
        m = ((m & 0xff00ff00) >> 8) | ((m & 0x00ff00ff) << 8);
        CELL(i) = m;
    }
#endif
}

/*
 *  Multiply 32-bit unsigned numbers *a and *b.
 *  High half of 64-bit result in *a, low half in *b
 */

static void umul(UNS32 *a, UNS32 *b) {
    UNS32 ah, al, bh, bl, ph, pm1, pm2, pl;

    ah = *a >> 16;
    al = *a & 0xffff;
    bh = *b >> 16;
    bl = *b & 0xffff;
    pl = al * bl;
    if ((ah | bh) == 0) {
        ph = 0;
    } else {
        pm1 = al * bh;
        pm2 = ah * bl;
        ph = ah * bh;
        pl = pl + (pm1 << 16);
        ph += (pl < (pm1 << 16));
        pl = pl + (pm2 << 16);
        ph += (pl < (pm2 << 16));
        ph = ph + (pm1 >> 16) + (pm2 >> 16);
    }
    *a=ph;
    *b=pl;
}

/*
 *  Divide 64-bit unsigned number (high half *b, low half *c) by
 *  32-bit unsigend number in *a. Quotient in *b, remainder in *c.
 */

static void udiv(UNS32 *a,UNS32 *b,UNS32 *c) {
    UNS32 d, qh, ql;
    int i, cy;

    qh = *b;
    ql = *c;
    d = *a;
    if (qh == 0) {
        *b = ql / d;
        *c = ql % d;
    } else {
        for (i = 0; i < 32; i++) {
            cy=qh & 0x80000000;
            qh <<= 1;
            if (ql & 0x80000000) qh++;
            ql <<= 1;
            if (qh >= d || cy) {
                qh -= d;
                ql++;
                cy = 0;
            }
            *c = qh;
            *b = ql;
        }
    }
}

/*
 *  Virtual machine itself.
 */

virtual_machine() {
    void *_vmops[] = {&&vmnoop, &&vmexit, &&vmlit, &&vmbranch, &&vm0branch, &&vmdrop,
        &&vmdup, &&vmswap, &&vmrot, &&vmover, &&vmcfetch, &&vmfetch, &&vmcstore,
        &&vmstore, &&vmand, &&vmor, &&vmxor, &&vmfromr, &&vmtor, &&vmrfetch, &&vmeq,
        &&vmugt, &&vmgt, &&vmplus, &&vmnegate, &&vmlshift, &&vmrshift, &&vmummult,
        &&vmumdiv, &&vmdplus, &&vmemit, &&vmkey, &&vmbye, &&vmspfetch, &&vmspstore,
        &&vmrpfetch, &&vmrpstore, &&vmopenfile, &&vmclosefile, &&vmreadline,
        &&vmwriteline, &&vmreadfile, &&vmwritefile, &&vmsystem, &&vmreposfile,
        &&vmfilepos, &&vmdelfile, &&vmfilesize};
    typedef void (*vmop)();
    UNS32 vmops = (UNS32)_vmops - 1;
    FILE *fp;
    char *filemode[] = {"w", "wb", "r", "rb", "r+", "r+b"};

    ip = (UNS32)base;
    rp = ip + MEMSIZE;
    sp = ip + MEMSIZE - 1024;
    PUSH(ip);
    DONEXT
/*
 *  Functions, implementing forth virtual machine primitives.
 */

vmdolist:  /* dolist  */ RPUSH(ip); ip +=t; DONEXT
vmnoop:    /* noop    */ DONEXT
vmexit:    /* exit    */ ip=RS; rp += 4; DONEXT
vmlit:     /* lit     */ PUSH(CELL(ip)); ip += 4; DONEXT
vmbranch:  /* branch  */ ip += CELL(ip); DONEXT
vm0branch: /* 0branch */ if (DS0) ip += 4; else ip += CELL(ip); sp += 4; DONEXT
vmdrop:    /* drop    */ sp += 4; DONEXT
vmdup:     /* dup     */ PUSH(DS1); DONEXT
vmswap:    /* swap    */ t = DS0; DS0 = DS1; DS1 = t; DONEXT
vmrot:     /* rot     */ t = DS2; DS2 = DS1; DS1 = DS0; DS0 = t; DONEXT
vmover:    /* over    */ PUSH(DS2); DONEXT
vmcfetch:  /* C@      */ DS0 = BYTE(DS0); DONEXT
vmfetch:   /* @       */ DS0 = CELL(DS0); DONEXT
vmcstore:  /* c!      */ BYTE(DS0) = DS1; sp += 8; DONEXT
vmstore:   /* !       */ CELL(DS0) = DS1; sp += 8; DONEXT
vmand:     /* and     */ DS1 &= DS0; sp += 4; DONEXT
vmor:      /* or      */ DS1 |= DS0; sp += 4; DONEXT
vmxor:     /* xor     */ DS1 ^= DS0; sp += 4; DONEXT
vmfromr:   /* r>      */ PUSH(RS); rp += 4; DONEXT
vmtor:     /* >r      */ RPUSH(DS0); sp += 4; DONEXT
vmrfetch:  /* r@      */ PUSH(RS); DONEXT
vmeq:      /* =       */ DS1 = - (DS0 == DS1); sp += 4; DONEXT
vmugt:     /* u<      */ DS1 = - (DS1 < DS0); sp += 4; DONEXT
vmgt:      /* <       */ DS1 = - ((INT32)DS1 < (INT32)DS0); sp += 4; DONEXT
vmplus:    /* +       */ DS1 += DS0; sp += 4; DONEXT
vmnegate:  /* negate  */ DS0 = - DS0; DONEXT
vmlshift:  /* lshift  */ DS1 <<= DS0; sp += 4; DONEXT
vmrshift:  /* rshift  */ DS1 >>= DS0; sp += 4; DONEXT
vmummult:  /* um*     */ umul (&DS0, &DS1); DONEXT
vmumdiv:   /* um/mod  */ udiv (&DS0, &DS1, &DS2); sp += 4; DONEXT
vmdplus:   /* d+      */ DS3 += DS1; DS2 += DS0; DS2 += (DS3 < DS1); sp += 8; DONEXT
vmemit:    /* emit    */ putchar(DS0); sp += 4; DONEXT
vmkey:     /* key     */ PUSH(getchar()); DONEXT
vmbye:     /* bye     */ exit(0); DONEXT
vmspfetch: /* sp@     */ PUSH(sp + 4); DONEXT
vmspstore: /* sp!     */ sp = DS0; DONEXT
vmrpfetch: /* rp@     */ PUSH(rp); DONEXT
vmrpstore: /* rp!     */ rp = DS0; sp += 4; DONEXT

/*
 *  virtual machine I/O primitivies
 */

vmopenfile:
    t = BYTE(DS2 + DS1);
    BYTE(DS2 + DS1) = 0;
    swap_mem(DS2, DS1);
    fp = fopen((char *)DS2, filemode[DS0]);
    swap_mem(DS2, DS1);
    BYTE(DS2 + DS1) = t;
    DS2 = (UNS32)fp;
    if (fp) {
        DS1 = 0;
    } else {
        DS1 = 200;
    }
    sp += 4;
    DONEXT

vmclosefile:
    DS0 = fclose(FP);
    DONEXT

vmreadline:
    clearerr(FP);
    swap_mem(DS2, DS1);
    fgets((char*)DS2, DS1, FP);
    t = strlen((char*)DS2);
    if (*(char*)(DS2 + t - 1) == '\n') {
        t--;
    }
    swap_mem(DS2, DS1);
    DS2 = t;
    DS1 = - !(feof(FP));
    if (ferror(FP)) {
        DS0 = -200;
    } else {
        DS0 = 0;
    }
    DONEXT

vmwriteline:
    clearerr(FP);
    swap_mem(DS2, DS1);
    fwrite((char*)DS2, 1, DS1, FP);
    swap_mem(DS2, DS1);
    fputc('\n', FP);
    DS2 = 0;
    if (ferror(FP)) {
        DS2 = -200;
    }
    sp += 8;
    DONEXT

vmreadfile:
    clearerr(FP);
    swap_mem(DS2, DS1);
    t = fread((char*)DS2, 1, DS1, FP);
    swap_mem(DS2, DS1);
    DS2 = t;
    DS1 = 0;
    if (ferror(FP)) {
        DS1 = -200;
    }
    sp += 4;
    DONEXT

vmwritefile:
    clearerr(FP);
    swap_mem(DS2, DS1);
    fwrite((char*)DS2, 1, DS1, FP);
    swap_mem(DS2, DS1);
    DS2 = 0;
    if (ferror(FP)) {
        DS2 = -200;
    }
    sp += 8;
    DONEXT

vmsystem:
    t = BYTE(DS1 + DS0);
    BYTE(DS1 + DS0) = 0;
    swap_mem(DS1, DS0);
    ior = system((char*)DS1);
    swap_mem(DS1, DS0);
    BYTE(DS1 + DS0) = t;
    DS1 = ior;
    sp += 4;
    DONEXT

vmreposfile:
    DS1 = fseek(FP, DS1, SEEK_SET);
    sp += 4;
    DONEXT

vmfilepos:
    DS0 = ftell(FP);
    sp -= 4;
    if (DS1 == -1) {
        DS0 = 200;
    } else {
        DS0 = 0;
    }
    DONEXT

vmdelfile:
    t = BYTE(DS1 + DS0);
    BYTE(DS1 + DS0) = 0;
    swap_mem(DS1, DS0);
    ior = remove((char*)DS1);
    swap_mem(DS1, DS0);
    BYTE(DS1 + DS0) = t;
    DS1 = ior;
    sp += 4;
    DONEXT

vmfilesize:
    t = ftell(FP);
    fseek(FP, 0L, SEEK_END);
    ior = ftell(FP);
    fseek(FP, t, SEEK_SET);
    DS0 = ior;
    PUSH(0);
    DONEXT
}

/*
 *  This function reads binary forth image from file into memory.
 */

load_image(char *name) {
    UNS32 len;
    FILE *infile;

    if ((infile = fopen(name, "rb")) == NULL) {
        fprintf(stderr, "Cannot open image file.\n");
        exit(2);
    }
    base = (UNS8*)(((UNS32)mem + 3) & (-4));
    len = fread(base, 1, MEMSIZE, infile);
    swap_mem((UNS32)base, len);
}

/*
 *  program entry point
 */

main(int argc,char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: relf <filename>\n");
    } else {
        load_image(argv[1]);
        virtual_machine();
    }
    return 0;
}
