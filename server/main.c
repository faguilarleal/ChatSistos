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

uso del multithreading: -en aceptar mensajes, inactividad y las conexiones en paralelo

*/
#include <libwebsockets.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <time.h>

#define MAX_CLIENTS 100
#define MAX_USERNAME 50
#define MAX_STATUS 20
#define INACTIVITY_TIMEOUT 120 

//estructura de ususrios
typedef struct {
    char username[MAX_USERNAME];
    char status[MAX_STATUS];
    char ip[50];
    struct lws *wsi;
    time_t last_activity;
} User;

User users[MAX_CLIENTS];
int user_count = 0;

pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct lws_context *context;

void send_json(struct lws *wsi, cJSON *json) {
    //string del json cJSON_PrintUnformatted toma el jsson y lo converite a string
    char *json_str = cJSON_PrintUnformatted(json);
    //buffer de datos
    unsigned char buf[LWS_PRE + 2048];
    unsigned char *p = &buf[LWS_PRE];
    //copiar datos al websocket
    size_t len = strlen(json_str);
    memcpy(p, json_str, len);
    //enviar los datos al ws
    lws_write(wsi, p, len, LWS_WRITE_TEXT);
    //liberar la memoria
    free(json_str);
}

User* find_user_by_username(const char* username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return &users[i];
        }
    }
    return NULL;
}
//validar estatus
int is_valid_status(const char* status) {
    return (strcmp(status, "ACTIVO") == 0 || 
            strcmp(status, "OCUPADO") == 0 || 
            strcmp(status, "INACTIVO") == 0);
}

//actualizar status
void update_user_status(struct lws *wsi, const char* new_status) {
    pthread_mutex_lock(&users_mutex);

    for (int i = 0; i < user_count; i++) {
        // validacion del estatus
        if (users[i].wsi == wsi) {
            if (is_valid_status(new_status)) {
                strncpy(users[i].status, new_status, MAX_STATUS - 1);
                //enviar respuesta de cambio de status a todos los clientes
                cJSON *status_change = cJSON_CreateObject();
                cJSON_AddStringToObject(status_change, "type", "status_change");
                cJSON_AddStringToObject(status_change, "sender", users[i].username);
                cJSON_AddStringToObject(status_change, "content", new_status);
                //cambio de status
                for (int j = 0; j < user_count; j++) {
                    send_json(users[j].wsi, status_change);
                } 
                cJSON_Delete(status_change);
            } else {
                // invalido
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "type", "error");
                cJSON_AddStringToObject(error, "content", "Status inválido. Use ACTIVO, OCUPADO o INACTIVO");
                send_json(wsi, error);
                cJSON_Delete(error);
            }
            break;
        }
    }

    pthread_mutex_unlock(&users_mutex);
}

//inactividad
void* check_inactivity(void* arg) {
    while (1) {
        sleep(60); // Revisar cada minuto

        pthread_mutex_lock(&users_mutex);
        time_t now = time(NULL);
        for (int i = 0; i < user_count; i++) {
            // Si ha pasado más de 5 minutos sin actividad
            if (now - users[i].last_activity > INACTIVITY_TIMEOUT) {
                strncpy(users[i].status, "INACTIVO", MAX_STATUS - 1);

                // Notificar cambio de status
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "status_change");
                cJSON_AddStringToObject(json, "sender", users[i].username);
                cJSON_AddStringToObject(json, "content", "INACTIVO");
                // Broadcast del cambio de status
                for (int j = 0; j < user_count; j++) {
                    send_json(users[j].wsi, json);
                }

                cJSON_Delete(json);
            }
        }

        pthread_mutex_unlock(&users_mutex);
    }
    return NULL;
}


//registrar un nuevo usuario
int register_user(const char* username, const char* status, const char* ip, struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return -1;
        }
    }
    if (user_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&users_mutex);
        return -2;
    }
    User *new_user = &users[user_count];
    strncpy(new_user->username, username, MAX_USERNAME - 1);
    strncpy(new_user->status, is_valid_status(status) ? status : "ACTIVO", MAX_STATUS - 1);
    strncpy(new_user->ip, ip, sizeof(new_user->ip) - 1);
    new_user->last_activity = time(NULL);
    new_user->wsi = wsi;
    user_count++;
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

