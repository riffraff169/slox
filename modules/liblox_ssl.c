#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include "../vm.h"

#ifdef HAVE_BEARSSL
#include <bearssl.h>

/*
static void bear_init() {
    br_ssl_client_context sc;
    unsigned char* iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
    br_ssl_client_init_full(&sc, &xc, TAs, TA_NUM);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_client_reset(&sc, "...", 0);
}
*/

typedef struct {
    SocketInternal so;

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char* iobuf;
    //bool handshake_done;
} SSLSocketInternal;

static ObjClass* sslClass;

void sslDestructor(ObjInstance* instance) {
    if (instance->foreignPtr == NULL) {
        return;
    }
    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;
    if (ssl_so->iobuf) {
        free(ssl_so->iobuf);
    }
    free(ssl_so);
}

static Value lox_ssl_write(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->foreignPtr == NULL) {
        setLastError(-1, "Cannot write to an uninitialized or failed SSL context.");
        return NIL_VAL;
    }

    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;
    ObjString* data = AS_STRING(args[0]);
    printf("[LOXSSLWRITE]\n");
    printf("==========\n");
    printValue(args[-1]);
    printf("\n");
    printValue(args[0]);
    printf("\n");
    printf("data.len: %d\n", data->length);
    printf("==========\n");

    br_ssl_engine_context* eng = &ssl_so->sc.eng;
    const unsigned char* buf = (const unsigned char*)data->chars;
    size_t len = data->length;

    while (len > 0) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED) {
            runtimeError("SSL connection closed unexpectedly during write.");
            return NIL_VAL;
        }

        // 1. Engine is ready
        if (state & BR_SSL_SENDAPP) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_sendapp_buf(eng, &wlen);
            if (wlen > len) wlen = len;

            memcpy(wbuf, buf, wlen);
            br_ssl_engine_sendapp_ack(eng, wlen);

            buf += wlen;
            len -= wlen;
        } else if (state & BR_SSL_SENDREC) {
            // 2. Engine needs to flush ciphertext packets to the network
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
            ssize_t sent = send(ssl_so->so.fd, rbuf, rlen, 0);
            if (sent <= 0) {
                runtimeError("Socket write error.");
                return NUMBER_VAL(-1);
            }
            br_ssl_engine_sendrec_ack(eng, sent);
        } else if (state & BR_SSL_RECVREC) {
            // 3. Engine needs to read data off wire
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_recvrec_buf(eng, &rlen);
            ssize_t received = recv(ssl_so->so.fd, rbuf, rlen, 0);
            if (received <= 0) {
                runtimeError("Socket read error during secure write sync.");
                return NIL_VAL;
            }
            br_ssl_engine_recvrec_ack(eng, received);
        } else {
            break;
        }
    }

    br_ssl_engine_flush(eng, 0);

    unsigned int final_state = br_ssl_engine_current_state(eng);
    if (final_state & BR_SSL_SENDREC) {
        size_t rlen;
        unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
        send(ssl_so->so.fd, rbuf, rlen, 0);
    }

    return NUMBER_VAL(data->length);
}

//int lox_ssl_read(LoxSSLSocket* s, char* dest, size_t max_len) {
static Value lox_ssl_read(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;


    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_RECVAPP) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_recvapp_buf(eng, &rlen);

            ObjString* result_string = copyString((const char*)rbuf, rlen);
            br_ssl_engine_recvapp_ack(eng, rlen);

            return OBJ_VAL(result_string);
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);

            ssize_t received = recv(ssl_so->so.fd, wbuf, wlen, 0);
            if (received <= 0) return OBJ_VAL(copyString("", 0));

            br_ssl_engine_recvrec_ack(eng, received);
        } else if (state & BR_SSL_CLOSED) {
            return OBJ_VAL(copyString("", 0));
        } else {
            return OBJ_VAL(copyString("", 0));
        }
    }
}

/*
void lox_ssl_free(LoxSSLSocket* s) {
    if (!s) return;
    if (s->sock_fd >= 0) {
        close(s->sock_fd);
    }
    free(s->iobuf);
    free(s);
}
*/

