/*
 *  ia32-fsm.c
 *  DynamicPatch
 *
 *  Created by jim on 31/3/2006.
 *
 *  Based on fsm.c by Elene Terry (partial header notes reproduced below)
 *  See http://zoo.cs.yale.edu/classes/cs490/00-01a/terry.elene.est23/
 *      for more information.
 *  Original file is available at:
 *      http://zoo.cs.yale.edu/classes/cs490/00-01a/terry.elene.est23/fsm.txt
 *
 *  Changes:
 *      1. No longer prints output, just counts bytes.
 *      2. No longer reads from stdin, all states take a byte ptr argument for input.
 *      3. State machine takes size argument, and terminates when insn size count
 *              exceeds that value.
 *      4. Replaced main function with entry point suitable for use by patch routines.
 *      5. Made tables static const, since they're read-only.
 *      6. Made more readable (mmm, whitespace).
 *
 */

#if __i386__

/* Elene Terry
Senior Project '00

OBJECTIVE
This program simulates in C the FSM used to parse the x86 instruction set
as described by the Intel Architecture Software Developer's Manual. Volume 2: 
Instruction Set Reference, 1997. I've assumed the machine to be a 32 bit 
architecture (Pentium Pro and above) and have tested this exclusively on 
code compiled for redhat linux. This header is repeated in the file fsmmaker
and fsmtester. 

OUTPUT
fsmtester and fsmmaker generate the output file fsmoutfile. It is formatted as
follows:

BYTE #:BYTEVAL is a starting byte opcode
21663:c9 is a starting byte opcode

For instance, byte number 1 would be the first byte in the file, byte number 
10, the tenth. If the fsmmaker encounters a byte that it does not know how to
parse, fsmmaker ceases immediately and prints to fsmoutfile that it does not 
know how to deal with the byte.

ADDITIONAL NOTES
Page 2-6 of the Instruction Set Reference describes the possible values of the
SIB byte. Note 1 specifies that an asterisk indicates an additional
displacement in certain instances. I've assumed the asterisk to only apply to
the one in the r32 column.

Every prefix (except 0x66) is classified as a starting byte opcode. The byte 
following it is also classified as a starting byte opcode. The prefix 0x66,
however, is always grouped with it's following bytes because it changes the
encoding of the following byte. So if the byte 0x65 were followed by 0x0f my
fsm would label them both as starting byte opcodes. If 0x66 were followed by
0x0f, only 0x66 would be a starting byte opcode.

I've only dealt with unary group 3. Some of the opcodes use the opcode 
extension in the Mod R/M byte to encode different types of bytes. I've
only encountered 0xf7 as being a problem (unary group3) other unary groups
might come up depending on other sample input.
*/

#include <stdio.h>
#include <setjmp.h>

static jmp_buf jump_env;

void state0(int *, int *, const unsigned char *, int);
void state4(int *, int *, const unsigned char *, int);
void state5(int *, int *, const unsigned char *, int);
void state6(int *, int *, const unsigned char *, int);
void state7(int *, int *, const unsigned char *, int);
void state8(int *, int *, const unsigned char *, int);
void state9(int *, int *, const unsigned char *, int);
void state17(int *, int *, const unsigned char *, int);
void state19(int *, int *, const unsigned char *, int);
void state20(int *, int *, const unsigned char *, int);
void state21(int *, int *, const unsigned char *, int);
void state22(int *, int *, const unsigned char *, int);
void state26(int *, int *, const unsigned char *, int);
void state101(int *, int *, const unsigned char *, int);
void state102(int *, int *, const unsigned char *, int);
void state103(int *, int *, const unsigned char *, int);
void state104(int *, int *, const unsigned char *, int);
void state105(int *, int *, const unsigned char *, int);
void state106(int *, int *, const unsigned char *, int);
void state107(int *, int *, const unsigned char *, int);
void state108(int *, int *, const unsigned char *, int);
void state109(int *, int *, const unsigned char *, int);
/*
int getbyte (int *byte, int *x) 
{ 
  *x = *x + 1;
  if (scanf("%x",byte)==1) return *byte;
  else {
    printf("no more bytes, program terminated\n");
    exit(0);
    return EOF;
  }
}
*/

