#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
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
    //int sock_fd;
    br_ssl_client_context sc;
    //br_x509_minimal_context xc;
    unsigned char* iobuf;
    bool handshake_done;
} LoxCryptoSocket;

typedef struct {
    int sock_fd;
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char* iobuf;
} LoxSSLSocket;

static ObjClass* sslDataClass;

void sslDestructor(ObjInstance* instance) {
    if (instance->foreignPtr == NULL) {
        return;
    }
    LoxSSLSocket* s = (LoxSSLSocket*)instance->foreignPtr;
    if (s->iobuf) {
        free(s->iobuf);
    }
    free(s);
}

static Value lox_ssl_write(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);
    LoxSSLSocket* s = (LoxSSLSocket*)instance->foreignPtr;
    ObjString* data = AS_STRING(args[1]);

    br_ssl_engine_context* eng = &s->sc.eng;
    const unsigned char* buf = (const unsigned char*)data->chars;
    size_t len = data->length;

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
            ssize_t sent = send(s->sock_fd, rbuf, rlen, 0);
            if (send <= 0) return NUMBER_VAL(-1);
            br_ssl_engine_sendrec_ack(eng, sent);
        }
    }
    return NUMBER_VAL(data->length);
}

//int lox_ssl_read(LoxSSLSocket* s, char* dest, size_t max_len) {
static Value lox_ssl_read(int argCount, Value* args) {
    ObjInstance* instance = AS_INSTANCE(args[0]);
    LoxSSLSocket* s = (LoxSSLSocket*)instance->foreignPtr;


    br_ssl_engine_context* eng = &s->sc.eng;

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

            ssize_t received = recv(s->sock_fd, wbuf, wlen, 0);
            if (received <= 0) return OBJ_VAL(copyString("", 0));

            br_ssl_engine_recvrec_ack(eng, received);
        } else if (state & BR_SSL_CLOSED) {
            return OBJ_VAL(copyString("", 0));
        } else {
            return OBJ_VAL(copyString("", 0));
        }
    }
}

void lox_ssl_free(LoxSSLSocket* s) {
    if (!s) return;
    if (s->sock_fd >= 0) {
        close(s->sock_fd);
    }
    free(s->iobuf);
    free(s);
}

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

static Value lox_ssl_wrap(int argCount, Value* args) {
    // args[0] -> host
    // args[1] -> socket instance
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_INSTANCE(args[1])) {
        runtimeError("wrap() expects a host string and a connected socket instance.");
        return NIL_VAL;
    }

    ObjString* host = AS_STRING(args[0]);
    ObjInstance* instance = AS_INSTANCE(args[1]);

    SocketInternal* so = (SocketInternal*)instance->foreignPtr;
    int fd = so->fd;

    LoxSSLSocket* s = malloc(sizeof(LoxSSLSocket));
    s->sock_fd = fd;
    s->iobuf = malloc(BR_SSL_BUFSIZE_BIDI);

    br_ssl_client_init_full(&s->sc, &s->xc, NULL, 0);
    br_ssl_engine_set_buffer(&s->sc.eng, s->iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_client_reset(&s->sc, host->chars, 0);

    br_ssl_engine_context* eng = &s->sc.eng;
    while (1) {
        unsigned int state = br_ssl_engine_current_state(eng);
        
        if (state & BR_SSL_SENDREC) {
            size_t rlen;
            unsigned char* rbuf = br_ssl_engine_sendrec_buf(eng, &rlen);
            ssize_t sent = send(fd, rbuf, rlen, 0);
            if (sent <= 0) break;
            br_ssl_engine_sendrec_ack(eng, sent);
        } else if (state & BR_SSL_RECVREC) {
            size_t wlen;
            unsigned char* wbuf = br_ssl_engine_recvrec_buf(eng, &wlen);
            ssize_t received = recv(fd, wbuf, wlen, 0);
            if (received <= 0) break;
            br_ssl_engine_recvrec_ack(eng, received);
        } else {
            break;
        }
    }

    ObjInstance* sslCtxObj = newInstance(sslDataClass);
    push(OBJ_VAL(sslCtxObj));

    sslCtxObj->foreignPtr = s;

    return pop();
}

#endif

void lox_module_init(VM* vm) {
#ifdef HAVE_BEARSSL
    ObjInstance* sslModule = newInstance(vm->moduleClass);
    push(OBJ_VAL(sslModule));

    ObjString* dataStr = copyString("SSLContext", 10);
    push(OBJ_VAL(dataStr));

    sslDataClass = newClass(dataStr);
    push(OBJ_VAL(sslDataClass));

    sslDataClass->destructor = sslDestructor;

    ObjNative* writeFn = newNative(lox_ssl_write);
    push(OBJ_VAL(writeFn));
    ObjString* write_str = copyString("write", 5);
    push(OBJ_VAL(write_str));
    tableSet(&sslDataClass->methods, write_str, OBJ_VAL(writeFn));
    pop();
    pop();

    ObjNative* readFn = newNative(lox_ssl_read);
    push(OBJ_VAL(readFn));
    ObjString* read_str = copyString("read", 4);
    push(OBJ_VAL(read_str));
    tableSet(&sslDataClass->methods, read_str, OBJ_VAL(readFn));
    pop();
    pop();

    tableSet(&sslModule->fields, dataStr, OBJ_VAL(sslDataClass));
    pop();
    pop();

    ObjNative* wrapFn = newNative(lox_ssl_wrap);
    push(OBJ_VAL(wrapFn));
    ObjString* wrap_str = copyString("wrap", 4);
    push(OBJ_VAL(wrap_str));
    tableSet(&sslModule->fields, wrap_str, OBJ_VAL(wrapFn));
    pop();
    pop();

    ObjString* sslStr = copyString("SSL", 3);
    push(OBJ_VAL(sslStr));
    tableSet(&vm->globals, sslStr, OBJ_VAL(sslModule));
    // set destructor

    pop();
    pop();
#endif
}

