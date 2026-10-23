/*
 * w32sock.h - Winsock under the BSD socket names sockets.c uses, with the
 * error of a failed call left in errno, where sockets.c looks for it.
 */

#ifndef W32SOCK_H
#define W32SOCK_H

#include <io.h>
#include <errno.h>
#include <winsock.h>

#define EWOULDBLOCK	WSAEWOULDBLOCK	/* NT's winsock.h leaves it out */

extern int W32SockError(int result);
extern int W32Connect(int s, const struct sockaddr *addr, int len);
extern int W32SetNonBlocking(int s);

#define read(s, buf, n)		W32SockError(recv((s), (buf), (n), 0))
#define write(s, buf, n)	W32SockError(send((s), (buf), (n), 0))
#define connect(s, addr, len)	W32SockError(W32Connect((s), (addr), (len)))
#define setsockopt(s, lvl, opt, val, len) \
	W32SockError(setsockopt((s), (lvl), (opt), (val), (len)))
#define select(n, r, w, e, t)	W32SockError(select((n), (r), (w), (e), (t)))
#define fcntl(s, cmd, flags)	W32SetNonBlocking(s)

#endif /* W32SOCK_H */