int getbyte(int *byte, int *x, const unsigned char *p, int stop)
{
    register int o = *x;
    *byte = (int) p[o];
    *x = o + 1;
    return ( *byte );
}

/* The columns correspond to the various encodings of the modR/M byte 
described on page 2-5 of the Instruction Set Reference.
Column # -- hex values of the modr/m
  byte encoding looks like
-----------------------------------------------------------------
Column 1 -- 00-03, 06-0B,0E-13,16-1B,1E-23,26-2B,2E-33,36-3B,3E-3F
  opcode --> Mod R/M --> <optional disp/imm based on opcode>
Column 2 --04,0C,14,1C,24,2C,34,3C
  opcode --> Mod R/M --> SIB --> <opt 4 byte disp based on SIB> --> 
            <opt disp/imm based on opcode>
Column 3 -- 40-43,45-4B,4D-53,55-5B,5D-63,65-6B,6D-73,75-7B,7D-7F
  opcode --> Mod R/M --> DISP --> <opt disp/imm based on opcode>
Column 4 -- 44,4C, 54,5C,64,6,74,7C
  opcode --> Mod R/M --> SIB --> DISP --> <opt disp/imm based on opcode>
Column 5 -- 80-83,85-8B,8D-93,95-9B,9D-A3,A5-AB,AD-B3,B5-BB,BD-BF
  opcode --> Mod R/M --> DISP --> DISP --> DISP --> DISP --> 
            <opt disp/imm based on opcode>
Column 6 -- 84,8C,94,9C,A4,AC,B4,BC
  opcode --> Mod R/M --> SIB --> DISP --> DISP --> DISP --> DISP -->
             <opt disp/imm based on opcode>
Column 7 -- C0-FF
  opcode --> Mod R/M --> <opt disp/imm based on opcode>
Column 8 -- 05,0D,15,1D,25,@D, 35,3D
  opcode --> Mod R/M --> DISP --> DISP --> DISP --> DISP --> 
         <opt disp/imm based on opcode>
*/

int incol1 (int byte)
{
    if (((byte&0xc0) == 0x00) && ((byte&0x07)!=0x04) && (byte&0x07)!=0x05)
        return 1;
    else return 0;
}
int incol2 (int byte)
{
    if ((byte&0xC7)==0x04) 
        return 1; 
    return 0;
}
/* checked to be correct - 11/25 */
int incol3 (int byte)
{
    if ((byte <=0x7f)&&(byte>=0x40)&&((byte|0x38)!=0x7c)) 
        return 1;
    else
        return 0;
}
/* checked to be correct - 11/25 */
int incol4 (int byte)
{
    if ((byte | 0x38) == 0x7c) 
        return 1;
    return 0;
}
/* checked to be correct -11/25 */
int incol5(int byte)
{
    if ((byte <=0xbf)&&(byte>=0x80)&&((byte|0x38)!=0xbc))  
        return 1;
    else
        return 0;
}
/* checked to be correct - 11/25 */
int incol6 (int byte)
{
    if ((byte|0x38) == 0xbc) 
        return 1;
    else
        return 0;
}
int incol7 (int byte)
{
    if ((byte>=0xC0)&&(byte<=0xFF))
        return 1;
    else
        return 0;
}
int incol8 (int byte)
{
    if ((byte&0xc7)==0x05)
        return 1; 
    return 0;
}


