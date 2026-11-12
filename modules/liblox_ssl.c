#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "vm.h"

#ifdef HAVE_BEARSSL
#include <bearssl.h>
#include "TrustAnchors.h"

typedef struct {
    /*
     * The SSL wrapper owns its own descriptor. It is a duplicate of the
     * descriptor from the IO socket, so the socket instance can be closed
     * or collected without invalidating the TLS connection.
     */
    SocketInternal so;

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char* iobuf;
} SSLSocketInternal;

static ObjClass* sslClass;

static void ssl_free_context(SSLSocketInternal* ssl_so) {
    if (ssl_so == NULL) {
        return;
    }
    if (ssl_so->so.fd >= 0) {
        close(ssl_so->so.fd);
        ssl_so->so.fd = -1;
    }
    free(ssl_so->iobuf);
    ssl_so->iobuf = NULL;
    free(ssl_so);
}

void sslDestructor(ObjInstance* instance) {
    if (instance->foreignPtr == NULL) {
        return;
    }
    ssl_free_context((SSLSocketInternal*)instance->foreignPtr);
    instance->foreignPtr = NULL;
}

static void ssl_report_engine_error(SSLSocketInternal* ssl_so, const char* context) {
    int err = br_ssl_engine_last_error(&ssl_so->sc.eng);

    switch (err) {
        case BR_ERR_X509_NOT_TRUSTED:
            setLastError(err, "%s: the remote certificate chain is untrusted.", context);
            break;
        case BR_ERR_X509_NOT_CA:
            setLastError(err, "%s: a certificate in the chain is not a CA.", context);
            break;
        case BR_ERR_X509_EXPIRED:
            setLastError(err, "%s: the remote server's certificate has expired.", context);
            break;
        case BR_ERR_RECV_FATAL_ALERT:
            setLastError(err, "%s: a fatal TLS alert was received.", context);
            break;
        case BR_ERR_SEND_FATAL_ALERT:
            setLastError(err, "%s: a fatal TLS alert was sent.", context);
            break;
        case BR_ERR_IO:
            setLastError(err, "%s: TLS transport I/O error.", context);
            break;
        case BR_ERR_OK:
            setLastError(err, "%s: the TLS connection was closed.", context);
            break;
        default:
            setLastError(err, "%s: BearSSL error code %d.", context, err);
            break;
    }
}

static bool ssl_socket_ready(SSLSocketInternal* ssl_so, const char* action) {
    if (ssl_so->so.fd < 0 || !ssl_so->so.connected) {
        setLastError(EBADF, "Cannot %s: the TLS connection is closed.", action);
        return false;
    }
    return true;
}

/*
 * Send all currently encrypted records. Returns false after setting the
 * last error if the transport fails.
 */
static bool ssl_flush_pending(SSLSocketInternal* ssl_so) {
    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    for (;;) {
        size_t rlen = 0;
        unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
        if (rbuf == NULL || rlen == 0) {
            return true;
        }

        ssize_t sent;
        do {
            sent = send(ssl_so->so.fd, rbuf, rlen, MSG_NOSIGNAL);
        } while (sent < 0 && errno == EINTR);

        if (sent < 0) {
            setLastError(errno, "TLS write failed: %s.", strerror(errno));
            return false;
        }
        if (sent == 0) {
            setLastError(EPIPE, "TLS write failed: connection closed by peer.");
            return false;
        }

        br_ssl_engine_sendrec_ack(eng, (size_t)sent);
    }
}

/*
 * Read one chunk from the transport into the engine.
 *
 * Returns 0 on success, 1 on EOF, and -1 on error (with the last error set).
 */
static int ssl_receive_record(SSLSocketInternal* ssl_so) {
    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    size_t wlen = 0;
    unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);
    if (wbuf == NULL || wlen == 0) {
        return 0;
    }

    ssize_t received;
    do {
        received = recv(ssl_so->so.fd, wbuf, wlen, 0);
    } while (received < 0 && errno == EINTR);

    if (received < 0) {
        setLastError(errno, "TLS read failed: %s.", strerror(errno));
        return -1;
    }
    if (received == 0) {
        return 1;
    }

    br_ssl_engine_recvrec_ack(eng, (size_t)received);
    return 0;
}

