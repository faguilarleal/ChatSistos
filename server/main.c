#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CLIENTS 10

static struct lws *clients[MAX_CLIENTS] = {NULL};

static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Client connected\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == NULL) {
                clients[i] = wsi;
                break;
            }
        }
        break;

        case LWS_CALLBACK_RECEIVE:
            lwsl_user("Received: %s\n", (char *)in);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i] != wsi) {
                lws_write(clients[i], (unsigned char *)in, len, LWS_WRITE_TEXT);
            }
        }
        break;

        case LWS_CALLBACK_CLOSED:
            lwsl_user("Client disconnected\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == wsi) {
                clients[i] = NULL;
                break;
            }
        }
        break;

        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {"chat-protocol", callback_chat, 0, 4096},
    {NULL, NULL, 0, 0}
};

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = 9000;
    info.protocols = protocols;
    struct lws_context *context = lws_create_context(&info);

    if (!context) {
        lwsl_err("Failed to create WebSocket context\n");
        return -1;
    }

    lwsl_user("WebSocket server started on ws://localhost:9000\n");

    while (1) {
        lws_service(context, 100);
    }

    lws_context_destroy(context);
    return 0;
}