/*      USAGE: int sibhasdisp(int byte)
        INPUT: int byte -- an SIB byte
       OUTPUT: return int -- a 1 if the SIB encodes additional displacement
                             a 0 if not
                             (see column encodings above as well as 2-5,2-6
                              in Instruction Set Reference.)
                           the new value of the byte read is returned 
                            through byte, as well as *byte
  DESCRIPTION: Page 2-6 of the Instruction Set Reference describes the 
               possible values of the SIB byte. Note 1 specifies that an 
               asterisk indicates an additional displacement in certain 
               instances. I've assumed the asterisk to only apply to the one 
               in the r32 column.
*/
int sibhasdisp(int byte)
{
    if ((byte|0xf8)==0xfd) 
        return 1;
    return 0;  
}




/* OPCODE Categories

Each opcode may belong to one of the following categories:

hasmodrm0 -- the opcode byte has a mod r/m following it
hasmodrm1 -- " " " " " " " " ", as well as a one byte disp/imm
hasmodrm4 -- " " " " " " " " ", as well as a 4 byte disp/imm
has1add   -- the opcode byte has a one byte disp/imm following it
has2add   -- " " " " " 2 byte disp/imm following it
has3add   -- " " " " " 3 " " " "
has4add   -- " " " " " 4 " " " "
has6add   -- " " " " " 6 " " " " (this is actually a ptr)
onebyte   -- the opcode has no bytes following it

There are additional categories, see state0.

To test for inclusion in these categories I've enumerated the values of bytes
that belong to each of the above categories and placed those values in an 
array. Each category has a function which returns a 1 if the opcode byte
belongs to that category and a 0 otherwise. I realize that this method
may be slower, but it's the easiest at the moment. When all bytes have been 
accounted for the arrays and array loops should be turned into logic.
Each function has the same footprint, using hasmodrm0 as an example:

      USAGE: int hasmodrm0(int )
      INPUT: byte -- the opcode byte
     OUTPUT: a 1 if the byte belongs to the category
               0 otherwise
DESCRIPTION: tests for inclusion in one of the categories above.
*/

int hasmodrm0 (int byte)
{
    int i,length=46;
    static const int modrm[46] = { 
        0x03, 0x38, 0x39, 0x3A, 0x3B, 0x84, 0x85, 0xff, 0x28, 0x29,
        0x2A, 0x2B, 0x8d, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8e, 0x31,
        0x01, 0x20, 0x00, 0x23, 0xd3, 0x08, 0x30, 0x09, 0x11,
        0x10, 0x62, 0x63, 0x0a, 0x0b, 0x18, 0x21, 0xd0, 0xd1, 0xd2,0xd3,
        0x19, 0xfe, 0x36, 0xc4, 0x12, 0x13
    };
    
    for (i=0;i<length;i++)
    {
        if (modrm[i] == byte) 
            return 1;
    }
    return 0;
}

int hasmodrm1 (int byte)
{
    int i,length=6;
    static const int modrm[6] = { 0xc1, 0xc0,0x80, 0x83, 0xf6, 0xc6 };
    
    for (i=0;i<length;i++) 
    {
        if (modrm[i] == byte) 
            return 1;
    }
    return 0;
}

int hasmodrm4 (int byte)
{
    int i,length=3;
    static const int modrm[3] = { 0x81,  0xc7, 0x69};
    
    for (i=0;i<length;i++)
    {
        if (modrm[i] == byte) 
            return 1;
    }
    return 0;
}

int has1add (int byte)
{
    int i, length=42 ;
    static const int add[42] = {
        0xe3,0x70,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,
        0x7a,0x7b,0x7c,0x7d,0x7e,0x7f, 0xeb, 0x3c, 0xa8, 0x6a,
        0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xa2,0x2c,0x24,
        0x0c, 0xcd,0x24,0x04,0x14, 0xe4, 
        0xe5,0xd4,0x34,0xa0, 0x1c
    };
    
    for (i=0;i<length;i++) 
    {
        if (add[i] == byte) 
            return 1;
    }
    return 0;
}
int has2add (int byte)
{
    int i, length=2;
    static const int add[2] = { 0xc2, 0xca  };
    for (i=0;i<length;i++) 
    {
        if (add[i] == byte) 
            return 1;
    }
    return 0;
}
int has3add (int byte) 
{
    int i, length=1;
    static const int add3 [1]= {0xc8};
    for (i=0; i<length; i++)
    {
        if (add3[i]==byte) 
            return 1;
    }
    return 0;
}

