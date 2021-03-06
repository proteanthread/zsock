#ifndef __NET_STDIO_H__
#define __NET_STDIO_H__

/*
 * Some hooks for the stdio style routines
 *
 * Do not include yourself - system file!!
 *
 * $Id: stdio.h,v 1.1 2002-10-08 18:59:10 dom Exp $
 */


extern int __LIB__ fgetc_net(void *s);
extern int __LIB__ fputc_net(void *s, int c);
extern int __LIB__ closenet(void *s);
extern int __LIB__ opennet(FILE *fp, char *name,char *exp, size_t len);
extern int __LIB__ fflush_net(void *s);


#endif

