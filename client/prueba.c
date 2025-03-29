#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

#define MAX_USERNAME 50
#define MAX_STATUS 20
#define MAX_MESSAGE 1024

// Variables globales
static struct lws *web_socket = NULL;
static char current_username[MAX_USERNAME] = {0};
static char current_status[MAX_STATUS] = "activo";
static int should_exit = 0;

// Registrar al usuario automáticamente
int register_user(struct lws *wsi, const char *username, const char *status) {
    // Validar status
    if (strcmp(current_status, "activo") != 0 &&
        strcmp(current_status, "ocupado") != 0 &&
        strcmp(current_status, "inactivo") != 0) {
        printf("Status inválido. Estableciendo como 'activo'.\n");
        strcpy(current_status, "activo");
    }

    strcpy(current_username, username);
    strcpy(current_status, status);

    // Preparar mensaje JSON de registro
    char message[256];
    snprintf(message, sizeof(message),
        "{\"type\":\"register\", \"username\":\"%s\", \"status\":\"%s\"}",
        current_username, current_status);

    // Enviar mensaje de registro
    unsigned char buf[LWS_PRE + 256];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    return 0;
}

// Función para enviar un mensaje general
void send_message(struct lws *wsi, const char *message) {
    unsigned char buf[LWS_PRE + MAX_MESSAGE];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);
}



// Callback del cliente
static int callback_client(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch(reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Conexión establecida con el servidor.\n");

            // Registrar al usuario automáticamente después de la conexión
            register_user(wsi, current_username, current_status);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            cJSON *json = cJSON_Parse((char *)in);
            if (json) {
                const cJSON *type = cJSON_GetObjectItem(json, "type");
                if (type && type->valuestring) {
                    if (strcmp(type->valuestring, "message") == 0) {
                        const cJSON *username = cJSON_GetObjectItem(json, "username");
                        const cJSON *message = cJSON_GetObjectItem(json, "message");
                        printf("[%s]: %s\n", username->valuestring, message->valuestring);
                    }
                    if (strcmp(type->valuestring, "users") == 0) {
                        const cJSON *users = cJSON_GetObjectItem(json, "users");
                        printf("Usuarios conectados:\n");
                        for (int i = 0; i < cJSON_GetArraySize(users); i++) {
                            const cJSON *user = cJSON_GetArrayItem(users, i);
                            printf("- %s\n", user->valuestring);
                        }
                    }
                }
                cJSON_Delete(json);
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            web_socket = NULL;
            printf("Conexión cerrada o error de conexión.\n");
            break;

        default:
            break;
    }
    return 0;
}

enum protocols {
    PROTOCOL_CLIENT = 0,
    PROTOCOL_COUNT
};

static struct lws_protocols protocols[] = {
    {
        .name = "chat-protocol",
        .callback = callback_client,
        .per_session_data_size = 0,
        .rx_buffer_size = 0,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0
    },
    LWS_PROTOCOL_LIST_TERM
};

// Manejar comandos
void handle_command(struct lws *wsi, const char *command) {
    if (strncmp(command, "$state ", 7) == 0) {
        // Cambiar estado
        const char *new_state = command + 7;
        if (strcmp(new_state, "activo") == 0 || strcmp(new_state, "ocupado") == 0 || strcmp(new_state, "inactivo") == 0) {
            strcpy(current_status, new_state);
            char message[256];
            snprintf(message, sizeof(message), "{\"type\":\"state\", \"username\":\"%s\", \"status\":\"%s\"}", current_username, current_status);
            send_message(wsi, message);
        } else {
            printf("Estado inválido. Usa 'activo', 'ocupado' o 'inactivo'.\n");
        }
    } else if (strncmp(command, "$all ", 5) == 0) {
        // Enviar mensaje al grupo
        send_message(wsi, command + 5);
    } else if (strncmp(command, "$desconectar", 12) == 0) {
        // Desconectar
        should_exit = 1;
    } else if (strncmp(command, "$users", 6) == 0) {
        // Listar usuarios
        char message[256];
        snprintf(message, sizeof(message), "{\"type\":\"list_users\"}");
        send_message(wsi, message);
    } else if (command[0] == '$' && strlen(command) > 1) {
        // Enviar mensaje privado
        char recipient[MAX_USERNAME];
        char message[MAX_MESSAGE];
        if (sscanf(command, "$%s %[^\n]", recipient, message) == 2) {
            char private_message[256];
            snprintf(private_message, sizeof(private_message), "{\"type\":\"private_message\", \"from\":\"%s\", \"to\":\"%s\", \"message\":\"%s\"}", current_username, recipient, message);
            send_message(wsi, private_message);
        }

        else if (strcmp(command, "$help") == 0) {
            printf("\nComandos:\n");
            printf("$state <estado>: Cambia tu estado (activo/ocupado/inactivo)\n");
            printf("$all <mensaje>: Envia un mensaje al chat general\n");
            printf("$<usuario> <mensaje>: Envia un mensaje privado\n");
            printf("$users: Lista los usuarios conectados\n");
            printf("$desconectar: Desconecta al usuario\n");
            // Asegura que no bloqueas el flujo
            fflush(stdout);
        }


    } else {
        printf("Comando no reconocido. Usa $help para ver los comandos disponibles.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <nombredeusuario> <IPdelservidor> <puertodelservidor>\n", argv[0]);
        return -1;
    }

    const char *username = argv[1];
    const char *server_ip = argv[2];
    int server_port = atoi(argv[3]);

    // Configuración de WebSocket
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN; // Sin servidor, solo cliente
    info.protocols = protocols;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        printf("Error al crear el contexto de WebSocket\n");
        return -1;
    }

    printf("Conectando al servidor...\n");

    // Aquí configuramos la conexión con el servidor WebSocket.
    struct lws_client_connect_info i = {0};
    i.context = context;
    i.address = server_ip; // Dirección del servidor (puede ser una IP o dominio).
    i.port = server_port;  // Puerto del servidor WebSocket
    i.path = "ws";         // Ruta del servidor WebSocket (puede variar según tu configuración)
    i.host = i.address;    // Host, generalmente es la misma que la dirección
    i.origin = "http://localhost";  // Origin, debe coincidir con lo que el servidor espera
    i.protocol = protocols[0].name; // El protocolo que estamos usando
    i.ssl_connection = 0;    // Si usas SSL, configura esta opción

    // Conectar al servidor WebSocket
    web_socket = lws_client_connect_via_info(&i);

    if (web_socket == NULL) {
        printf("Error al conectar al servidor WebSocket\n");
        return -1;
    }

    printf("Conectado al servidor WebSocket\n");

    // Registrar usuario inicial
    register_user(web_socket, username, "activo");

    // Bucle principal del WebSocket
    while (!should_exit) {
        // Procesar eventos de WebSocket
        int n = lws_service(context, 50);  // El segundo parámetro es el tiempo de espera (50 ms)

        // Verificar si se debe salir del bucle
        if (n < 0) {
            // Si lws_service devuelve un valor negativo, hay un error en la comunicación
            fprintf(stderr, "Error en lws_service, cerrando WebSocket.\n");
            break;
        }

        // Escuchar comandos del usuario
        char command[MAX_MESSAGE];
        printf("Ingrese un comando: ");
        if (fgets(command, sizeof(command), stdin) != NULL) {
            handle_command(web_socket, command);
        }
    }

    lws_context_destroy(context);
    return 0;
}