int has4add (int byte) {
    int i, length=21 ;
    static const int add4[21] = {
        0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0x25,0x3d,0x68,
        0xa1,0xa3,0xa9,0xe8,0xe9,0x35,0x05,0x0d, 0x1d,0x15
    };
    for (i=0;i<length;i++) 
    {
        if (add4[i] == byte) return 1;
    }
    return 0;
}
int has6add (int byte)
{
    int i, length=1;
    static const int add6 [1]= {0xea};
    for (i=0; i<length; i++)
    {
        if (add6[i]==byte) return 1;
    }
    return 0;
}

int onebyte (int byte) {
    int i, length=74;
    static const int one[74] = {
        0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,
        0x4b,0x4c,0x4d,0x4e,0x4f,0xf8,0x99,0x50,0x52,0x53,0x55, 
        0x90,0xc3,0xc9,0x5e,0xf4,0x56,0x5b,0x54,0x51,0x57,0xaf,
        0xfc,0x5f,0x58,0x5d,0x61,0x65,0x64,0x59,0xac,0xad,0x5c,
        0xcc,0x6d,0x6f,0x6e,0x67,0x66,0x6c,0x9c,0x95,0xce,0x60,
        0xec,0xed,0xcf,0xfd,0xd7,0x5a,0xa4,0xab,0xaa,0x07,0x94,
        /*prefix */0xf0,0xf2,0xf3,0xa5,0xae,0xa6,0x98,
        0x82/*XXX not right but to get past the
        (bad ) bytes */  
    };
    for (i=0;i<length;i++) {
        if (one[i] == byte) return 1;
    }
    return 0;
}

/* My state encodinsgs are as follows, for additional information see the 
   comments before the beginning of the state 
  
   state 0 - beginning state, "starting byte opcode"

   state 4 - Is a 2 byte opcode, first byte being 0x0F

   state 5 - escape opcodes for floating point operations 

   state 6 - has Mod/RM and no additional bytes 
   state 7 - has Mod/RM and 1 additional byte
   state 8 - has Mod/RM and 2 additional bytes 
   state 9 - has Mod/RM and 4 additional bytes
 
   state 17 - operand size override 0x66

   state 19 - sib byte for state6 
   state 20 - sib byte for state7 
   state 21 - sib byte for state8 
   state 22 - sib byte for state9 

   state 26 - opcode 0xf7

   state 101-109 -- has # bytes additional


*/

/* This is the starting state of my FSM, it's output is the statement 
byte #:byte val is a starting byte opcode. It then tests for what type of 
opcode the byte is and sends it the next state of the FSM */
void state0(int *byte, int *x, const unsigned char *p, int stop)
{
    int opcode;
    
    // do we even need another instruction ?
    if ( *x >= stop )
        longjmp(jump_env, 1);
    
    getbyte(byte,x,p,stop);
    //printf ("%d:%x is a starting byte opcode \n",*x,*byte);
    opcode=*byte;
    if (*byte==0x66)            state17 (byte,x,p,stop); /* operand size override */
    if (hasmodrm0(opcode))      state6(byte,x,p,stop);
    if (hasmodrm1(opcode))      state7(byte,x,p,stop);
    if (hasmodrm4(opcode))      state9(byte,x,p,stop);
    if (opcode == 0x0F)         state4(byte,x,p,stop); /* 2 byte opcode beginning with 0x0F */
    if ((opcode&0xF8) == 0xd8)  state5(byte,x,p,stop); /* escape opcode for floating pt*/
    if (has1add(opcode))        state101(byte,x,p,stop);
    if (has2add(opcode))        state102(byte,x,p,stop);
    if (has3add(opcode))        state103(byte,x,p,stop);
    if (has4add(opcode))        state104(byte,x,p,stop);
    if (has6add(opcode))        state106(byte,x,p,stop); 
    if (onebyte(opcode))        state0(byte,x,p,stop);
    if (opcode==0xf7)           state26(byte,x,p,stop); 
    /* belongs to unary group 3 as indicated on page A-4 of the Instruction Set
        Reference. Unary group 3 has different opcode encodings based on the 
        modr/m byte, because bits 5,4,3 of the ModR/M byte are used as opcode
        extensions */
    //printf("don't know how to deal with previous byte\n");
    //exit (1);
}

