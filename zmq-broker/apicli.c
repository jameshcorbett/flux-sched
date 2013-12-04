/* apicli.c - flux_t implementation for UNIX domain socket */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "cmb.h"
#include "util.h"
#include "flux.h"
#include "handle.h"

#define CMB_CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int rank;
    zlist_t *resp;
    zlist_t *event;
    zlist_t *snoop;
    flux_t h;
} cmb_t;

static const struct flux_handle_ops cmb_ops;

static int cmb_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return zmsg_send_fd_typemask (c->fd, FLUX_MSGTYPE_REQUEST, zmsg);
}

static zmsg_t *cmb_response_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;
    int typemask;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!(z = zlist_pop (c->resp))) {
        while ((z = zmsg_recv_fd_typemask (c->fd, &typemask, nonblock))) {
            if ((typemask & FLUX_MSGTYPE_RESPONSE)) {
                break;
            } else if ((typemask & FLUX_MSGTYPE_EVENT)) {
                if (zlist_append (c->event, z) < 0)
                    oom ();
            } else if ((typemask & FLUX_MSGTYPE_SNOOP)) {
                if (zlist_append (c->snoop, z) < 0)
                    oom ();
            }
        }
    }
    return z;
}

static int cmb_response_putmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (zlist_append (c->resp, *zmsg) < 0)
        return -1;
    *zmsg = NULL;
    return 0;
}

static int cmb_snoop_subscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.snoop.subscribe.%s", s ? s: "");
}

static int cmb_snoop_unsubscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.snoop.unsubscribe.%s", s ? s: "");
}

static int cmb_event_subscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.event.subscribe.%s", s ? s: "");
}

static int cmb_event_unsubscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.event.unsubscribe.%s", s ? s: "");
}

static int cmb_event_sendmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return zmsg_send_fd_typemask (c->fd, FLUX_MSGTYPE_EVENT, zmsg);
}

static zmsg_t *cmb_event_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;
    int typemask;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!(z = zlist_pop (c->event))) {
        while ((z = zmsg_recv_fd_typemask (c->fd, &typemask, nonblock))) {
            if ((typemask & FLUX_MSGTYPE_EVENT)) {
                break;
            } else if ((typemask & FLUX_MSGTYPE_RESPONSE)) {
                if (zlist_append (c->resp, z) < 0)
                    oom ();
            } else if ((typemask & FLUX_MSGTYPE_SNOOP)) {
                if (zlist_append (c->snoop, z) < 0)
                    oom ();
            }
        }
    }
    return z;
}

static zmsg_t *cmb_snoop_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;
    int typemask;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!(z = zlist_pop (c->snoop))) {
        while ((z = zmsg_recv_fd_typemask (c->fd, &typemask, nonblock))) {
            if ((typemask & FLUX_MSGTYPE_SNOOP)) {
                break;
            } else if ((typemask & FLUX_MSGTYPE_RESPONSE)) {
                if (zlist_append (c->resp, z) < 0)
                    oom ();
            } else if ((typemask & FLUX_MSGTYPE_EVENT)) {
                if (zlist_append (c->event, z) < 0)
                    oom ();
            }
        }
    }
    return z;
}

static int cmb_rank (void *impl)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (c->rank == -1) {
        if (flux_info (c->h, &c->rank, NULL, NULL) < 0)
            return -1;
    }
    return c->rank;
}

static void cmb_fini (void *impl)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (c->fd >= 0)
        (void)close (c->fd);
    free (c);
}

flux_t cmb_init_full (const char *path, int flags)
{
    cmb_t *c = NULL;
    struct sockaddr_un addr;

    c = xzmalloc (sizeof (*c));
    if (!(c->resp = zlist_new ()))
        oom ();
    if (!(c->snoop = zlist_new ()))
        oom ();
    if (!(c->event = zlist_new ()))
        oom ();
    c->magic = CMB_CTX_MAGIC;
    c->rank = -1;
    c->fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0)
        goto error;
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (connect (c->fd, (struct sockaddr *)&addr,
                         sizeof (struct sockaddr_un)) < 0)
        goto error;
    c->h = handle_create (c, &cmb_ops, flags);
    return c->h;
error:
    if (c)
        cmb_fini (c);
    return NULL;
}

flux_t cmb_init (void)
{
    const char *val;
    char path[PATH_MAX + 1];
    int flags = 0;

    if ((val = getenv ("CMB_API_PATH")) || (val = getenv ("FLUX_API_PATH"))) {
        if (strlen (val) > PATH_MAX) {
            err ("Crazy value for CMB_API_PATH!");
            return (NULL);
        }
        strcpy(path, val);
    }
    else
        snprintf (path, sizeof (path), CMB_API_PATH_TMPL, getuid ());

    if ((val = getenv ("FLUX_TRACE_APISOCK")) && !strcmp (val, "1"))
        flags = FLUX_FLAGS_TRACE;

    return cmb_init_full (path, flags);
}

static const struct flux_handle_ops cmb_ops = {
    .request_sendmsg = cmb_request_sendmsg,
    .response_recvmsg = cmb_response_recvmsg,
    .response_putmsg = cmb_response_putmsg,
    .event_sendmsg = cmb_event_sendmsg,
    .event_recvmsg = cmb_event_recvmsg,
    .event_subscribe = cmb_event_subscribe,
    .event_unsubscribe = cmb_event_unsubscribe,
    .snoop_recvmsg = cmb_snoop_recvmsg,
    .snoop_subscribe = cmb_snoop_subscribe,
    .snoop_unsubscribe = cmb_snoop_unsubscribe,
    .rank = cmb_rank,
    .impl_destroy = cmb_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