int ssl_write(br_ssl_engine_context* eng, int sock_fd, const void* src, size_t len) {
    const unsigned char* buf = src;

    while (len > 0) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_SENDAPP) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_sendapp_buf(eng, &wlen);
            if (wlen > len) wlen = len;

            memcpy(wbuf, buf, wlen);
            br_ssl_engine_sendapp_ack(eng, wlen);

            buf += wlen;
            len -= wlen;
        } else if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);

            ssize_t sent = send(sock_fd, rbuf, rlen, 0);
            if (sent <= 0) return -1;

            br_ssl_engine_sendrec_ack(eng, sent);
        } else if (state & BR_SSL_RECVREC) {
            break;
        }
    }
    return 0;
}

int ssl_read(br_ssl_engine_context* eng, int sock_fd, void* dest, size_t len) {
    unsigned char* buf = dest;
    size_t out_count = 0;

    while (out_count < len) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_RECVAPP) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_recvapp_buf(eng, &rlen);
            if (rlen > (len - out_count)) rlen = len - out_count;

            memcpy(buf + out_count, rbuf, rlen);
            br_ssl_engine_recvapp_ack(eng, rlen);
            out_count += rlen;

            if (out_count == len) break;
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);

            ssize_t received = recv(sock_fd, wbuf, wlen, 0);
            if (received <= 0) return -1;

            br_ssl_engine_recvrec_ack(eng, received);
        } else if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);

            ssize_t sent = send(sock_fd, rbuf, rlen, 0);
            if (sent <= 0) return -1;

            br_ssl_engine_sendrec_ack(eng, sent);
        }
    }
    return out_count;
}

static Value lox_ssl_init(int argCount, Value* args) {
    // args[0] -> host
    // args[1] -> socket instance
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_INSTANCE(args[1])) {
        runtimeError("init() expects a host string and a connected socket instance.");
        return NIL_VAL;
    }

    ObjInstance* this_instance = AS_INSTANCE(args[-1]);
    ObjString* host = AS_STRING(args[0]);
    ObjInstance* so_instance = AS_INSTANCE(args[1]);

    if (so_instance->foreignPtr == NULL) {
        runtimeError("Passed socket instance is uninitialized.");
        return NIL_VAL;
    }

    SocketInternal* old_so = (SocketInternal*)so_instance->foreignPtr;
    SSLSocketInternal* ssl_so = malloc(sizeof(SSLSocketInternal));

    memcpy(&ssl_so->so, old_so, sizeof(SocketInternal));
    /*
    ssl_so->so.fd = old_so->fd;
    ssl_so->so.type = old_so->type;
    ssl_so->so.connected = old_so->connected;
    */

    ssl_so->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);

    br_ssl_client_init_full(&ssl_so->sc, &ssl_so->xc, NULL, 0);

    //static br_x509_knownkey_context x509_dummy;

    //br_ssl_engine_set_x509(&ssl_so->sc.eng, &x509_dummy.vtable);
    br_x509_minimal_set_time(&ssl_so->xc, 0, 0);

    //br_ssl_client_set_insecure(&ssl_so->sc);
    br_ssl_engine_set_buffer(&ssl_so->sc.eng, ssl_so->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_client_reset(&ssl_so->sc, host->chars, 0);

    br_ssl_engine_context* eng = &ssl_so->sc.eng;
    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);
        
        if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
            ssize_t sent = send(ssl_so->so.fd, rbuf, rlen, 0);
            if (sent <= 0) break;
            br_ssl_engine_sendrec_ack(eng, sent);
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);
            ssize_t received = recv(ssl_so->so.fd, wbuf, wlen, 0);
            if (received <= 0) break;
            br_ssl_engine_recvrec_ack(eng, received);
        } else {
            break;
        }
    }

    unsigned int final_state = br_ssl_engine_current_state(eng);
    if (final_state == BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(eng);
        free(ssl_so->iobuf);
        free(ssl_so);

        if (err == BR_ERR_X509_NOT_TRUSTED) {
            setLastError(err, "TLS Handshake Failed: The remote certificate chain is untrusted.");
        } else if (err == BR_ERR_X509_NOT_CA) {
            setLastError(err, "TLS Handshake Failed: A certificate in the chain is not a CA.");
        } else {
            setLastError(err, "TLS Handshake Failed: BearSSL Error Code %d", err);
        }
        //return NIL_VAL;
        this_instance->foreignPtr = NULL;
        return args[-1];
    }
    // overwrite this objects foregn pointer with our extended
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
    // set destructor

    pop();
    pop();
#endif
}