/* State 4 deals with 2 byte opcodes where the beginning byte is 0x0F. I'm 
still encountering 2 byte opcodes where I don't know what the encoding is.
When this happens the state prints that I don't know how to deal with the 
encoding and exits */
void state4(int *byte, int *x, const unsigned char *p, int stop)
{
    *byte = getbyte(byte,x,p,stop);
    if ((*byte==0x89)||(*byte== 0x83)||(*byte == 0x85)||(*byte==0x84)||
        (*byte==0x8e)||(*byte==0x88)||(*byte==0x8d)||(*byte==0x8c)||
        (*byte==0x8f)||(*byte==0x87)||(*byte==0x86)||(*byte==0x82))
    {
        state104(byte,x,p,stop); /* 4 bytes of additional disp/imm */
    } 
    else if ((*byte==0xa3)||(*byte==0xbe)||(*byte==0xbf)||(*byte==0xaf)||
             (*byte==0xb7)||(*byte==0xb6)||(*byte==0xab)||(*byte==0x92)||
             (*byte==0xb3)||(*byte==0x9e))  
    {
        state6(byte,x,p,stop); /*Mod R/M byte follows the 2nd byte of opcode */
    } 
    else if ((*byte==0x94)||(*byte==0x95)) 
    { 
        state101(byte,x,p,stop); /* 1 bytes of disp/imm */
    }
    else if ((*byte==0x05)||(*byte==0x08)||(*byte==0x09)) 
    { 
        state0(byte,x,p,stop); /* no further bytes */
    }
    //printf("byte: %x",*byte);
    //printf("don't know how to deal with previous byte\n");
    //exit(1);
}

/* State 5 deals with escape opcodes. If the 2nd byte following the escape 
opcode (d8-df) is between hex values 0x00 and 0xBF the byte should be 
interpreted as a ModR/M byte. Otherwise the next byte is a starting byte 
opcode */ 
void state5(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if ((*byte >= 0x00) && (*byte <= 0xBF))  /* Mod R/M byte */
    {
        if (incol1(*byte) || incol7(*byte))
            state0(byte,x,p,stop);
        if (incol2(*byte))
            state19(byte,x,p,stop);
        if (incol3(*byte))
            state101(byte,x,p,stop);
        if (incol4(*byte))
            state102(byte,x,p,stop);
        if (incol5(*byte) || incol8(*byte))
            state104(byte,x,p,stop);
        if (incol6(*byte))
            state105(byte,x,p,stop);
    }
    state0(byte,x,p,stop);
}