void broadcast_message(const char* sender, const char* content) {
    pthread_mutex_lock(&users_mutex);
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "broadcast");
    cJSON_AddStringToObject(msg, "sender", sender);
    cJSON_AddStringToObject(msg, "content", content);

    char timestamp[64];
    time_t now = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(msg, "timestamp", timestamp);

    char *json_str = cJSON_PrintUnformatted(msg);
    printf("- Enviando broadcast JSON: %s\n", json_str);
    free(json_str);

    for (int i = 0; i < user_count; i++) {
        send_json(users[i].wsi, msg);
    }
    cJSON_Delete(msg);
    pthread_mutex_unlock(&users_mutex);
}

void send_private_message(const char* sender, const char* target, const char* content) {
    pthread_mutex_lock(&users_mutex);
    User *recipient = find_user_by_username(target);
    if (recipient) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "private");
        cJSON_AddStringToObject(msg, "sender", sender);
        cJSON_AddStringToObject(msg, "target", target);
        cJSON_AddStringToObject(msg, "content", content);

        char timestamp[64];
        time_t now = time(NULL);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        cJSON_AddStringToObject(msg, "timestamp", timestamp);

        send_json(recipient->wsi, msg);
        cJSON_Delete(msg);
    }
    pthread_mutex_unlock(&users_mutex);
}


// WebSocket callback, este es el controlador de eventos
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Cliente conectado.\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            //actualizar la utlima actividad al recibir un mensaje
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < user_count; i++) {
                if (users[i].wsi == wsi) {
                    users[i].last_activity = time(NULL);
                    break;
                }
            }
            pthread_mutex_unlock(&users_mutex);

            char ip[50];
            lws_get_peer_simple(wsi, ip, sizeof(ip));
            cJSON *json = cJSON_Parse((char *)in);
            if (!json) break;

            const cJSON *type = cJSON_GetObjectItem(json, "type");
            if (!type || !cJSON_IsString(type)) {
                cJSON_Delete(json);
                break;
            }

            printf("- Tipo de mensaje recibido: %s\n", type->valuestring);

            if (strcmp(type->valuestring, "register") == 0) {
                const cJSON *username = cJSON_GetObjectItem(json, "sender"); 
                const cJSON *status = cJSON_GetObjectItem(json, "content");
                if (username && cJSON_IsString(username)) {
                    int res = register_user(username->valuestring, 
                                           status && cJSON_IsString(status) ? status->valuestring : "ACTIVO", 
                                           ip, wsi);
                    cJSON *res_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(res_json, "type", "register_response");
                    cJSON_AddStringToObject(res_json, "content", res == 0 ? "Registro exitoso" : (res == -1 ? "Usuario ya existe" : "Límite alcanzado"));
                    send_json(wsi, res_json);
                    cJSON_Delete(res_json);
                }
            } 
            else if (strcmp(type->valuestring, "broadcast") == 0) {
                const cJSON *sender = cJSON_GetObjectItem(json, "sender");
                const cJSON *content = cJSON_GetObjectItem(json, "content");
                if (sender && content && cJSON_IsString(sender) && cJSON_IsString(content)) {
                    printf("- Broadcast recibido de %s: %s\n", sender->valuestring, content->valuestring);
                    broadcast_message(sender->valuestring, content->valuestring);
                } else {
                    printf("- Estructura de broadcast incorrecta\n");
                }
            } 
            else if (strcmp(type->valuestring, "private") == 0) {
                const cJSON *sender = cJSON_GetObjectItem(json, "sender");
                const cJSON *target = cJSON_GetObjectItem(json, "target");
                const cJSON *content = cJSON_GetObjectItem(json, "content");
                if (sender && target && content && cJSON_IsString(sender) && cJSON_IsString(target) && cJSON_IsString(content)) {
                    send_private_message(sender->valuestring, target->valuestring, content->valuestring);
                }
            } 
            else if (strcmp(type->valuestring, "status_change") == 0) {
                const cJSON *status = cJSON_GetObjectItem(json, "content");
                if (status) {
                    update_user_status(wsi, status->valuestring);
                }
            } 
            else if (strcmp(type->valuestring, "list_users") == 0) {
                cJSON *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "type", "list_users_response");
                cJSON *user_list = cJSON_CreateArray();
                pthread_mutex_lock(&users_mutex);
                for (int i = 0; i < user_count; i++) {
                    cJSON_AddItemToArray(user_list, cJSON_CreateString(users[i].username));
                }
                pthread_mutex_unlock(&users_mutex);
                cJSON_AddItemToObject(response, "content", user_list);
                send_json(wsi, response);
                cJSON_Delete(response);
            } 
            else if (strcmp(type->valuestring, "user_info") == 0) {
                const cJSON *sender = cJSON_GetObjectItem(json, "sender");
                const cJSON *target = cJSON_GetObjectItem(json, "target");
                if (sender && target && cJSON_IsString(target) && cJSON_IsString(sender)) {
                    pthread_mutex_lock(&users_mutex);
                    User *u = find_user_by_username(target->valuestring);
                    pthread_mutex_unlock(&users_mutex);

                    cJSON *response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "type", "user_info_response");
                    cJSON_AddStringToObject(response, "sender", sender->valuestring);
                    cJSON_AddStringToObject(response, "target", target->valuestring);

                    cJSON *content = cJSON_CreateObject();
                    char timestamp[64];
                    time_t now = time(NULL);
                    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

                    if (u) {
                        cJSON_AddStringToObject(content, "ip", u->ip);
                        cJSON_AddStringToObject(content, "status", u->status);
                    } else {
                        cJSON_AddStringToObject(content, "ip", "Desconocida");
                        cJSON_AddStringToObject(content, "status", "Desconocido");
                    }

                    cJSON_AddItemToObject(response, "content", content);
                    cJSON_AddStringToObject(response, "timestamp", timestamp);

                    send_json(wsi, response);
                    cJSON_Delete(response);
                }
            }
            cJSON_Delete(json);
            break;
        }

        case LWS_CALLBACK_CLOSED:
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < user_count; i++) {
                if (users[i].wsi == wsi) {
                    users[i] = users[user_count - 1];
                    user_count--;
                    break;
                }
            }

            pthread_mutex_unlock(&users_mutex);

            printf("Cliente desconectado.\n");
            break;

        default:
            break;
    }
    return 0;
}

