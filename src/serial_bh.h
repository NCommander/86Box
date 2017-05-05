/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Bottom Half of the SERIAL card.
 *
 * Version:	@(#)serial_bh.h	1.0.1	2017/04/14
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef SERIAL_BH_H
# define SERIAL_BH_H


#define BHTTY_PORT1	"COM2"		/* port 1 connects to .. */
#define BHTTY_PORT2	"COM4"		/* port 2 connects to .. */


typedef struct {
    char	name[79];		/* name of open port */
    void	(*rd_done)(void *, int);
    void	*rd_arg;
#ifdef BHTTY_C
    HANDLE	handle;
    OVERLAPPED	rov,			/* READ and WRITE events */
		wov;
    int		tmo;			/* current timeout value */
    DCB		dcb,			/* terminal settings */
		odcb;
#endif
} BHTTY;


extern BHTTY	*bhtty_open(char *__port, int __tmo);
extern void	bhtty_close(BHTTY *);
extern int	bhtty_flush(BHTTY *);
extern void	bhtty_raw(BHTTY *, void *__arg);
extern int	bhtty_speed(BHTTY *, long __speed);
extern int	bhtty_params(BHTTY *, char __dbit, char __par, char __sbit);
extern int	bhtty_sstate(BHTTY *, void *__arg);
extern int	bhtty_gstate(BHTTY *, void *__arg);
extern int	bhtty_crtscts(BHTTY *, char __yesno);

extern int	bhtty_write(BHTTY *, unsigned char);
extern int	bhtty_read(BHTTY *, unsigned char *, int);


#endif	/*SERIAL_BH_H*/