/* State 6,7,8 and 9 deal with ModR/M bytes. See the comments before the 
incol functions to see the encodings */
void state6(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (incol1(*byte) || incol7(*byte))
        state0(byte,x,p,stop);
    if (incol2(*byte))
        state19(byte,x,p,stop);
    if (incol3(*byte))
        state101(byte,x,p,stop);
    if (incol4(*byte))
        state102(byte,x,p,stop);
    if (incol5(*byte)||incol8(*byte))
        state104(byte,x,p,stop);
    if (incol6(*byte))
        state105(byte,x,p,stop);
    //printf("don't know how to deal with modrm byte");
    //exit (1);
}
void state7(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (incol1(*byte) || incol7(*byte))
        state101(byte,x,p,stop);
    if (incol2(*byte))
        state20(byte,x,p,stop);
    if (incol3(*byte))
        state102(byte,x,p,stop);
    if (incol4(*byte))
        state103(byte,x,p,stop);
    if (incol5(*byte)||incol8(*byte))
        state105(byte,x,p,stop);
    if (incol6(*byte))
        state106(byte,x,p,stop);
    //printf("don't know how to deal with modrm byte");
    //exit (1);
}
void state8(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (incol1(*byte) || incol7(*byte))
        state102(byte,x,p,stop);
    if (incol2(*byte))
        state21(byte,x,p,stop);
    if (incol3(*byte))
        state103(byte,x,p,stop);
    if (incol4(*byte))
        state104(byte,x,p,stop);
    if (incol5(*byte)||incol8(*byte))
        state106(byte,x,p,stop);
    if (incol6(*byte))
        state107(byte,x,p,stop);
    //printf("don't know how to deal with modrm byte");
    //exit (1);
}
void state9(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (incol1(*byte) || incol7(*byte))
        state104(byte,x,p,stop);
    if (incol2(*byte))
        state22(byte,x,p,stop);
    if (incol3(*byte))
        state105(byte,x,p,stop);
    if (incol4(*byte))
        state106(byte,x,p,stop);
    if (incol5(*byte)||incol8(*byte))
        state108(byte,x,p,stop);
    if (incol6(*byte))
        state109(byte,x,p,stop);
    //printf("don't know how to deal with modrm byte");
    //exit (1);
}

/* State 17 deals with the operand-size override prefix 0x66. It reads the 
following byte and changes it's encoding to match that of a 16 bit machine
(I've assumed that we're always on a 32 bit machine) I'm not sure if all my
branches are completely right, I may need additional states to truly take
care of this opcode XXX */
void state17(int *byte, int *x, const unsigned char *p, int stop)
{
    int opcode;
    getbyte(byte,x,p,stop); /* this is not actually right. screwy */
    if (*byte==0x66)
        state17 (byte,x,p,stop); 
    
    opcode = *byte;
    
    if (hasmodrm0(opcode))      state6(byte,x,p,stop);
    if (hasmodrm1(opcode))      state7(byte,x,p,stop);
    if (hasmodrm4(opcode))      state8(byte,x,p,stop);
    if (opcode == 0x0F)         state4(byte,x,p,stop);
    if ((opcode&0xF8) == 0xd8)  state5(byte,x,p,stop);
    if (has1add(opcode))        state101(byte,x,p,stop);
    if (has2add(opcode))        state102(byte,x,p,stop);
    if (has3add(opcode))        state103(byte,x,p,stop);
    if (has4add(opcode))        state102(byte,x,p,stop);
    if (has6add(opcode))        state104(byte,x,p,stop);
    if (onebyte(opcode))        state0(byte,x,p,stop);
    if (opcode==0xf7)           state26(byte,x,p,stop);
    //printf("don't know how to deal with previous byte\n");
    //exit (1);
}

/* In the case that the SIB may or may not encode an additional displacement
state 19-22 will deal with the byte appropriately. Please see comments
above incol functions above */
void state19(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (sibhasdisp(*byte))
        state104(byte,x,p,stop);
    state0(byte,x,p,stop);
}
void state20 (int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (sibhasdisp(*byte))
        state105(byte,x,p,stop);
    state101(byte,x,p,stop);
}
/* On second inspection I'm not sure if this is right XXX */
void state21(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (sibhasdisp(*byte))
        state102(byte,x,p,stop);
    state0(byte,x,p,stop);
}
void state22(int *byte, int *x, const unsigned char *p, int stop)
{
    getbyte(byte,x,p,stop);
    if (sibhasdisp(*byte))
        state108(byte,x,p,stop);
    state104(byte,x,p,stop);
}

 

