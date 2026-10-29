/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * sockets.c - functions to deal with sockets.
 */

#ifdef _WIN32
#include "w32sock.h"	/* Winsock under the names used below */
#else
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#ifdef __VMS
#include <sys/ioctl.h>	/* FIONBIO - see SetNonBlocking below */
#endif
#endif
#include <assert.h>
#include <vncviewer.h>

void PrintInHex(char *buf, int len);

Bool errorMessageOnReadFailure = True;

#define BUF_SIZE 65536
static char buf[BUF_SIZE];
static char *bufoutptr = buf;
static unsigned int buffered = 0;

/*
 * ReadFromRFBServer is called whenever we want to read some data from the RFB
 * server.  It is non-trivial for two reasons:
 *
 * 1. For efficiency it performs some intelligent buffering, avoiding invoking
 *    the read() system call too often.  For small chunks of data, it simply
 *    copies the data out of an internal buffer.  For large amounts of data it
 *    reads directly into the buffer provided by the caller.
 *
 * 2. Whenever read() would block, it invokes the Xt event dispatching
 *    mechanism to process X events.  In fact, this is the only place these
 *    events are processed, as there is no XtAppMainLoop in the program.
 */

#ifdef __VMS

/*
 * DECwindows Xt does not deliver XtAppAddInput() callbacks for socket
 * descriptors - registering one succeeds and then never fires, so the
 * loop below would block for ever.  select() does work on VMS sockets,
 * so poll the socket and drain the X queue in between instead.
 */

#define VMS_POLL_MSEC 20

static void
ProcessXtEvents()
{
#ifdef VNCSTATS
  double start = StatsTime();
#endif

  for (;;) {
    while (XtAppPending(appContext))
      StatsProcessEvent(XtIMAll);

    if (sessionRestartPending)
      break;

    if (VmsSocketReady(rfbsock, VMS_POLL_MSEC))
      break;
  }

  STATS(vncStats.sockWaits++);
  STATS(vncStats.waitTime += StatsTime() - start);
}

#else /* !__VMS */

static Bool rfbsockReady = False;
static XtInputId rfbsockInput = 0;

static void
rfbsockReadyCallback(XtPointer clientData, int *fd, XtInputId *id)
{
  rfbsockReady = True;
  XtRemoveInput(*id);
  rfbsockInput = 0;
}

static void
ProcessXtEvents()
{
#ifdef VNCSTATS
  double start = StatsTime();
#endif

  rfbsockReady = False;
  rfbsockInput = XtAppAddInput(appContext, rfbsock, (XtPointer)XtInputReadMask,
			       rfbsockReadyCallback, NULL);
  while (!rfbsockReady && !sessionRestartPending) {
    StatsProcessEvent(XtIMAll);
  }

  /* The wait can also end because something we dispatched asked for a
     reconnect, in which case the callback never fired. */
  if (rfbsockInput) {
    XtRemoveInput(rfbsockInput);
    rfbsockInput = 0;
  }

  STATS(vncStats.sockWaits++);
  STATS(vncStats.waitTime += StatsTime() - start);
}

#endif /* !__VMS */

/*
 * One read() into p, pumping X events for as long as the socket is dry.
 * Returns the byte count, or -1 on error or EOF.
 *
 * It also gives up on the connection when something dispatched from the X
 * queue asked for a reconnect.  That unwinds the whole read through the
 * decoders and back to the main loop, which is the only place the socket may
 * be dropped: the settings panel that asks for one runs several frames deep
 * inside this read.  Stopping mid-rectangle costs nothing, since the new
 * connection asks for the screen again from scratch.
 */

static int
ReadSock(char *p, unsigned int n)
{
  for (;;) {
    int i;

    if (sessionRestartPending)
      return -1;

    i = read(rfbsock, p, n);

    STATS(if (i > 0) { vncStats.sockIn += i; vncStats.sockReads++; });
    if (i > 0)
      return i;

    if (i == 0) {
      if (errorMessageOnReadFailure)
	ConnError("VNC server closed the connection");
      return -1;
    }
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
      ConnError("Read from VNC server failed: %s", strerror(errno));
      return -1;
    }
    ProcessXtEvents();
  }
}

