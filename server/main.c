/*
Servidor: mantiene una lista de todos los clientes/usuarios contectados del sistema.
solo puede haber 1 servidor en la ejecucion del chat
puede ejecutarse en cualquier mauquina linux 

este se tiene que ejecutar como un proceso independiente con el comando:
<nombredelservidor><puertodelservidor>

el servidor tiene que estar pendiente de conexiones y mensajes de sus clientes

tiene que atender las conexiones de sus clientes multithreading y no puede 
permitir dos usuarios con el mismo nombre.

Servicios: 
- Registro de usuarios
- Liberacion de usuarios
- Listado de usuarios conectados
- Obtencion de informacion de usuario
- Cambio de estatus
- Boracasting y mensajes directos

*/
#include <libwebsockets.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#define MAX_CLIENTS 100
#define MAX_USERNAME 50
#define MAX_STATUS 20

//estructura de ususrios
typedef struct {
    char username[MAX_USERNAME];
    char status[MAX_STATUS];
    char ip[50];
    struct lws *wsi;
} User;

User users[MAX_CLIENTS];
int user_count = 0;

pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct lws_context *context;

//registrar un nuevo usuario
int register_user(const char* username, const char* status, const char* ip, struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);

    // Verificar si el usuario ya existe
    for (int i =0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return -1; // ya existe
        }
    }

    if (user_count>= MAX_CLIENTS) {
        pthread_mutex_unlock(&users_mutex);
        return -2;  // limnite de usuarios alcanzado
    }

    // Registrar nuevo usuario
    User *new_user = &users[user_count];
    strncpy(new_user->username, username, MAX_USERNAME - 1);
    strncpy(new_user->status, status, MAX_STATUS - 1);
    strncpy(new_user->ip, ip, sizeof(new_user->ip) - 1);
    new_user->wsi = wsi;

    user_count++;
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

//enviar json
void send_json(struct lws *wsi, cJSON *json) {
    char *json_str = cJSON_PrintUnformatted(json);
    unsigned char buf[LWS_PRE + 1024];
    unsigned char *p = &buf[LWS_PRE];
    size_t len = strlen(json_str);
    memcpy(p, json_str, len);
    lws_write(wsi, p, len, LWS_WRITE_TEXT);
    free(json_str);
}

//lista de usuarios activos
void send_user_list(struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);

    cJSON *array = cJSON_CreateArray();
    //verificar que sean activos
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].status, "activo") == 0) {
            cJSON_AddItemToArray(array, cJSON_CreateString(users[i].username));
        }
    }
    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "users_list");
    cJSON_AddItemToObject(res, "users", array);
    send_json(wsi, res);
    cJSON_Delete(res);

    pthread_mutex_unlock(&users_mutex);
}

void send_user_info(struct lws *wsi, const char* username) {
    pthread_mutex_lock(&users_mutex);

    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            cJSON *res = cJSON_CreateObject();
            cJSON_AddStringToObject(res, "type", "user_info");
            cJSON_AddStringToObject(res, "username", users[i].username);
            cJSON_AddStringToObject(res, "status", users[i].status);
            cJSON_AddStringToObject(res, "IP", users[i].ip);
            send_json(wsi, res);
            cJSON_Delete(res);
            pthread_mutex_unlock(&users_mutex);
            return;
        }
    }

    pthread_mutex_unlock(&users_mutex);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "type", "error");
    cJSON_AddStringToObject(res, "message", "Usuario no encontrado");
    send_json(wsi, res);
    cJSON_Delete(res);
}

// WebSocket callback
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Cliente conectado.\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            char ip[50];
            lws_get_peer_simple(wsi, ip, sizeof(ip));
            cJSON *json = cJSON_Parse((char *)in);
            if (!json) break;

            const cJSON *type = cJSON_GetObjectItem(json, "type");
            if (type && strcmp(type->valuestring, "register") == 0) {
                const cJSON *username = cJSON_GetObjectItem(json, "username");
                const cJSON *status = cJSON_GetObjectItem(json, "status");
                int res = register_user(username->valuestring, status->valuestring, ip, wsi);

                cJSON *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "type", "register_response");

                if (res == 0)
                    cJSON_AddStringToObject(response, "message", "Registro exitoso");
                else if (res == -1)
                    cJSON_AddStringToObject(response, "message", "Usuario ya existe");
                else
                    cJSON_AddStringToObject(response, "message", "Limite alcanzado");

                send_json(wsi, response);
                cJSON_Delete(response);
            }
            else if (strcmp(type->valuestring, "list_users") == 0) {
                send_user_list(wsi);
            }
            else if (strcmp(type->valuestring, "user_info") == 0) {
                const cJSON *username = cJSON_GetObjectItem(json, "username");
                if (username) {
                    send_user_info(wsi, username->valuestring);
                }
            }

            cJSON_Delete(json);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            //eiminar usuario desconectado
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < user_count; i++) {
                if (users[i].wsi == wsi) {
                    //mover ultimo usuario a esta posición
                    users[i] = users[user_count - 1];
                    user_count--;
                    break;
                }
            }

            pthread_mutex_unlock(&users_mutex);

            printf("Cliente desconectado.\n");
            break;
        }

        default:
            break;
    }

    return 0;
}

// Protocolo WebSocket
static struct lws_protocols protocols[] = {
    {
        .name = "chat-protocol",
        .callback = callback_chat,
        .per_session_data_size = 0,
        .rx_buffer_size = 0
    },
    { NULL, NULL, 0, 0 }
};

// Hilo que ejecuta lws_service
void *websocket_service_thread(void *arg) {
    while (1) {
        lws_service(context, 100);
    }
    return NULL;
}

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = 8080;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    context = lws_create_context(&info);

    if (!context) {
        fprintf(stderr, "Error al crear contexto WebSocket\n");
        return -1;
    }

    //crear el hilo
    pthread_t service_thread;
    pthread_create(&service_thread, NULL, websocket_service_thread, NULL);

    printf("Servidor WebSocket escuchando en ws://localhost:8080\n");

    pthread_join(service_thread, NULL);
    lws_context_destroy(context);
    return 0;
}