// Protocolo WebSocket
static struct lws_protocols protocols[] = {
    { "chat-protocol", callback_chat, 0, 2048 },
    { NULL, NULL, 0, 0 }
};

// Hilo que ejecuta lws_service, en este lo que pasa es que recibe los mensajes de los clientes conectados
void *websocket_service_thread(void *arg) {
    while (1) {
        //el verdadero hilo, este acepta nuevas conexiones y ve si hay nuevos datos al cliente, llama al callback si pasa algo nuevo
        lws_service(context, 100); //porque lo que hace esta instruccion es procesar eventos en el web socket
        // printf("Esperando eventos...\n");
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <nombre_del_servidor> <puerto>\n", argv[0]);
        return 1;
    }

    //tomar los argumentos
    const char* server_name = argv[1];
    int port = atoi(argv[2]);

    printf("iniciando servidor '%s' en el puerto %d ... \n", server_name, port);

    //declara una estructura llamdad info de websocket
    struct lws_context_creation_info info;
    //pone los campos de info en 0, para limpiarlo
    memset(&info, 0, sizeof(info));

    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    //el contexto tiene configuracion del puerto, estado del servidor de ws y conexiones activas
    context = lws_create_context(&info);

    if (!context) {
        fprintf(stderr, "Error al crear contexto WebSocket\n");
        return -1;
    }

    //hilo para verificar inactividad
    pthread_t inactivity_thread;
    pthread_create(&inactivity_thread, NULL, check_inactivity, NULL);

    //crear el hilo
    pthread_t service_thread;
    pthread_create(&service_thread, NULL, websocket_service_thread, NULL);


    pthread_join(service_thread, NULL);
    lws_context_destroy(context);
    return 0;
}