/*
 * ResetReadBuffer throws away whatever the last connection left buffered, so
 * that a retry after a failed one starts the handshake on clean bytes.
 */

void
ResetReadBuffer(void)
{
  bufoutptr = buf;
  buffered = 0;
}

Bool
ReadFromRFBServer(char *out, unsigned int n)
{
  STATS(vncStats.streamIn += n);

  if (n <= buffered) {
    memcpy(out, bufoutptr, n);
    bufoutptr += n;
    buffered -= n;
    return True;
  }

  memcpy(out, bufoutptr, buffered);

  out += buffered;
  n -= buffered;

  bufoutptr = buf;
  buffered = 0;

  if (n > BUF_SIZE) {
    while (n > 0) {
      int i = ReadSock(out, n);
      if (i < 0)
	return False;
      out += i;
      n -= i;
    }
    return True;
  }

  while (buffered < n) {
    int i = ReadSock(buf + buffered, BUF_SIZE - buffered);
    if (i < 0)
      return False;
    buffered += i;
  }

  memcpy(out, bufoutptr, n);
  bufoutptr += n;
  buffered -= n;
  return True;
}


/*
 * ReadFromRFBServerPeek hands back a pointer into the read buffer instead of
 * copying data out, so the zlib decoders can inflate straight from it.  *len
 * comes back with what is available, never more than max and never zero.
 * The caller then reports what it actually used to ReadFromRFBServerSkip;
 * anything left stays buffered.  The data is valid until the next read.
 */

Bool
ReadFromRFBServerPeek(char **ptr, unsigned int max, unsigned int *len)
{
  while (buffered == 0) {
    int i;

    bufoutptr = buf;
    i = ReadSock(buf, BUF_SIZE);
    if (i < 0)
      return False;
    buffered = i;
  }

  *ptr = bufoutptr;
  *len = (buffered < max) ? buffered : max;
  return True;
}

void
ReadFromRFBServerSkip(unsigned int n)
{
  STATS(vncStats.streamIn += n);
  bufoutptr += n;
  buffered -= n;
}


/*
 * Write an exact number of bytes, and don't return until you've sent them.
 */

Bool
WriteExact(int sock, char *buf, int n)
{
  fd_set fds;
  int i = 0;
  int j;

  while (i < n) {
    j = write(sock, buf + i, (n - i));
    STATS(if (j > 0) { vncStats.sockOut += j; vncStats.sockWrites++; });
    if (j <= 0) {
      if (j < 0) {
	if (errno == EWOULDBLOCK || errno == EAGAIN) {
	  FD_ZERO(&fds);
	  FD_SET(rfbsock,&fds);

	  if (select(rfbsock+1, NULL, &fds, NULL, NULL) <= 0) {
	    ConnError("Waiting for the VNC server failed: %s",
		      strerror(errno));
	    return False;
	  }
	  j = 0;
	} else {
	  ConnError("Write to VNC server failed: %s", strerror(errno));
	  return False;
	}
      } else {
	ConnError("Write to VNC server failed");
	return False;
      }
    }
    i += j;
  }
  return True;
}


/*
 * ConnectToTcpAddr connects to the given TCP port.  It says nothing itself:
 * errno survives the close() so that the caller, which knows the host name
 * that was dialled, can report what happened.
 */

int
ConnectToTcpAddr(unsigned int host, int port)
{
  int sock, err;
  struct sockaddr_in addr;
  int one = 1;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = host;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		 (char *)&one, sizeof(one)) < 0) {
    err = errno;
    close(sock);
    errno = err;
    return -1;
  }

  return sock;
}



/*
 * FindFreeTcpPort tries to find unused TCP port in the range
 * (TUNNEL_PORT_OFFSET, TUNNEL_PORT_OFFSET + 99]. Returns 0 on failure.
 */

int
FindFreeTcpPort(void)
{
  int sock, port;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    fprintf(stderr, "%s", programName);
    perror(": FindFreeTcpPort: socket");
    return 0;
  }

  for (port = TUNNEL_PORT_OFFSET + 99; port > TUNNEL_PORT_OFFSET; port--) {
    addr.sin_port = htons((unsigned short)port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      close(sock);
      return port;
    }
  }

  close(sock);
  return 0;
}


/*
 * ListenAtTcpPort starts listening at the given TCP port.
 */

