#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "config.h"
#include "vector.h"
#include "log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <netdb.h>
#include <ev.h>

#define MAX_TCP_HANDLERS 32
#define LISTEN_BACKLOG 128
#define SESSION_SDS_VECTOR_INITIAL_SIZE 4

// tcplistener_t represents a socket listening on a port
typedef struct {
	struct ev_loop *loop;
	int sd;
	struct ev_io *watcher;
	list_t clients;
	void *data;
	void *(*cb_conn)(int, void *);
	int (*cb_recv)(int, void *, void *);
}tcplistener_t;

// tcpserver_t represents an event loop bound to multiple sockets
typedef struct {
	struct ev_loop *loop;
	tcplistener_t *listeners[MAX_TCP_HANDLERS];
	int listener_fds[MAX_TCP_HANDLERS];
	int listeners_len;
	void *data;
}tcpserver_t;

// tcpsession_t represents a client connection to the server
typedef struct {
	struct ev_loop *loop;
	int sd;
	struct ev_io *watcher;
	void *data;
	list_t clients;
	int (*cb_recv)(int, void *, void *);
	struct sockaddr_storage client_addr;
	void *ctx;
	void (*ctx_dealloc)(void *);
}tcpsession_t;

tcpserver_t *tcpserver_create(struct ev_loop *loop, void *data);
int tcpserver_bind(tcpserver_t *server,
		const char *address_and_port,
		bool rebind,
		void *(*cb_conn)(int, void *),
		int (*cb_recv)(int, void *, void *));
void tcpserver_destroy(tcpserver_t *server);

void tcpserver_stop_accepting_connections(tcpserver_t *server);

#endif
