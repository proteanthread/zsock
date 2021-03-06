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
 * $Id: udp_dom.c,v 1.3 2002-05-13 20:00:48 dom Exp $
 *
 * Inspired/stolen from Waterloo/NCSA telnet
 */


#include "zsock.h"
#include <net/resolver.h>


#define DOMAINBUFMAX    256


char *rip(char *);



/*
 *      Nasty internal gubbins
 */




/* Set up the question */

/* This bizarre bit of code reduces the size on a a z80
 * and also speeds thing up a little bit
 */

#ifdef SCCZ80
static void qinit( struct useek *question2)
{
   struct useek *question=question2;
#else
static void qinit( struct useek *question)
{
#endif
    question->h.flags = htons(DRD);
    question->h.qdcount = htons(1);
    question->h.ancount = 0;
    question->h.nscount = 0;
    question->h.arcount = 0;
}

/*********************************************************************/
/*  packdom
*   pack a regular text string into a packed domain name, suitable
*   for the name server.
*
*   returns length
*/
static packdom( u8_t *dst,u8_t *src )
{
    u8_t *p=src,*q,*savedst=dst;
    u8_t i,dotflag=0,defflag=0,pad;


    do {                        /* copy whole string */
        *dst = 0;
        q = dst + 1;
        while (*p && (*p != '.'))
            *q++ = *p++;

        i =  (p - src);
        if (i > 0x3f)
            return(-1);
        *dst = i;
        *q = 0;

        if (*p) {                                       /* update pointers */
            dotflag = 1;
            src = ++p;
            dst = q;
        }
        else if (dotflag==0 && defflag==0 && *sysdata.domainname) {
            p = sysdata.domainname;             /* continue packing with default */
            defflag = 1;
            src = p;
            dst = q;
        }
    }
    while (*p);
    q++;
    return((q-savedst));                  /* length of packed string */
}

/*
 *  unpackdom
 *  Unpack a compressed domain name that we have received from another
 *  host.  Handles pointers to continuation domain names -- buf is used
 *  as the base for the offset of any pointer which is present.
 *  returns the number of u8_ts at src which should be skipped over.
 *  Includes the NULL terminator in its length count.
 */
static int unpackdom( u8_t *dst, u8_t *src, u8_t *buf )
{
    int retval=0;
    u8_t i,j;
    u8_t *savesrc=src;

    while (*src) {
        j = *src;

        while ((j & 0xC0) == 0xC0) {
            if (retval==0)
                retval =  (src-savesrc+2);
            src++;
            src = &buf[(j & 0x3f)*256+*src];            /* pointer dereference */
            j = *src;
        }

        src++;
        for (i=0; i < (j & 0x3f) ; i++)
            *dst++ = *src++;

        *dst++ = '.';
    }

    *(--dst) = 0;                       /* add terminator */
    src++;                                      /* account for terminator on src */

    if (retval==0)
        retval = (int) (src-savesrc);

    return(retval);
}


/*********************************************************************
 *  sendom
 *   put together a domain lookup packet and send it
 *   uses port 53
 *       num is used as identifier
 */
static int sendom( UDPSOCKET *dom_sock, struct useek *question, u8_t *s, u16_t num, u8_t dtype )
{
    u16_t i,ulen;
    u8_t *psave,*p;

    psave = question->x;
    i = packdom(question->x,s);

    p = &(question->x[i]);
    *p++ = 0;                           /* high u8_t of qtype */
    *p++ = dtype;                       /* number is < 256, so we know high u8_t=0 */
    *p++ = 0;                           /* high u8_t of qclass */
    *p++ = DIN;                         /* qtype is < 256 */

    question->h.ident = htons(num);
    ulen = sizeof(struct dhead)+(p-psave);
    udp_write( dom_sock, (u8_t*)question, ulen );
    return( ulen);
}


/*
 * Unpack a DTYPA RR (address record)
 */
static void typea_unpacker( void *data,   /* where to put IP */
                    struct rrpart *rrp, struct useek *qp )
{
//   qp = qp;
    memcpy(data,rrp->rdata,4);   /* Save IP number */
}

/*
 * Unpack a DTYPEPTR RR
 *
 * assumes: data buffer long enough for the name.
 *          256 chars should be enough(?)
 */
static void typeptr_unpacker( void *data,
                     struct rrpart *rrp, struct useek *qp )
{
   unpackdom(data,rrp->rdata,(u8_t *)qp);
}

/*********************************************************************/
/*  ddextract
*   extract the data from a response message.
*   returns the appropriate status code and if the data is available,
*   copies it into data
*   dtype is the RR type;  unpacker is the function to get the data
*         from the RR.
*/

static int ddextract( struct useek *qp, u8_t dtype,
                        int (*unpacker)(), void *data )
{
    u16_t i,j,nans,rcode;
    struct rrpart *rrp;
    u8_t *p;
    u8_t *space;

    if ((space=calloc(260,1) ) == NULL) return (-1);


    nans = htons(qp->h.ancount);              /* number of answers */
    rcode = DRCODE & htons(qp->h.flags);      /* return code for this message*/

    if (rcode > 0) {
	free(space);
        return(rcode);
    }

    if (nans > 0 &&                                                             /* at least one answer */
    (htons(qp->h.flags) & DQR)) {                     /* response flag is set */
	p = qp->x;                 /* where question starts */
        i = unpackdom(space,p,(u8_t *)qp);    /* unpack question name */
        /*  spec defines name then  QTYPE + QCLASS = 4 BYTEs */
        p += i+4;
/*
 *  at this point, there may be several answers.  We will take the first
 *  one which has an IP number.  There may be other types of answers that
 *  we want to support later.
 */
        while (nans-- > 0) {                                    /* look at each answer */
            i = unpackdom(space,p,(u8_t *)qp);     /* answer name to unpack */
            p += i;  
	    /* account for string */
            rrp = (struct rrpart *)p;                   /* resource record here */
 /*
  *  check things which might not align on 68000 chip one BYTE at a time
  */
	    if ( htons(rrp->rtype) == dtype && rrp->rclass == htons(DIN) ) {
		unpacker(data,rrp,qp);     /* get data we're looking for */
		free(space);
		return(0);      
            }
	    j = rrp->rdlength;
            p += 10+htons(j);                         /* length of rest of RR */
        }
    }

    free(space);
    return(-1);                                         /* generic failed to parse */
}




/*
 *  getdomain
 *   Look at the results to see if our DOMAIN request is ready.
 *   It may be a timeout, which requires another query.
 */

static int udpdom(UDPSOCKET *dom_sock,struct useek *question, u16_t dtype, int (*unpacker)(), void *data )
{
    int i,uret;

    uret = udp_read(dom_sock, question, sizeof(struct useek ),0);
    if (uret < 0) {
	return(0);    /* Fail */
     }

 /* num = htons(question->h.ident); */     /* get machine number */
/*
 *  check to see if the necessary information was in the UDP response
 */

    i =  ddextract(question, dtype, unpacker, data);
#ifndef UDPDOM_CASE
    if ( i == 0 )
	return 1;
    return 0;
#else
    switch (i) {
        case 0: 
		return(1); /* we got a response */
//        case 3: 
		//return(0);              /* name does not exist */
//        case -1:
		//return( 0 );            /* strange return code from ddextract */
        default:
		return( 0 );            /* dunno */
    }
#endif
}


/*
 *  Sdomain
 *   DOMAIN based name lookup
 *   query a domain name server to get resource record data
 *       Returns the data on a machine from a particular RR type.
 *   Events generated will have this number tagged with them.
 *    Returns various negative numbers on error conditions.
 *
 *   if adddom is nonzero, add default domain
 *
 *    Returns true if we got data, false otherwise.
 */
static int Sdomain( u8_t *mname, u8_t dtype, int (*unpacker)(),
            void *data, int adddom, ipaddr_t nameserver, u8_t *timedout )
{
    UDPSOCKET *s;
    struct useek *question;
    u32_t timeout;
    u8_t i;
    u8_t *namebuff;
    int result=0;

    *timedout = 1;

    if (nameserver==0) {  /* no nameserver, give up now */
        return(0);
    }

    if ( (namebuff=calloc(DOMAINBUFMAX,1) ) == NULL) return 0;
    if ( (question=calloc(sizeof (struct useek),1) ) == NULL ) {
		free(namebuff);
		return 0;
    }

    while (*mname && *mname < 33) mname ++;   /* kill leading spaces */

    if ((*mname)==0) {
	free(namebuff);
	free(question);
        return(0);
     }

    /* Open a socket up */
    if ( (s = udp_open(nameserver,0,53,NULL,0) ) == NULL ) {
		free(namebuff);
		free(question);
		return 0;
     }

    qinit(question);              /* initialize some flag fields */

    strcpy( namebuff, mname );

    if ( adddom ) {
        if(namebuff[strlen(namebuff)-1] != '.') { /* if no trailing dot */
	    strcat(namebuff,".");
	    strcat(namebuff,sysdata.domainname);
        } else
            namebuff[ strlen(namebuff)-1] = 0;  /* kill trailing dot */
    }



    /*
     * This is not terribly good, but it attempts to use a binary
     * exponentially increasing delays.
     */

    for ( i = 2; i < 17; i*=2  ) {
        sendom(s,question,namebuff, 0xf001, dtype);     /* try UDP */
        timeout=set_ttimeout(i);
        do {
	    BUSYINT();
            if ( getk() == 27) { result=-1; goto getout; } /* ^C abort resolver*/
            if (chk_timeout( timeout ))  break; 
            if ( sock_dataready_i( (TCPSOCKET *)s )) 
		*timedout = 0;
        } while ( *timedout );
	
        if ( *timedout == 0 )
	    break;        /* got an answer */
    }

    if ( *timedout == 0 ) {
        result = udpdom(s,question,dtype, unpacker, data); /* process the received data */
    }
getout:
    udp_close( s );
    free(namebuff);
    free(question);
    return( result );
}


/*
 * Perform a nameserver lookup of specified type using specified
 *    unpacker to extract data, if found.
 */

static int do_ns_lookup( u8_t *name, 
                         u8_t dtype,
                         int (*unpacker)(), 
                         void *data) 
{

    u8_t timeout[ MAXNAMESERV ];

    int		result;
    u8_t        count,i;

    result=FALSE;
    count=TRUE;

    if (name==0) {
	return( 0 );
    }
    rip( name );

    timeout[0]=0;          /* FIXME if you change MAXNAMESERV */
    timeout[1]=0;
//  memset(timeout,0,sizeof(timeout) );
    do {

        for ( i = 0; i < sysdata.numnameserv ; ++i ) {
            if (timeout[i]==0) {
                if ((result = Sdomain( name, dtype, unpacker, data, count , sysdata.nameservers[i], &timeout[i])) == 1)  return 1;
		}
            if (result==-1) break;    
        }


        if (count-- == 0) break;
    } while (result==0);

    if (result==-1) result=0;           // S. Lawson
    return( result );
}


/*
 *      Our entry points 
 */


/*
 * resolve()
 *      convert domain name -> address resolution.
 *      returns 0 if name is unresolvable right now
 */

ipaddr_t resolve_i( char *name)
{
    ipaddr_t ipaddr;

    ipaddr = inet_addr_i(name);
    if ( ipaddr ) 
	return (ipaddr);
    if ( do_ns_lookup(name, DTYPEA, typea_unpacker, &ipaddr) ) {
       return (ipaddr);
    } else {
	return (0L);
    }
}


/*
 * reverse_addr_lookup()
 *    lookup a hostname based on IP address
 *          (PTR lookups in the .in-addr.arpa domain)
 *
 * Returns true if the info was found, false otherwise.
 *
 * assumes: name buffer is big enough for the longest hostname
 */

int reverse_addr_lookup_i( ipaddr_t ipaddr, char *name)
{
   char revname[64];
/* ipaddr has to be in little endian format for resolving to work*/
   inet_ntoa_i( htonl(ipaddr), revname ); 
   strcat(revname, ".IN-ADDR.ARPA.");

   return ( do_ns_lookup(revname, DTYPEPTR, typeptr_unpacker,name) );
}

char *rip (char *s)
{
	char *temp;
	if (temp=strchr(s,'\n')) *temp=0;
	if (temp=strchr(s,'\r')) *temp=0;
	return s;
}

