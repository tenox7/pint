/*
    https: TLS 1.2 on a connected socket, done by generic\win32\w32ssl.c
    with BearSSL (sec\bearssl). After SSL_Start(), Net_Send_Async(),
    Net_Recv_Async() and Net_Close() go through TLS for that socket.
*/

#ifndef SSL_H
#define SSL_H

/* SSL_Send(), SSL_Flush() and SSL_Recv() return this when they have to
   wait: wait for the socket events SSL_Events() returns, then call again. */
#define SSL_WOULDBLOCK  (-2)

BOOL SSL_Start(int socket, const char *pszHost);
BOOL SSL_IsSecure(int socket);
int SSL_Send(int socket, const void *pBuf, int nLen);
int SSL_Flush(int socket);
int SSL_Recv(int socket, void *pBuf, int nLen);
long SSL_Events(int socket);
const char *SSL_ErrorText(int socket);
void SSL_Close(int socket);

#endif /* SSL_H */
