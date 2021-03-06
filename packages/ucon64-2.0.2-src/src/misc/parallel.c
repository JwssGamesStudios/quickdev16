/*
parallel.c - miscellaneous parallel port functions

Copyright (c) 1999 - 2004       NoisyB
Copyright (c) 2001 - 2004, 2015 dbjh
Copyright (c) 2001              Caz (original BeOS code)
Copyright (c) 2002 - 2004       Jan-Erik Karlsson (Amiga code)


This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#ifdef  HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef  USE_PARALLEL

#include <stdio.h>
#include <stdlib.h>
#ifdef  HAVE_UNISTD_H
#include <unistd.h>                             // ioperm() (libc5)
#endif

#ifdef  USE_PPDEV                               // ppdev is a Linux parallel
#include <fcntl.h>                              //  port device driver
#include <linux/parport.h>
#include <linux/ppdev.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#elif   defined __linux__ && defined __GLIBC__  // USE_PPDEV
#ifdef  HAVE_SYS_IO_H                           // necessary for some Linux/PPC configs
#include <sys/io.h>                             // ioperm() (glibc), in{b, w}(), out{b, w}()
#else
#error No sys/io.h; configure with --disable-parallel
#endif
#elif   defined __OpenBSD__                     // __linux__ && __GLIBC__
#include <machine/sysarch.h>
#include <sys/types.h>
#elif   defined __BEOS__ || defined __FreeBSD__ // __OpenBSD__
#include <fcntl.h>
#elif   defined AMIGA                           // __BEOS__ || __FreeBSD__
#include <fcntl.h>
#include <devices/parallel.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/types.h>
#elif   defined _WIN32                          // AMIGA
#ifdef  _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4255) // 'function' : no function prototype given: converting '()' to '(void)'
#pragma warning(disable: 4668) // 'symbol' is not defined as a preprocessor macro, replacing with '0' for 'directives'
#pragma warning(disable: 4820) // 'bytes' bytes padding added after construct 'member_name'
#endif
#include <windows.h>
#ifdef  _MSC_VER
#pragma warning(pop)
#endif
#include <conio.h>                              // inp{w}() & outp{w}()
#ifdef  _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4820) // 'bytes' bytes padding added after construct 'member_name'
#include <io.h>                                 // access()
#pragma warning(pop)
#define F_OK 00
#endif
#include "misc/dlopen.h"
#elif   defined __CYGWIN__                      // _WIN32
#include <windows.h>                            // definition of WINAPI
#undef  _WIN32
#include "misc/dlopen.h"
#endif
#include "misc/file.h"                          // DIR_SEPARATOR_S
#include "misc/parallel.h"
#include "ucon64.h"


#if     defined __i386__ || defined __x86_64__  // GCC && x86
static inline unsigned char i386_input_byte (unsigned short);
static inline unsigned short i386_input_word (unsigned short);
static inline void i386_output_byte (unsigned short, unsigned char);
static inline void i386_output_word (unsigned short, unsigned short);
#endif


#if     defined USE_PPDEV || defined __BEOS__ || defined __FreeBSD__
static int parport_io_fd;
#ifdef  USE_PPDEV
static enum { FORWARD = 0, REVERSE } parport_io_direction;
static int parport_io_mode;
#endif
#endif

#ifdef  __BEOS__
typedef struct st_ioport
{
  unsigned int port;
  unsigned char data8;
  unsigned short data16;
} st_ioport_t;
#endif

#ifdef  AMIGA
static struct IOStdReq *parport_io_req;
static struct MsgPort *parport;
#endif


#if     defined _WIN32 || defined __CYGWIN__

#define NODRIVER_MSG "ERROR: No (working) I/O port driver. Please see the FAQ, question 4\n"

static void *io_driver;

// The original inpout32.dll only has I/O functions for byte-sized I/O
static short (__stdcall *Inp32) (short) = NULL;
static void (__stdcall *Outp32) (short, short) = NULL;

static unsigned char inpout32_input_byte (unsigned short port) { return (unsigned char) Inp32 ((short) port); }
static void inpout32_output_byte (unsigned short port, unsigned char byte) { Outp32 ((short) port, (short) byte); }

// io.dll has more functions then the ones we refer to here, but we don't need them
static char (WINAPI *PortIn) (short int) = NULL;
static short int (WINAPI *PortWordIn) (short int) = NULL;
static void (WINAPI *PortOut) (short int, char) = NULL;
static void (WINAPI *PortWordOut) (short int, short int) = NULL;
static short int (WINAPI *IsDriverInstalled) (void) = NULL;

static unsigned char io_input_byte (unsigned short port) { return PortIn (port); }
static unsigned short io_input_word (unsigned short port) { return PortWordIn (port); }
static void io_output_byte (unsigned short port, unsigned char byte) { PortOut (port, byte); }
static void io_output_word (unsigned short port, unsigned short word) { PortWordOut (port, word); }

// dlportio.dll has more functions then the ones we refer to here, but we don't need them
static unsigned char (__stdcall *DlPortReadPortUchar) (unsigned short) = NULL;
static unsigned short (__stdcall *DlPortReadPortUshort) (unsigned short) = NULL;
static void (__stdcall *DlPortWritePortUchar) (unsigned short, unsigned char) = NULL;
static void (__stdcall *DlPortWritePortUshort) (unsigned short, unsigned short) = NULL;

static unsigned char dlportio_input_byte (unsigned short port) { return DlPortReadPortUchar (port); }
static unsigned short dlportio_input_word (unsigned short port) { return DlPortReadPortUshort (port); }
static void dlportio_output_byte (unsigned short port, unsigned char byte) { DlPortWritePortUchar (port, byte); }
static void dlportio_output_word (unsigned short port, unsigned short word) { DlPortWritePortUshort (port, word); }

#if     defined __CYGWIN__ || defined __MINGW32__
// default to functions which are always available (but which generate an
//  exception on Windows NT/2000/XP/2003/Vista/7/8/8.1/10 without an I/O driver)
static unsigned char (*input_byte) (unsigned short) = i386_input_byte;
static unsigned short (*input_word) (unsigned short) = i386_input_word;
static void (*output_byte) (unsigned short, unsigned char) = i386_output_byte;
static void (*output_word) (unsigned short, unsigned short) = i386_output_word;

#elif   defined _WIN32
// The following four functions are needed because inp{w} and outp{w} seem to be macros
static unsigned char inp_func (unsigned short port) { return (unsigned char) inp (port); }
static unsigned short inpw_func (unsigned short port) { return inpw (port); }
static void outp_func (unsigned short port, unsigned char byte) { outp (port, byte); }
static void outpw_func (unsigned short port, unsigned short word) { outpw (port, word); }

// default to functions which are always available (but which generate an
//  exception on Windows NT/2000/XP/2003/Vista/7/8/8.1/10 without an I/O driver)
static unsigned char (*input_byte) (unsigned short) = inp_func;
static unsigned short (*input_word) (unsigned short) = inpw_func;
static void (*output_byte) (unsigned short, unsigned char) = outp_func;
static void (*output_word) (unsigned short, unsigned short) = outpw_func;

#endif
#endif // _WIN32 || __CYGWIN__


#if     defined __i386__ || defined __x86_64__  // GCC && x86
static inline unsigned char
i386_input_byte (unsigned short port)
{
  unsigned char byte;
  __asm__ __volatile__
  ("inb %1, %0"
    : "=a" (byte)
    : "d" (port)
  );
  return byte;
}


static inline unsigned short
i386_input_word (unsigned short port)
{
  unsigned short word;
  __asm__ __volatile__
  ("inw %1, %0"
    : "=a" (word)
    : "d" (port)
  );
  return word;
}


static inline void
i386_output_byte (unsigned short port, unsigned char byte)
{
  __asm__ __volatile__
  ("outb %1, %0"
    :
    : "d" (port), "a" (byte)
  );
}


static inline void
i386_output_word (unsigned short port, unsigned short word)
{
  __asm__ __volatile__
  ("outw %1, %0"
    :
    : "d" (port), "a" (word)
  );
}
#endif // __i386__ || __x86_64__


unsigned char
inportb (unsigned short port)
{
#ifdef  USE_PPDEV
  int ppreg = port - ucon64.parport;
  unsigned char byte;

  switch (ppreg)
    {
    case 0:                                     // data
      if (parport_io_direction == FORWARD)      // dir is forward?
        {
          parport_io_direction = REVERSE;       // change it to reverse
          ioctl (parport_io_fd, PPDATADIR, &parport_io_direction);
        }
      ioctl (parport_io_fd, PPRDATA, &byte);
      break;
    case 1:                                     // status
      ioctl (parport_io_fd, PPRSTATUS, &byte);
      break;
    case 2:                                     // control
      ioctl (parport_io_fd, PPRCONTROL, &byte);
      break;
    case 3:                                     // EPP/ECP address
      if (!(parport_io_mode & IEEE1284_ADDR))   // IEEE1284_DATA is 0!
        {
          parport_io_mode |= IEEE1284_ADDR;
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      read (parport_io_fd, &byte, 1);
      break;
    case 4:                                     // EPP/ECP data
      if (parport_io_mode & IEEE1284_ADDR)
        {
          parport_io_mode &= ~IEEE1284_ADDR;    // IEEE1284_DATA is 0
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      read (parport_io_fd, &byte, 1);
      break;
    case 0x402:                                 // ECP register
      puts ("WARNING: Ignored read from ECP register, returning 0");
      byte = 0;
      break;
    default:
      fprintf (stderr,
               "ERROR: inportb() tried to read from an unsupported port (0x%x)\n",
               port);
      exit (1);
    }
  return byte;
#elif   defined __BEOS__
  st_ioport_t temp;

  temp.port = port;
  ioctl (parport_io_fd, 'r', &temp, 0);

  return temp.data8;
#elif   defined AMIGA
  (void) port;                                  // warning remover
  ULONG wait_mask;

  parport_io_req->io_Length = 1;
  parport_io_req->io_Command = CMD_READ;

/*
  SendIO ((struct IORequest *) parport_io_req);

  wait_mask = SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F | 1L << parport->mp_SigBit;
  if (Wait (wait_mask) & (SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F))
    AbortIO ((struct IORequest *) parport_io_req);
  WaitIO ((struct IORequest *) parport_io_req);
*/

  /*
    The difference between using SendIO() and DoIO(), is that DoIO() handles
    messages etc. by itself but it will not return until the I/O is done.

    Probably have to do more error handling here :-)

    Can one CTRL-C a DoIO() request? (Or for that matter a SendIO().)
  */

  if (DoIO ((struct IORequest *) parport_io_req))
    {
      fprintf (stderr, "ERROR: Could not communicate with parallel port (%s, %d)\n",
               ucon64.parport_dev, ucon64.parport);
      exit (1);
    }

  return (unsigned char) parport_io_req->io_Data;