/* State 26 deals with the messy encoding of unary group 3 as shown on page
A-4 of the Instruction Set Reference. Unary group 3 (0xF7) has different opcode
encodings based on the modr/m byte, because bits 5,4,3 of the ModR/M byte 
are used as opcode extensions. I don't believe I've dealt with this 
particular opcode completely, but it's passing through all tests I've
thrown at it.XXX  */
void state26(int *byte, int *x, const unsigned char *p, int stop)
{
    *byte = getbyte(byte,x,p,stop);
    if ((*byte | 0xc7)==0xc7)
    {
        if (incol1(*byte) || incol7(*byte))
            state104(byte,x,p,stop);
        if (incol2(*byte))
            state22(byte,x,p,stop);
        if (incol3(*byte))
            state105(byte,x,p,stop);
        if (incol4(*byte))
            state106(byte,x,p,stop);
        if (incol5(*byte)||incol8(*byte))
            state108(byte,x,p,stop);
        if (incol6(*byte))
            state109(byte,x,p,stop);
        //printf("don't know how to deal with modrm byte");
        //exit (1);
    }
    else
    {
        if (incol1(*byte) || incol7(*byte))
            state0(byte,x,p,stop);
        if (incol2(*byte))
            state19(byte,x,p,stop);
        if (incol3(*byte))
            state101(byte,x,p,stop);
        if (incol4(*byte))
            state102(byte,x,p,stop);
        if (incol5(*byte)||incol8(*byte))
            state104(byte,x,p,stop);
        if (incol6(*byte))
            state105(byte,x,p,stop);
        //printf("don't know how to deal with modrm byte");
        //exit (1);
    }
}

/* State 101 and above are a chain of bytes for use when we need to grab
   a certain number of bytes, but the value of the byte does not matter 
   101 - grab one byte before returning to state0
   102 - grab two bytes before returning to state0
   etc...
*/
void state101(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state0(byte,x,p,stop);}
void state102(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state101(byte,x,p,stop);}
void state103(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state102(byte,x,p,stop);}
void state104(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state103(byte,x,p,stop);}
void state105(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state104(byte,x,p,stop);}
void state106(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state105(byte,x,p,stop);}
void state107(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state106(byte,x,p,stop);}
void state108(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state107(byte,x,p,stop);}
void state109(int *byte, int *x, const unsigned char *p, int stop){ getbyte(byte,x,p,stop);  state108(byte,x,p,stop);}

/*
int main ()  {
  int byte;
  int x=-1;
  state0(&byte,&x);
  return 1;
}
*/

int __calc_insn_size( const unsigned char * in_fn_addr, void * jmp_target, 
                      unsigned char new_instr[32], size_t *pSize )
{
    // return 1 if successful
    int result = 1;
    // byte being decoded
    int ibyte = 0;
    // size of decoded instructions
    int isize = 0;
    // size of data to be replaced
    // ptr to use while decoding
    const unsigned char * p = in_fn_addr;

    // setjmp returns zero on initial invocation, and returns another
    // value after a longjmp call -- I return 1 for that

    // if this returns zero, we've set the jump, otherwise, we've
    // jumped to it
    if ( setjmp( jump_env ) == 0 )
    {
        // jump into the state machine to decode
        // needed size is five bytes - size of long jump relative, 32-bit
        // operand
        state0( &ibyte, &isize, p, 5 );
    }
    
    // should have a basic byte size now
    if ( isize >= 5 )
    {
        // compose the ljmp instruction
        int i;
        unsigned char * nops = new_instr + 5;
        
        // calculate offset (relative jump)
        int offset = jmp_target - (((void*)in_fn_addr) + 5);
        
        new_instr[ 0 ] = 0xE9;
        memcpy( &new_instr[ 1 ], &offset, 4 );
        
        for ( i = (isize - 5); i > 0; i-- )
        {
            // insert no-op instructions here
            *nops++ = 0x90;
        }
        
        // return size of instruction data to save
        *pSize = (size_t) isize;
        
        // we leave the actual swap to the calling code, to try & get it happening
        //  as atomically as possible.
    }
    else
    {
        // should never happen, but...
        result = 0;
    }
    
    return ( result );
}

#endif  /* __i386__ */