int
ListenAtTcpPort(int port)
{
  int sock;
  struct sockaddr_in addr;
  int one = 1;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    fprintf(stderr, "%s", programName);
    perror(": ListenAtTcpPort: socket");
    return -1;
  }

  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		 (const char *)&one, sizeof(one)) < 0) {
    fprintf(stderr, "%s", programName);
    perror(": ListenAtTcpPort: setsockopt");
    close(sock);
    return -1;
  }

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "%s", programName);
    perror(": ListenAtTcpPort: bind");
    close(sock);
    return -1;
  }

  if (listen(sock, 5) < 0) {
    fprintf(stderr, "%s", programName);
    perror(": ListenAtTcpPort: listen");
    close(sock);
    return -1;
  }

  return sock;
}


/*
 * AcceptTcpConnection accepts a TCP connection.
 */

int
AcceptTcpConnection(int listenSock)
{
  int sock;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);	/* socklen_t predates none of our targets, so
				   the pointer is cast rather than retyped */
  int one = 1;

  sock = accept(listenSock, (struct sockaddr *) &addr, (void *) &addrlen);
  if (sock < 0) {
    fprintf(stderr, "%s", programName);
    perror(": AcceptTcpConnection: accept");
    return -1;
  }

  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		 (char *)&one, sizeof(one)) < 0) {
    fprintf(stderr, "%s", programName);
    perror(": AcceptTcpConnection: setsockopt");
    close(sock);
    return -1;
  }

  return sock;
}


/*
 * SetNonBlocking sets a socket into non-blocking mode.
 */

Bool
SetNonBlocking(int sock)
{
#ifdef __VMS
  /* VMS fcntl() accepts F_SETFL on a socket and returns -1 without setting
     anything; FIONBIO is the way to do it there. */
  int one = 1;

  if (ioctl(sock, FIONBIO, &one) < 0) {
    ConnError("Cannot set the socket non-blocking: %s", strerror(errno));
    return False;
  }
#else
  if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
    ConnError("Cannot set the socket non-blocking: %s", strerror(errno));
    return False;
  }
#endif
  return True;
}


/*
 * StringToIPAddr - convert a host string to an IP address.
 */

Bool
StringToIPAddr(const char *str, unsigned int *addr)
{
  struct hostent *hp;

  if (strcmp(str,"") == 0) {
    *addr = 0; /* local */
    return True;
  }

  *addr = inet_addr(str);

  if (*addr != (unsigned int) -1)	/* not INADDR_NONE: not everywhere */
    return True;

  hp = gethostbyname(str);

  if (hp) {
    *addr = *(unsigned int *)hp->h_addr;
    return True;
  }

  return False;
}


/*
 * Test if the other end of a socket is on the same machine.
 */

Bool
SameMachine(int sock)
{
  struct sockaddr_in peeraddr, myaddr;
  int addrlen = sizeof(struct sockaddr_in);

  getpeername(sock, (struct sockaddr *)&peeraddr, (void *) &addrlen);
  getsockname(sock, (struct sockaddr *)&myaddr, (void *) &addrlen);

  return (peeraddr.sin_addr.s_addr == myaddr.sin_addr.s_addr);
}


/*
 * Print out the contents of a packet for debugging.
 */

void
PrintInHex(char *buf, int len)
{
  int i, j;
  char c, str[17];

  str[16] = 0;

  fprintf(stderr,"ReadExact: ");

  for (i = 0; i < len; i++)
    {
      if ((i % 16 == 0) && (i != 0)) {
	fprintf(stderr,"           ");
      }
      c = buf[i];
      str[i % 16] = (((c > 31) && (c < 127)) ? c : '.');
      fprintf(stderr,"%02x ",(unsigned char)c);
      if ((i % 4) == 3)
	fprintf(stderr," ");
      if ((i % 16) == 15)
	{
	  fprintf(stderr,"%s\n",str);
	}
    }
  if ((i % 16) != 0)
    {
      for (j = i % 16; j < 16; j++)
	{
	  fprintf(stderr,"   ");
	  if ((j % 4) == 3) fprintf(stderr," ");
	}
      str[i % 16] = 0;
      fprintf(stderr,"%s\n",str);
    }

  fflush(stderr);
}