#elif   defined _WIN32 || defined __CYGWIN__
  return input_byte (port);
#elif   defined __i386__ || defined __x86_64__
  return i386_input_byte (port);
#elif   defined HAVE_SYS_IO_H
  return inb (port);
#endif
}


unsigned short
inportw (unsigned short port)
{
#ifdef  USE_PPDEV
  int ppreg = port - ucon64.parport;
  unsigned char buf[2];

  switch (ppreg)
    {
    case 3:                                     // EPP/ECP address
      if (!(parport_io_mode & IEEE1284_ADDR))   // IEEE1284_DATA is 0!
        {
          parport_io_mode |= IEEE1284_ADDR;
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      read (parport_io_fd, buf, 2);
      break;
    case 4:                                     // EPP/ECP data
      if (parport_io_mode & IEEE1284_ADDR)
        {
          parport_io_mode &= ~IEEE1284_ADDR;    // IEEE1284_DATA is 0
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      read (parport_io_fd, buf, 2);
      break;
    // the data, status, control and ECP registers should only be accessed in "8-bit mode"
    default:
      fprintf (stderr,
               "ERROR: inportw() tried to read from an unsupported port (0x%x)\n",
               port);
      exit (1);
    }
  return buf[0] | buf[1] << 8;                  // words are read in little endian format
#elif   defined __BEOS__
  st_ioport_t temp;

  temp.port = port;
  ioctl (parport_io_fd, 'r16', &temp, 0);

  return temp.data16;
#elif   defined AMIGA
  (void) port;                                  // warning remover
  ULONG wait_mask;

  parport_io_req->io_Length = 2;
  parport_io_req->io_Command = CMD_READ;

  if (DoIO ((struct IORequest *) parport_io_req))
    {
      fprintf (stderr, "ERROR: Could not communicate with parallel port (%s, %d)\n",
               ucon64.parport_dev, ucon64.parport);
      exit (1);
    }

  return (unsigned short) parport_io_req->io_Data;
#elif   defined _WIN32 || defined __CYGWIN__
  return input_word (port);
#elif   defined __i386__ || defined __x86_64__
  return i386_input_word (port);
#elif   defined HAVE_SYS_IO_H
  return inw (port);
#endif
}


void
outportb (unsigned short port, unsigned char byte)
{
#ifdef  USE_PPDEV
  int ppreg = port - ucon64.parport;

  switch (ppreg)
    {
    case 0:                                     // data
      if (parport_io_direction == REVERSE)      // dir is reverse?
        {
          parport_io_direction = FORWARD;       // change it to forward
          ioctl (parport_io_fd, PPDATADIR, &parport_io_direction);
        }
      ioctl (parport_io_fd, PPWDATA, &byte);
      break;
    case 2:                                     // control
      ioctl (parport_io_fd, PPWCONTROL, &byte);
      break;
    case 3:                                     // EPP/ECP address
      if (!(parport_io_mode & IEEE1284_ADDR))   // IEEE1284_DATA is 0!
        {
          parport_io_mode |= IEEE1284_ADDR;
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      write (parport_io_fd, &byte, 1);
      break;
    case 4:                                     // EPP/ECP data
      if (parport_io_mode & IEEE1284_ADDR)
        {
          parport_io_mode &= ~IEEE1284_ADDR;    // IEEE1284_DATA is 0
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      write (parport_io_fd, &byte, 1);
      break;
    case 0x402:                                 // ECP register
      puts ("WARNING: Ignored write to ECP register");
      break;
    default:
      fprintf (stderr,
               "ERROR: outportb() tried to write to an unsupported port (0x%x)\n",
               port);
      exit (1);
    }
#elif   defined __BEOS__
  st_ioport_t temp;

  temp.port = port;
  temp.data8 = byte;
  ioctl (parport_io_fd, 'w', &temp, 0);
#elif   defined AMIGA
  (void) port;                                  // warning remover
  ULONG wait_mask;

  parport_io_req->io_Length = 1;
  parport_io_req->io_Data = byte;
  parport_io_req->io_Command = CMD_WRITE;

  if (DoIO ((struct IORequest *) parport_io_req))
    {
      fprintf (stderr, "ERROR: Could not communicate with parallel port (%s, %d)\n",
               ucon64.parport_dev, ucon64.parport);
      exit (1);
    }
#elif   defined _WIN32 || defined __CYGWIN__
  output_byte (port, byte);
#elif   defined __i386__ || defined __x86_64__
  i386_output_byte (port, byte);
#elif   defined HAVE_SYS_IO_H
  outb (byte, port);
#endif
}


void
outportw (unsigned short port, unsigned short word)
{
#ifdef  USE_PPDEV
  int ppreg = port - ucon64.parport;
  unsigned char buf[2];

  // words are written in little endian format
  buf[0] = word;
  buf[1] = word >> 8;
  switch (ppreg)
    {
    case 3:                                     // EPP/ECP address
      if (!(parport_io_mode & IEEE1284_ADDR))   // IEEE1284_DATA is 0!
        {
          parport_io_mode |= IEEE1284_ADDR;
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      write (parport_io_fd, buf, 2);
      break;
    case 4:                                     // EPP/ECP data
      if (parport_io_mode & IEEE1284_ADDR)
        {
          parport_io_mode &= ~IEEE1284_ADDR;    // IEEE1284_DATA is 0
          ioctl (parport_io_fd, PPSETMODE, &parport_io_mode);
        }
      write (parport_io_fd, buf, 2);
      break;
    // the data, control and ECP registers should only be accessed in "8-bit mode"
    default:
      fprintf (stderr,
               "ERROR: outportw() tried to write to an unsupported port (0x%x)\n",
               port);
      exit (1);
    }
#elif   defined __BEOS__
  st_ioport_t temp;

  temp.port = port;
  temp.data16 = word;
  ioctl (parport_io_fd, 'w16', &temp, 0);
#elif   defined AMIGA
  (void) port;                                  // warning remover
  ULONG wait_mask;

  parport_io_req->io_Length = 2;
  parport_io_req->io_Data = word;
  parport_io_req->io_Command = CMD_WRITE;

  if (DoIO ((struct IORequest *) parport_io_req))
    {
      fprintf (stderr, "ERROR: Could not communicate with parallel port (%s, %d)\n",
               ucon64.parport_dev, ucon64.parport);
      exit (1);
    }
#elif   defined _WIN32 || defined __CYGWIN__
  output_word (port, word);
#elif   defined __i386__ || defined __x86_64__
  i386_output_word (port, word);
#elif   defined HAVE_SYS_IO_H
  outw (word, port);
#endif
}


#if     (defined __i386__ || defined __x86_64__ || defined _WIN32) && !defined USE_PPDEV
#define DETECT_MAX_CNT 1000
static int
parport_probe (unsigned short port)
{
  int i = 0;

  outportb (port, 0xaa);
  for (i = 0; i < DETECT_MAX_CNT; i++)
    if (inportb (port) == 0xaa)
      break;

  if (i < DETECT_MAX_CNT)
    {
      outportb (port, 0x55);
      for (i = 0; i < DETECT_MAX_CNT; i++)
        if (inportb (port) == 0x55)
          break;
    }

  if (i >= DETECT_MAX_CNT)
    return 0;

  return 1;
}
#endif


#ifdef  _WIN32
static LONG
new_exception_filter (LPEXCEPTION_POINTERS exception_pointers)
{
  if (exception_pointers->ExceptionRecord->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION)
    {
      fputs (NODRIVER_MSG, stderr);
      exit (1);
    }
  return EXCEPTION_CONTINUE_SEARCH;
}
#elif   defined __CYGWIN__
static EXCEPTION_DISPOSITION NTAPI
new_exception_handler (PEXCEPTION_RECORD exception_record, void *establisher_frame,
                       PCONTEXT context_record, void *dispatcher_context)
{
  (void) establisher_frame;
  (void) context_record;
  (void) dispatcher_context;
  if (exception_record->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION)
    {
      fputs (NODRIVER_MSG, stderr);
      exit (1);
    }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif


unsigned short
parport_open (unsigned short port)
{
#ifdef  USE_PPDEV
  struct timeval t;
  int capabilities = 0, ucon64_parport, x;

  if (port == PARPORT_UNKNOWN)
    port = 0;

  parport_io_fd = open (ucon64.parport_dev, O_RDWR | O_NONBLOCK);
  if (parport_io_fd == -1)
    {
      fprintf (stderr, "ERROR: Could not open parallel port device (%s)\n"
                       "       Check if you have the required privileges\n",
               ucon64.parport_dev);
      exit (1);
    }

  ioctl (parport_io_fd, PPEXCL);                // disable sharing
  if (ioctl (parport_io_fd, PPCLAIM) == -1)     // claim the device
    {
      fprintf (stderr, "ERROR: Could not get exclusive access to parallel port device (%s)\n"
                       "       Check if another module (like lp) uses the module parport\n",
               ucon64.parport_dev);
      exit (1);
    }
  t.tv_sec = 0;
  t.tv_usec = 500 * 1000;                       // set time-out to 500 milliseconds
  ioctl (parport_io_fd, PPSETTIME, &t);
/*
  ioctl (parport_io_fd, PPGETTIME, &t);
  printf ("Current time-out value: %ld microseconds\n", t.tv_usec);
*/

  ioctl (parport_io_fd, PPGETMODES, &capabilities);
//  printf ("Capabilities: %x\n", capabilities);

  if (ucon64.parport_mode == UCON64_EPP || ucon64.parport_mode == UCON64_ECP)
    if ((capabilities & (PARPORT_MODE_EPP | PARPORT_MODE_ECP)) == 0)
      puts ("WARNING: EPP or ECP mode was requested, but not available");

  // set mode for read() and write()
  if (capabilities & PARPORT_MODE_ECP)
    parport_io_mode = IEEE1284_MODE_ECP;
  else if (capabilities & PARPORT_MODE_EPP)
    parport_io_mode = IEEE1284_MODE_EPP;
  else
    parport_io_mode = IEEE1284_MODE_BYTE;
  parport_io_mode |= IEEE1284_DATA;             // default to EPP/ECP data reg
  ioctl (parport_io_fd, PPSETMODE, &parport_io_mode); //  (IEEE1284_DATA is 0...)

  x = PP_FASTREAD | PP_FASTWRITE;               // enable 16-bit transfers
  ioctl (parport_io_fd, PPSETFLAGS, &x);

  parport_io_direction = FORWARD;               // set forward direction as default
  ioctl (parport_io_fd, PPDATADIR, &parport_io_direction);
#elif   defined __BEOS__
  parport_io_fd = open ("/dev/misc/ioport", O_RDWR | O_NONBLOCK);
  if (parport_io_fd == -1)
    {
      parport_io_fd = open ("/dev/misc/parnew", O_RDWR | O_NONBLOCK);
      if (parport_io_fd == -1)
        {
          fputs ("ERROR: Could not open I/O port device (no driver)\n"
                 "       You can download the latest ioport driver from\n"
                 "       http://ucon64.sourceforge.net\n",
                 stderr);
          exit (1);
        }
      else
        {                                       // print warning, but continue
          puts ("WARNING: Support for the driver parnew is deprecated. Future versions of uCON64\n"
                "         might not support this driver. You can download the latest ioport\n"
                "         driver from http://ucon64.sourceforge.net\n");
        }
    }
#elif   defined AMIGA
  int x;

  parport = CreatePort (NULL, 0);
  if (parport == NULL)
    {
      fputs ("ERROR: Could not create the MsgPort\n", stderr);
      exit (1);
    }
  parport_io_req = CreateExtIO (parport, sizeof (struct IOExtPar));
  if (parport_io_req == NULL)
    {
      fputs ("ERROR: Could not create the I/O request structure\n", stderr);
      DeletePort (parport);
      exit (1);
    }

  // Is it possible to probe for the correct port?
  if (port == PARPORT_UNKNOWN)
    port = 0;

  x = OpenDevice (ucon64.parport_dev, port, (struct IORequest *) parport_io_req,
                  (ULONG) 0);
  if (x != 0)
    {
      fprintf (stderr, "ERROR: Could not open parallel port (%s, %x)\n",
               ucon64.parport_dev, port);
      DeleteExtIO ((struct IOExtPar *) parport_io_req);
      DeletePort (parport);
      exit (1);
    }
#elif   defined __FreeBSD__
  parport_io_fd = open ("/dev/io", O_RDWR);
  if (parport_io_fd == -1)
    {
      fputs ("ERROR: Could not open I/O port device (/dev/io)\n"
             "       (This program needs root privileges for the requested action)\n",
             stderr);
      exit (1);
    }
#endif

#if     defined __linux__ && (defined __i386__ || defined __x86_64__) && !defined USE_PPDEV
  /*
    Some code needs us to switch to the real uid and gid. However, other code
    needs access to I/O ports other than the standard printer port registers.
    We just do an iopl(3) and all code should be happy. Using iopl(3) enables
    users to run all code without being root (of course with the uCON64
    executable setuid root).
    Another reason to use iopl() and not ioperm() is that the former enables
    access to all I/O ports, while the latter enables access to ports up to
    0x3ff. For the standard parallel port hardware addresses this is not a
    problem. It *is* a problem for add-on parallel port cards which can be
    mapped to I/O addresses above 0x3ff.
  */
  if (iopl (3) == -1)
    {
      fputs ("ERROR: Could not set the I/O privilege level to 3\n"
             "       (This program needs root privileges for the requested action)\n",
             stderr);
      exit (1);                                 // Don't return, if iopl() fails port access
    }                                           //  causes core dump
#endif // __linux__ && (__i386__ || __x86_64__) && !USE_PPDEV

#ifdef  __OpenBSD__ // || defined __NetBSD__, add after feature request ;-)
  // We use i386_iopl() on OpenBSD for the same reasons we use iopl() on Linux
  //  (i386_set_ioperm() has the same limitation as ioperm()).
  if (i386_iopl (3) == -1)
    {
      fputs ("ERROR: Could not set the I/O privilege level to 3\n"
             "       (This program needs root privileges for the requested action)\n",
             stderr);
      exit (1);
    }
#endif

#if     (defined __i386__ || defined __x86_64__ || defined _WIN32) && !defined USE_PPDEV

#if     defined _WIN32 || defined __CYGWIN__
  /*
    We support the I/O port drivers inpout32.dll, io.dll and dlportio.dll,
    because using them is way easier than using UserPort or GiveIO. The drivers
    are also more reliable and seem to enable access to all I/O ports
    (dlportio.dll enables access to ports > 0x100). The downsides of
    inpout32.dll are that it's almost two times slower than UserPort and that
    it only has functions for byte-sized I/O.
  */
  char fname[FILENAME_MAX];
  int driver_found = 0;
  u_func_ptr_t sym;

  sprintf (fname, "%s" DIR_SEPARATOR_S "%s", ucon64.configdir, "dlportio.dll");
#if 0 // We must not do this for Cygwin or access() won't "find" the file
  change_mem (fname, strlen (fname), "/", 1, 0, 0, "\\", 1, 0);
#endif
  if (access (fname, F_OK) == 0)
    {
      io_driver = open_module (fname);

      driver_found = 1;
      printf ("Using %s\n\n", fname);

      sym.void_ptr = get_symbol (io_driver, "DlPortReadPortUchar");
      DlPortReadPortUchar = (unsigned char (__stdcall *) (unsigned short)) sym.func_ptr;
      sym.void_ptr = get_symbol (io_driver, "DlPortReadPortUshort");
      DlPortReadPortUshort = (unsigned short (__stdcall *) (unsigned short)) sym.func_ptr;
      sym.void_ptr = get_symbol (io_driver, "DlPortWritePortUchar");
      DlPortWritePortUchar = (void (__stdcall *) (unsigned short, unsigned char)) sym.func_ptr;
      sym.void_ptr = get_symbol (io_driver, "DlPortWritePortUshort");
      DlPortWritePortUshort = (void (__stdcall *) (unsigned short, unsigned short)) sym.func_ptr;

      input_byte = dlportio_input_byte;
      input_word = dlportio_input_word;
      output_byte = dlportio_output_byte;
      output_word = dlportio_output_word;
    }

  if (!driver_found)
    {
      sprintf (fname, "%s" DIR_SEPARATOR_S "%s", ucon64.configdir, "io.dll");
      if (access (fname, F_OK) == 0)
        {
          io_driver = open_module (fname);

          sym.void_ptr = get_symbol (io_driver, "IsDriverInstalled");
          IsDriverInstalled = (short int (WINAPI *) (void)) sym.func_ptr;
          if (IsDriverInstalled ())
            {
              driver_found = 1;
              printf ("Using %s\n\n", fname);

              sym.void_ptr = get_symbol (io_driver, "PortIn");
              PortIn = (char (WINAPI *) (short int)) sym.func_ptr;
              sym.void_ptr = get_symbol (io_driver, "PortWordIn");
              PortWordIn = (short int (WINAPI *) (short int)) sym.func_ptr;
              sym.void_ptr = get_symbol (io_driver, "PortOut");
              PortOut = (void (WINAPI *) (short int, char)) sym.func_ptr;
              sym.void_ptr = get_symbol (io_driver, "PortWordOut");
              PortWordOut = (void (WINAPI *) (short int, short int)) sym.func_ptr;

              input_byte = io_input_byte;
              input_word = io_input_word;
              output_byte = io_output_byte;
              output_word = io_output_word;
            }
        }
    }

  if (!driver_found)
    {
      sprintf (fname, "%s" DIR_SEPARATOR_S "%s", ucon64.configdir, "inpout32.dll");
      if (access (fname, F_OK) == 0)
        {
          io_driver = open_module (fname);

          driver_found = 1;
          printf ("Using %s\n\n", fname);

          /*
            Newer ports of inpout32.dll also contain the API provided by
            dlportio.dll. Since the API of dlportio.dll does not have the flaws
            of inpout32.dll (*signed* short return value and arguments), we
            prefer it if it is present.
          */
          sym.void_ptr = has_symbol (io_driver, "DlPortReadPortUchar");
          DlPortReadPortUchar = (unsigned char (__stdcall *) (unsigned short)) sym.func_ptr;
          if (DlPortReadPortUchar != (void *) -1)
            input_byte = dlportio_input_byte;
          else
            {
              sym.void_ptr = get_symbol (io_driver, "Inp32");
              Inp32 = (short (__stdcall *) (short)) sym.func_ptr;

              input_byte = inpout32_input_byte;
            }

          sym.void_ptr = has_symbol (io_driver, "DlPortWritePortUchar");
          DlPortWritePortUchar = (void (__stdcall *) (unsigned short, unsigned char)) sym.func_ptr;
          if (DlPortWritePortUchar != (void *) -1)
            output_byte = dlportio_output_byte;
          else
            {
              sym.void_ptr = get_symbol (io_driver, "Out32");
              Outp32 = (void (__stdcall *) (short, short)) sym.func_ptr;

              output_byte = inpout32_output_byte;
            }

          sym.void_ptr = has_symbol (io_driver, "DlPortReadPortUshort");
          DlPortReadPortUshort = (unsigned short (__stdcall *) (unsigned short)) sym.func_ptr;
          if (DlPortReadPortUshort != (void *) -1)
            input_word = dlportio_input_word;

          sym.void_ptr = has_symbol (io_driver, "DlPortWritePortUshort");
          DlPortWritePortUshort = (void (__stdcall *) (unsigned short, unsigned short)) sym.func_ptr;
          if (DlPortReadPortUshort != (void *) -1)
            output_word = dlportio_output_word;
        }
    }

  /*
    __try and __except are not supported by MinGW and Cygwin. MinGW has __try1
    and __except1, but using them requires more code than we currently have.
    Cygwin does something stupid which breaks SetUnhandledExceptionFilter()...
  */
  {
#ifdef  _WIN32                                  // MinGW & Visual C++
    LPTOP_LEVEL_EXCEPTION_FILTER org_exception_filter =
      SetUnhandledExceptionFilter ((LPTOP_LEVEL_EXCEPTION_FILTER) new_exception_filter);
    input_byte (0x378); // 0x378 is okay (don't use this function's parameter)
    // if we get here accessing I/O port 0x378 did not cause an exception
    SetUnhandledExceptionFilter (org_exception_filter);
#else                                           // Cygwin
    EXCEPTION_REGISTRATION exception_registration;
    exception_registration.handler = new_exception_handler;

    __asm__ __volatile__
    ("movl %%fs:0, %0\n"
     "movl %1, %%fs:0"
      : "=a" (exception_registration.prev)
      : "b" (&exception_registration)
    );
    input_byte (0x378); // 0x378 is okay (don't use this function's parameter)
    // if we get here accessing I/O port 0x378 did not cause an exception
    __asm__ __volatile__
    ("movl %0, %%fs:0"
      :
      : "r" (exception_registration.prev)
    );
#endif
  }
#endif // _WIN32 || __CYGWIN__

  if (port == PARPORT_UNKNOWN)                  // no port specified or forced?
    {
      unsigned short parport_addresses[] = { 0x3bc, 0x378, 0x278 };
      int x, found = 0;

      for (x = 0; x < 3; x++)
        if ((found = parport_probe (parport_addresses[x])) == 1)
          {
            port = parport_addresses[x];
            break;
          }

      if (found != 1)
        {
          fputs ("ERROR: Could not find a parallel port on your system\n"
                 "       Try to specify it on the command line or in the configuration file\n\n",
                 stderr);
          exit (1);
        }
    }
#endif // (__i386__ || __x86_64__ || _WIN32) && !USE_PPDEV

#ifdef  USE_PPDEV
  // the following two calls need a valid value for ucon64.parport
  ucon64_parport = ucon64.parport;
  ucon64.parport = port;
#endif
  outportb (port + PARPORT_CONTROL, inportb (port + PARPORT_CONTROL) & 0x0f);
  // bit 4 = 0 => IRQ disable for ACK, bit 5-7 unused
#ifdef  USE_PPDEV
  ucon64.parport = ucon64_parport;
#endif

  return port;
}


void
parport_close (void)
{
#ifdef  USE_PPDEV
  parport_io_mode = IEEE1284_MODE_COMPAT;
  // We really don't want to perform IEEE 1284 negotiation, but if we don't do
  //  it (older versions of) ppdev will do it for us...
  ioctl (parport_io_fd, PPNEGOT, &parport_io_mode);
  ioctl (parport_io_fd, PPRELEASE);
  close (parport_io_fd);
#elif   defined __BEOS__ || defined __FreeBSD__
  close (parport_io_fd);
#elif   defined AMIGA
  CloseDevice ((struct IORequest *) parport_io_req);
  DeleteExtIO ((struct IOExtPar *) parport_io_req);
  DeletePort (parport);
  parport_io_req = NULL;
#elif   defined _WIN32 || defined __CYGWIN__
  input_byte = NULL;
  input_word = NULL;
  output_byte = NULL;
  output_word = NULL;
#endif
}


void
parport_print_info (void)
{
#ifdef  USE_PPDEV
  printf ("Using parallel port device: %s\n", ucon64.parport_dev);
#elif   defined AMIGA
  printf ("Using parallel port device: %s, port %d\n", ucon64.parport_dev, ucon64.parport);
#else
  printf ("Using I/O port base address: 0x%x\n", ucon64.parport);
#endif
}

#else

// Define at least one symbol, so that the translation unit is not empty if
//  USE_PARALLEL is not defined.
void
parport_dummy (void)
{
}

#endif // USE_PARALLEL