static bool ssl_handshake(SSLSocketInternal* ssl_so) {
    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    for (;;) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED) {
            ssl_report_engine_error(ssl_so, "TLS handshake failed");
            return false;
        }
        if (state & BR_SSL_SENDAPP) {
            return true;
        }

        if (state & BR_SSL_SENDREC) {
            if (!ssl_flush_pending(ssl_so)) {
                return false;
            }
        } else if (state & BR_SSL_RECVREC) {
            int rc = ssl_receive_record(ssl_so);
            if (rc > 0) {
                setLastError(EPIPE, "TLS handshake failed: connection closed by peer.");
                return false;
            }
            if (rc < 0) {
                return false;
            }
        } else if (state & BR_SSL_RECVAPP) {
            /*
             * Application data should not arrive during the handshake. Drain
             * it so an empty record cannot stall the state machine.
             */
            size_t rlen = 0;
            unsigned char* rbuf = br_ssl_engine_recvapp_buf(eng, &rlen);
            if (rbuf != NULL && rlen > 0) {
                br_ssl_engine_recvapp_ack(eng, rlen);
            } else {
                setLastError(BR_ERR_IO, "TLS handshake failed: unexpected application data.");
                return false;
            }
        } else {
            setLastError(BR_ERR_IO, "TLS handshake failed: the SSL engine stalled.");
            return false;
        }
    }
}

static Value lox_ssl_write(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) {
        setLastError(EINVAL, "write() expects a string argument.");
        return NIL_VAL;
    }

    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->foreignPtr == NULL) {
        setLastError(EBADF, "Cannot write to an uninitialized or failed SSL context.");
        return NIL_VAL;
    }

    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;
    if (!ssl_socket_ready(ssl_so, "write")) {
        return NIL_VAL;
    }

    ObjString* data = AS_STRING(args[0]);
    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    size_t offset = 0;
    while (offset < (size_t)data->length) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED) {
            ssl_report_engine_error(ssl_so, "TLS write failed");
            return NIL_VAL;
        }

        size_t app_len = 0;
        unsigned char* app_buf = br_ssl_engine_sendapp_buf(eng, &app_len);
        if (app_buf != NULL && app_len > 0) {
            size_t chunk = (size_t)data->length - offset;
            if (chunk > app_len) {
                chunk = app_len;
            }

            memcpy(app_buf, data->chars + offset, chunk);
            br_ssl_engine_sendapp_ack(eng, chunk);
            offset += chunk;

            /* Assemble a record for any partial application buffer. */
            br_ssl_engine_flush(eng, 0);
            if (!ssl_flush_pending(ssl_so)) {
                return NIL_VAL;
            }
            continue;
        }

        if (state & BR_SSL_SENDREC) {
            if (!ssl_flush_pending(ssl_so)) {
                return NIL_VAL;
            }
            continue;
        }

        if (state & BR_SSL_RECVREC) {
            int rc = ssl_receive_record(ssl_so);
            if (rc > 0) {
                setLastError(EPIPE, "TLS write failed: connection closed by peer.");
                return NIL_VAL;
            }
            if (rc < 0) {
                return NIL_VAL;
            }
            continue;
        }

        setLastError(EAGAIN, "TLS write failed: the SSL engine is not ready to send data.");
        return NIL_VAL;
    }

    return INT_VAL(data->length);
}

static Value lox_ssl_read(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->foreignPtr == NULL) {
        setLastError(EBADF, "Cannot read from an uninitialized or failed SSL context.");
        return NIL_VAL;
    }

    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;
    if (!ssl_socket_ready(ssl_so, "read")) {
        return NIL_VAL;
    }

    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    for (;;) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_RECVAPP) {
            size_t rlen = 0;
            unsigned char* rbuf = br_ssl_engine_recvapp_buf(eng, &rlen);
            if (rbuf == NULL || rlen == 0) {
                break;
            }

            /*
             * Copy before acknowledging so the engine cannot reuse the
             * buffer while copyString allocates.
             */
            Value result = OBJ_VAL(copyString((const char*)rbuf, (int)rlen));
            br_ssl_engine_recvapp_ack(eng, rlen);
            return result;
        }

        if (state & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(eng);
            if (err != BR_ERR_OK) {
                ssl_report_engine_error(ssl_so, "TLS read failed");
            }
            return NIL_VAL;
        }

        if (state & BR_SSL_SENDREC) {
            if (!ssl_flush_pending(ssl_so)) {
                return NIL_VAL;
            }
            continue;
        }

        if (state & BR_SSL_RECVREC) {
            int rc = ssl_receive_record(ssl_so);
            if (rc > 0) {
                setLastError(0, "TLS connection closed by peer.");
                return NIL_VAL;
            }
            if (rc < 0) {
                return NIL_VAL;
            }
            continue;
        }

        return NIL_VAL;
    }

    return NIL_VAL;
}

static bool ssl_is_socket_instance(ObjInstance* instance) {
    ObjClass* klass = instance->obj.klass;

    while (klass != NULL) {
        const char* name = klass->name->chars;
        if (strcmp(name, "IO") == 0 || strcmp(name, "tcp") == 0
                || strcmp(name, "udp") == 0) {
            return true;
        }
        klass = klass->superclass;
    }

    return false;
}

