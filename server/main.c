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

void send_json(struct lws *wsi, cJSON *json);


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
        if (users[i].wsi == wsi) {
            // validacion del estatus
            if (is_valid_status(new_status)) {
                strncpy(users[i].status, new_status, MAX_STATUS - 1);
                
                //enviar respuesta de cambio de status a todos los clientes
                cJSON *status_change = cJSON_CreateObject();
                cJSON_AddStringToObject(status_change, "type", "status_change");
                cJSON_AddStringToObject(status_change, "username", users[i].username);
                cJSON_AddStringToObject(status_change, "status", new_status);
                
                //cambio de status
                for (int j = 0; j < user_count; j++) {
                    send_json(users[j].wsi, status_change);
                }
                
                cJSON_Delete(status_change);
            } else {
                // invalido
                cJSON *error = cJSON_CreateObject();
                cJSON_AddStringToObject(error, "type", "error");
                cJSON_AddStringToObject(error, "message", "Status inválido. Use ACTIVO, OCUPADO o INACTIVO");
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
        time_t current_time = time(NULL);
        
        for (int i = 0; i < user_count; i++) {
            // Si ha pasado más de 5 minutos sin actividad
            if (current_time - users[i].last_activity > INACTIVITY_TIMEOUT) {
                strncpy(users[i].status, "INACTIVO", MAX_STATUS - 1);
                
                // Notificar cambio de status
                cJSON *status_change = cJSON_CreateObject();
                cJSON_AddStringToObject(status_change, "type", "status_change");
                cJSON_AddStringToObject(status_change, "username", users[i].username);
                cJSON_AddStringToObject(status_change, "status", "INACTIVO");
                
                // Broadcast del cambio de status
                for (int j = 0; j < user_count; j++) {
                    send_json(users[j].wsi, status_change);
                }
                
                cJSON_Delete(status_change);
            }
        }
        
        pthread_mutex_unlock(&users_mutex);
    }
    return NULL;
}


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

    if (!is_valid_status(status)) {
        strncpy(new_user->status, "ACTIVO", MAX_STATUS - 1);
    } else {
        strncpy(new_user->status, status, MAX_STATUS - 1);
    }
    
    strncpy(new_user->ip, ip, sizeof(new_user->ip) - 1);
    new_user->last_activity = time(NULL); 

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
        if (strcmp(users[i].status, "ACTIVO") == 0) {
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


void send_private_message(const char* from, const char* to, const char* message) {
    pthread_mutex_lock(&users_mutex);
    User *recipient = find_user_by_username(to);
    if (recipient) {
        cJSON *private_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(private_msg, "type", "private");
        cJSON_AddStringToObject(private_msg, "from", from);
        cJSON_AddStringToObject(private_msg, "message", message);
        send_json(recipient->wsi, private_msg);
        cJSON_Delete(private_msg);
        

    }
    pthread_mutex_unlock(&users_mutex);
}

void broadcast_message(const char* from, const char* message) {
    pthread_mutex_lock(&users_mutex);
    cJSON *group_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(group_msg, "type", "broadcast");
    cJSON_AddStringToObject(group_msg, "from", from);
    cJSON_AddStringToObject(group_msg, "message", message);
    char *json_str = cJSON_PrintUnformatted(group_msg);
    for (int i = 0; i < user_count; i++) {
        unsigned char buf[LWS_PRE + 1024];
        unsigned char *p = &buf[LWS_PRE];
        size_t len = strlen(json_str);
        memcpy(p, json_str, len);
        lws_write(users[i].wsi, p, len, LWS_WRITE_TEXT);
        
    }
    free(json_str);
    cJSON_Delete(group_msg);
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
            //lita de usuarios
            else if (strcmp(type->valuestring, "list_users") == 0) {
                send_user_list(wsi);
            }
            //info de un usuario en especifico
            else if (strcmp(type->valuestring, "user_info") == 0) {
                const cJSON *username = cJSON_GetObjectItem(json, "username");
                if (username) {
                    send_user_info(wsi, username->valuestring);
                }
            }
            //broadcast
            else if (strcmp(type->valuestring, "broadcast") == 0) {
                const cJSON *from = cJSON_GetObjectItem(json, "from");
                const cJSON *message = cJSON_GetObjectItem(json, "message");
                if (from && message) {
                    broadcast_message(from->valuestring, message->valuestring);
                }
            } 
            //mensajes privados
            else if (strcmp(type->valuestring, "private") == 0) {
                const cJSON *from = cJSON_GetObjectItem(json, "from");
                const cJSON *to = cJSON_GetObjectItem(json, "to");
                const cJSON *message = cJSON_GetObjectItem(json, "message");
                if (from && to && message) {
                    send_private_message(from->valuestring, to->valuestring, message->valuestring);
                }
            }
            //cambiar el estatus
            else if (strcmp(type->valuestring, "status_change") == 0) {
                const cJSON *status = cJSON_GetObjectItem(json, "status");
                if (status) {
                    update_user_status(wsi, status->valuestring);
                }
            }
            

            cJSON_Delete(json);
            break;
        }

        //liberar
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

// Hilo que ejecuta lws_service, en este lo que pasa es que recibe los mensajes de los clientes conectados 
void *websocket_service_thread(void *arg) {
    while (1) {
        //el verdadero hilo, este acepta nuevas conexiones y ve si hay nuevos datos al cliente, llama al callback si pasa algo nuevo
        lws_service(context, 100); //porque lo que hace esta instruccion es procesar eventos en el web socket
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

    printf("Servidor WebSocket escuchando en ws://localhost:9000\n");

    pthread_join(service_thread, NULL);
    lws_context_destroy(context);
    return 0;
}
