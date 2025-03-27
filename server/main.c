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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <cjson/cJSON.h> //para parcear a cjson
#include <pthread.h> 

#define MAX_CLIENTS 100
#define MAX_USERNAME 50
#define MAX_STATUS 20
#define MAX_MESSAGE 1024

typedef struct {
    char username[MAX_USERNAME];
    char status[MAX_STATUS];
    char IP[50];
    struct lws *wsi;
} User;

User users[MAX_CLIENTS];
int user_count = 0;

pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;



//registrar un nuevo usuario
int register_user(const char* username, const char* status, const char* ip, struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);
    
    // Verificar si el usuario ya existe
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&users_mutex);
            return -1;  // si existe
        }
    }
    
    //veridicar el  limite de usuarios
    if (user_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&users_mutex);
        return -2;  // limnite de usuarios alcanzado
    }
    
    // Registrar nuevo usuario
    User *new_user = &users[user_count];
    strncpy(new_user->username, username, MAX_USERNAME - 1);
    strncpy(new_user->status, status, MAX_STATUS - 1);
    strncpy(new_user->IP, ip, 49);
    new_user->wsi = wsi;
    
    user_count++;
    pthread_mutex_unlock(&users_mutex);
    
    return 0;
}



// Callback del servidor
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Cliente conectado\n");
            break;

        case LWS_CALLBACK_RECEIVE: {
            // Parsear mensaje JSON
            cJSON *json = cJSON_Parse((char *)in);
            if (!json) {
                printf("Error al parsear JSON\n");
                break;
            }

            const cJSON *type = cJSON_GetObjectItem(json, "type");
            
            if (type && type->valuestring) {
                char ip[50];
                lws_get_peer_simple(wsi, ip, sizeof(ip));
                
                if (strcmp(type->valuestring, "register") == 0) {
                    const cJSON *username = cJSON_GetObjectItem(json, "username");
                    const cJSON *status = cJSON_GetObjectItem(json, "status");
                    
                    if (username && status) {
                        int result = register_user(username->valuestring, 
                                                   status->valuestring, 
                                                   ip, wsi);
                        
                        cJSON *response = cJSON_CreateObject();
                        cJSON_AddStringToObject(response, "type", "register_response");
                        
                        if (result == 0) {
                            cJSON_AddStringToObject(response, "message", "Registro exitoso");
                        } else if (result == -1) {
                            cJSON_AddStringToObject(response, "message", "Usuario ya existe");
                        } else {
                            cJSON_AddStringToObject(response, "message", "Límite de usuarios alcanzado");
                        }
                        
                        char *json_str = cJSON_Print(response);
                        unsigned char buf[LWS_PRE + strlen(json_str) + 1];
                        unsigned char *p = &buf[LWS_PRE];
                        strcpy((char*)p, json_str);
                        
                        lws_write(wsi, p, strlen(json_str), LWS_WRITE_TEXT);
                        
                        free(json_str);
                        cJSON_Delete(response);
                    }
                }
                
            }
            
            cJSON_Delete(json);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Eliminar usuario desconectado
            pthread_mutex_lock(&users_mutex);
            for (int i = 0; i < user_count; i++) {
                if (users[i].wsi == wsi) {
                    // Mover último usuario a esta posición
                    users[i] = users[user_count - 1];
                    user_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&users_mutex);
            
            printf("Cliente desconectado\n");
            break;
        }

        default:
            break;
    }
    return 0;
}

//definicion de protocolos
static const struct lws_protocols protocols[] = {
    {"chat-protocol", callback_chat, 0, 4096},
    {NULL, NULL, 0, 0}
};

int main() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = 9000;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    struct lws_context *context = lws_create_context(&info);
    
    if (!context) {
        fprintf(stderr, "Error al crear contexto de WebSocket\n");
        return -1;
    }

    printf("Servidor de chat iniciado en ws://localhost:9000\n");
    
    while (1) {
        lws_service(context, 50);
    }

    lws_context_destroy(context);
    return 0;
}