static Value lox_ssl_init(int argCount, Value* args) {
    /* args[0] -> host, args[1] -> connected socket instance */
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_INSTANCE(args[1])) {
        setLastError(EINVAL, "Initialization failed: init() expects a host string and a connected socket instance.");
        return args[-1];
    }

    ObjInstance* this_instance = AS_INSTANCE(args[-1]);
    ObjString* host = AS_STRING(args[0]);
    ObjInstance* so_instance = AS_INSTANCE(args[1]);

    if (!ssl_is_socket_instance(so_instance)) {
        setLastError(EINVAL, "Initialization failed: the passed instance is not an IO socket.");
        return args[-1];
    }

    if (so_instance->foreignPtr == NULL) {
        setLastError(EINVAL, "Initialization failed: the passed socket instance is uninitialized.");
        return args[-1];
    }

    SocketInternal* old_so = (SocketInternal*)so_instance->foreignPtr;
    if (old_so->fd < 0 || !old_so->connected) {
        setLastError(ENOTCONN, "Initialization failed: the passed socket is not connected.");
        return args[-1];
    }
    if (old_so->type != SOCK_STREAM) {
        setLastError(EINVAL, "Initialization failed: SSL requires a stream (TCP) socket.");
        return args[-1];
    }

    /*
     * Re-initializing an SSL instance discards the previous connection. Its
     * descriptor is a duplicate, so closing it does not affect the IO socket.
     */
    if (this_instance->foreignPtr != NULL) {
        ssl_free_context((SSLSocketInternal*)this_instance->foreignPtr);
        this_instance->foreignPtr = NULL;
    }

    SSLSocketInternal* ssl_so = calloc(1, sizeof(SSLSocketInternal));
    if (ssl_so == NULL) {
        setLastError(ENOMEM, "Initialization failed: out of memory.");
        return args[-1];
    }
    ssl_so->so.fd = -1;

    int fd = dup(old_so->fd);
    if (fd < 0) {
        setLastError(errno, "Initialization failed: could not duplicate the socket descriptor: %s.", strerror(errno));
        ssl_free_context(ssl_so);
        return args[-1];
    }
    ssl_so->so.fd = fd;
    ssl_so->so.type = SOCK_STREAM;
    ssl_so->so.connected = true;

    ssl_so->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
    if (ssl_so->iobuf == NULL) {
        setLastError(ENOMEM, "Initialization failed: out of memory for the TLS buffer.");
        ssl_free_context(ssl_so);
        return args[-1];
    }

    br_ssl_client_init_full(&ssl_so->sc, &ssl_so->xc, TAs, TAs_NUM);
    br_ssl_engine_set_buffer(&ssl_so->sc.eng, ssl_so->iobuf, BR_SSL_BUFSIZE_BIDI, 1);

    if (!br_ssl_client_reset(&ssl_so->sc, host->chars, 0)) {
        ssl_report_engine_error(ssl_so, "TLS initialization failed");
        ssl_free_context(ssl_so);
        return args[-1];
    }

    if (!ssl_handshake(ssl_so)) {
        ssl_free_context(ssl_so);
        return args[-1];
    }

    this_instance->foreignPtr = ssl_so;
    return args[-1];
}

#endif

void lox_module_init(VM* vm) {
#ifdef HAVE_BEARSSL
    ObjString* sslStr = copyString("SSL", 3);
    push(OBJ_VAL(sslStr));

    sslClass = newClass(sslStr);
    push(OBJ_VAL(sslClass));

    sslClass->destructor = sslDestructor;
    sslClass->superclass = vm->objectClass;

    ObjNative* initFn = newNative(lox_ssl_init);
    push(OBJ_VAL(initFn));
    ObjString* initStr = copyString("init", 4);
    push(OBJ_VAL(initStr));
    tableSet(&sslClass->methods, initStr, OBJ_VAL(initFn));
    pop();
    pop();

    ObjNative* writeFn = newNative(lox_ssl_write);
    push(OBJ_VAL(writeFn));
    ObjString* write_str = copyString("write", 5);
    push(OBJ_VAL(write_str));
    tableSet(&sslClass->methods, write_str, OBJ_VAL(writeFn));
    pop();
    pop();

    ObjNative* readFn = newNative(lox_ssl_read);
    push(OBJ_VAL(readFn));
    ObjString* read_str = copyString("read", 4);
    push(OBJ_VAL(read_str));
    tableSet(&sslClass->methods, read_str, OBJ_VAL(readFn));
    pop();
    pop();

    tableSet(&vm->globals, sslStr, OBJ_VAL(sslClass));

    pop();
    pop();
#endif
}
