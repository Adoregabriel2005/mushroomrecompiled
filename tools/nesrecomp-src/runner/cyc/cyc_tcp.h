/*
 * cyc_tcp.h - the cycle host's TCP debug server: JSON over newline on
 * 127.0.0.1, one client at a time, polled once per host loop (never
 * blocking). The protocol is the function-level runtime's (TCP.md): send
 * {"cmd":"state","id":7}\n, receive one line of JSON ({"id":7,"ok":true,...}
 * or {"id":7,"ok":false,"err":"..."}); a bare command name also works.
 *
 * The window (cyc_sdl.c) registers its commands: input actions and device
 * input through the bindings, the Disk action, the runtime menu, the HLE
 * axes, screenshots of the picture and of everything the window presents
 * (picture, toast, menu, dev bar), the state, the always-on event ring, quit.
 * With it every window check runs on a hidden or offscreen window.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CycTcpHandler)(int id, const char *line);

bool cyc_tcp_start(int port);          /* listen; false if the port cannot be bound */
void cyc_tcp_poll(void);               /* accept, read, dispatch complete lines */
void cyc_tcp_stop(void);
int  cyc_tcp_port(void);               /* 0 when not listening */
void cyc_tcp_register(const char *name, const char *summary, CycTcpHandler handler);

/* Replies. cyc_tcp_ok's fields are JSON members without braces ("\"frame\":3"),
 * or NULL. A handler may answer later (a screenshot taken at the next frame);
 * the reply goes to whichever client is connected then. */
void cyc_tcp_ok(int id, const char *fields);
void cyc_tcp_err(int id, const char *message);

/* Request fields: a string or number member of the request line. */
bool cyc_tcp_str(const char *line, const char *key, char *out, size_t n);
bool cyc_tcp_long(const char *line, const char *key, long *out);
bool cyc_tcp_bool(const char *line, const char *key, bool *out);
/* A JSON string literal of `s` (quotes, escapes) into out. */
void cyc_tcp_quote(const char *s, char *out, size_t n);

#ifdef __cplusplus
}
#endif
