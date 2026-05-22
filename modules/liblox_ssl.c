#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include "../vm.h"

#ifdef HAVE_BEARSSL
#include <bearssl.h>
#include "TrustAnchors.h"

typedef struct {
    SocketInternal so;

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char* iobuf;
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
        setLastError(9, "Cannot write to an uninitialized or failed SSL context.");
        return NIL_VAL;
    }

    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;
    ObjString* data = AS_STRING(args[0]);

    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    size_t app_len;
    unsigned char* app_buf = br_ssl_engine_sendapp_buf(eng, &app_len);

    if (app_len < (size_t)data->length) {
        setLastError(9, "Payload length exceeds BearSSL max record window size.");
        return NIL_VAL;
    }

    memcpy(app_buf, data->chars, data->length);
    br_ssl_engine_sendapp_ack(eng, data->length);

    br_ssl_engine_flush(eng, 0);

    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);

            ssize_t sent = send(ssl_so->so.fd, rbuf, rlen, 0);
            if (sent <= 0) break;

            br_ssl_engine_sendrec_ack(eng, sent);
        } else {
            break;
        }
    }

    return NUMBER_VAL(data->length);
}

static Value lox_ssl_read(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[-1]);
    if (instance->foreignPtr == NULL) {
        setLastError(9, "Cannot read from an uninitialized or failed SSL context.");
        return NIL_VAL;
    }
    SSLSocketInternal* ssl_so = (SSLSocketInternal*)instance->foreignPtr;


    br_ssl_engine_context* eng = &ssl_so->sc.eng;

    size_t capacity = 4096;
    unsigned char* plaintext_buf = malloc(capacity);
    size_t total_plaintext = 0;

    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_RECVAPP) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_recvapp_buf(eng, &rlen);

            //ObjString* result_string = copyString((const char*)rbuf, rlen);
            if (rlen > 0) {
                if (total_plaintext + rlen > capacity) {
                    while (total_plaintext + rlen > capacity) {
                        capacity *= 2;
                    }
                    plaintext_buf = realloc(plaintext_buf, capacity);
                }

                memcpy(plaintext_buf + total_plaintext, rbuf, rlen);
                total_plaintext += rlen;
                br_ssl_engine_recvapp_ack(eng, rlen);
                break;
            }
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);
            if (wlen == 0) break;

            ssize_t received = recv(ssl_so->so.fd, wbuf, wlen, 0);
            if (received <= 0) break;//return OBJ_VAL(copyString("", 0));

            br_ssl_engine_recvrec_ack(eng, received);
        } else if (state & BR_SSL_CLOSED) {
            break;
            //return OBJ_VAL(copyString("", 0));
        } else {
            break;
            //return OBJ_VAL(copyString("", 0));
        }
    }
    if (total_plaintext > 0) {
        Value result = OBJ_VAL(copyString((const char*)plaintext_buf, total_plaintext));
        free(plaintext_buf);
        return result;
    }

    free(plaintext_buf);
    return NIL_VAL;
}

static Value lox_ssl_init(int argCount, Value* args) {
    // args[0] -> host
    // args[1] -> socket instance
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_INSTANCE(args[1])) {
        setLastError(22, "Initialization failed: init() expects a host string and a connected socket instance.");
        return args[-1];
    }

    ObjInstance* this_instance = AS_INSTANCE(args[-1]);
    ObjString* host = AS_STRING(args[0]);
    ObjInstance* so_instance = AS_INSTANCE(args[1]);

    if (so_instance->foreignPtr == NULL) {
        setLastError(22, "Initialization failed: The passed socket instance is uninitialized.");
        return args[-1];
    }

    SocketInternal* old_so = (SocketInternal*)so_instance->foreignPtr;
    SSLSocketInternal* ssl_so = malloc(sizeof(SSLSocketInternal));

    memcpy(&ssl_so->so, old_so, sizeof(SocketInternal));

    ssl_so->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);

    br_ssl_client_init_full(&ssl_so->sc, &ssl_so->xc, TAs, TAs_NUM);

    br_ssl_engine_set_buffer(&ssl_so->sc.eng, ssl_so->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_client_reset(&ssl_so->sc, host->chars, 0);

    br_ssl_engine_context* eng = &ssl_so->sc.eng;
    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);
        
        if (state == BR_SSL_CLOSED) {
            break;
        }

        if (state & BR_SSL_SENDAPP) {
            break;
        }

        if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
            ssize_t sent = send(ssl_so->so.fd, rbuf, rlen, 0);
            if (sent <= 0) break;
            br_ssl_engine_sendrec_ack(eng, sent);
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);

            if (wlen == 0) {
                break;
            }

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
        } else if (err == BR_ERR_X509_EXPIRED) {
            setLastError(err, "TLS Handshake Failed: The remote server's certificate has expired.");
        } else {
            setLastError(err, "TLS Handshake Failed: BearSSL Error Code %d", err);
        }
        this_instance->foreignPtr = NULL;
        return args[-1];
    }
    // overwrite this objects foreign pointer with our extended
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

