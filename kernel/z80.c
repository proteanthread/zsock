/*
 * Copyright (c) 1999-2002 Dominic Morris
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Dominic Morris.
 * 4. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.  
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
 *
 * This file is part of the ZSock TCP/IP stack.
 *
 * $Id: z80.c,v 1.6 2002-06-08 16:26:03 dom Exp $
 *
 * Z80 Checksumming routines etc
 */


#include "zsock.h"


#ifdef __Z88__
u32_t current_time()
{
#asm
    INCLUDE "#time.def"
        ld      de,1
        call_oz(gn_gmt) ;abc, a=MSB
        ld      d,0
        ld      e,a
        ld      h,b
        ld      l,c
#endasm
}

u32_t GetSeqNum()
{
#asm
          ld   de,1
          call_oz (gn_gmt)
          ld    l,c
          ld    h,b
          ld   e,a
          ld   d,c      ;more random
#endasm
}
#else
#ifdef __CPM__
/* Uses extensions to ZXCC */
u32_t current_time()
{
#asm
    ld    c,0x99
    call  5
#endasm
}

u32_t GetSeqNum()
{
    return (current_time());
}
#else
/* Won't work */
u32_t current_time()
{
    return 0;
}

u32_t GetSeqNum()
{
    return 0;
}
#endif
#endif




/***************************************************************
 *
 *   Some convenient ICMP routines
 * 
 *   Fill up an ping packet with data - this is a custom routine
 *   for the Z80
 * 
 ***************************************************************/
void icmp_fill_ping(char *ptr, u16_t len)
{
#pragma asm
	pop	de
	pop	bc	;len
	pop	hl	;ptr
	push	hl
	push	bc
	push	de
	ld	a,32
	ex	af,af
.fillping1
	ld	a,b
	or	c
	ret	z
	ex	af,af
	ld	(hl),a
	inc	a
	and	63
	ex	af,af
	inc	hl
	dec	bc
	jr	fillping1
#pragma endasm
}




/**********************************************************************
 *
 *    Various combinations of checksum routines
 * 
 *    We have a variety of entry points - hence the multiple functions
 *
 **********************************************************************/ 

/* Calculate the pseudo checksum - we supply with the address of the
   fully formed IP packet, and it returns the checksum, works both
   ways and so if a valid packet is there then should return 0
*/
u16_t inet_cksum_pseudo(ip_header_t *ip,void *tcp,u8_t protocol,u16_t length)      
{
#asm
        ld      ix,2    ;use ix as frame pointer
        add     ix,sp
        ld      c,(ix+0)        ;length
        ld      b,(ix+1)
        ld      l,(ix+2)        ;protocol
        ld      e,(ix+6)        ;ip header
        ld      d,(ix+7)        
        push    bc
        call    _PsHeaderSum  
        ld      l,(ix+4)        ;tcp header
        ld      h,(ix+5)        			    
        pop     bc
	call	FastCSum_tcp
        ld      l,b
        ld      h,c
#endasm
}


/* Check the IP checksum, returns zero if good packet */
u16_t ip_check_cksum(ip_header_t *buf)
{
#asm  
        ld      a,(hl)
        and     @00001111
        rlca
        rlca
        ld      c,a
        ld      b,0
        call    FastCSum        ;exits with checksum in hl
        ld      l,c
        ld      h,b
#endasm
}

/* Set the IP Header checksum (just considers IP header) */
void __FASTCALL__  inet_cksum_set(ip_header_t *buf)
{
#asm
               ld   bc,10
               add  hl,bc
               xor  a
               ld   (hl),a
               inc  hl
               ld   (hl),a         ; Set checksum to zero
               push hl
               scf
               sbc  hl,bc
               ld   a,(hl)
               and  $0f
               rlca
               rlca
               ld   c,a
               ld   b,0
               call FastCsum
               pop  hl
               ld   (hl),c
               dec  hl
               ld   (hl),b
#endasm
}



/* Now the generic checksum routines */
u16_t inet_cksum(void *buf,u16_t len)   
{
#asm
        pop     de      ;return address
        pop     bc      ;length
        pop     hl      ;address of buffer
        push    hl
        push    bc
        push    de
        call    FastCSum
        ld      l,c
        ld      h,b
#endasm
}


/* The real checksum routines. These are lifted from the abortive
 * MSX TCP stack
 */
#asm
;Calculate 1s complement sum of pseudo IP header
;Entry:   de=ip buffer, l=protocol, bc=length
;Exit:    de=check sum

._PsHeaderSum   
               ld   h,0       ; add (0,protocol)
               add  hl,bc          ; add data length
	       ex   af,af     ; save carry
               ld   bc,12
               ex   de,hl
	       add  hl,bc     ;step to IP addresses
               ex   af,af
               ld   b,4       ; add 4 words (source, destination)
.PsHead_1
               ld   a,(hl)
               inc  hl
               adc  a,d
               ld   d,a
               ld   a,(hl)
               inc  hl
               adc  a,e
               ld   e,a
               djnz PsHead_1
               ld   a,d       ; add last carry
               adc  a,0
               ld   d,a
               ld   a,e
               adc  a,0
               ld   e,a
               ld   a,d
               adc  a,0
               ld   d,a
               ret

/* 
 * Assembler Checksum routine
 *
 * Enter: hl = start, bc = length
 * Exit:  bc = cksum, hl = start, de=de
 */

.FastCSum
;	       push hl
;               push de
               ld   de,0      ; de=sum
.FastCsum_tcp
               ld   a,c
               ld   c,b       ;  swap b,c
               srl  c
               rra            ; adjust counter for words
               ld   b,a       ; (cb=#words)
               ex   af,af        ; save cary for a single byte
	       ld   a,b
               or   c         ; check for zero also clear carry
               jr   z,FastCsum_2   ; Only one or less bytes
               inc  c
               ld   a,b
               or   a
               jr   z,FastCsum_1b
.FastCsum_1              ; use counter c for outer loop
               ld   a,(hl)
               inc  hl
               adc  a,d
               ld   d,a
               ld   a,(hl)
               inc  hl
               adc  a,e
               ld   e,a
               djnz FastCsum_1     ; inner loop
.FastCsum_1b
               dec  c
               jr   nz,FastCsum_1  ; outer loop
               ld   a,d
               adc  a,0
               ld   d,a
               ld   a,e
               adc  a,0
               ld   e,a
               ld   a,d
               adc  a,0
               ld   d,a
.FastCsum_2
               ex   af,af        ; check for single byte
               jr   nc,FastCsum_3
               ld   a,(hl)
               add  a,d
               ld   d,a
               ld   a,e
               adc  a,0
               ld   e,a
               ld   a,d
               adc  a,0
               ld   d,a
               ld   a,e
               adc  a,0
               ld   e,a
.FastCsum_3
               ld   a,d
               cpl
               ld   b,a
               ld   a,e
               cpl
               ld   c,a
;               pop  de
;               pop  hl
               ret
; size=256*c+b
; This costs about 27660 states/kB: 7.7 ms/kB @3.58MHz


#endasm
