
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> //mi real esta libreria
#include <cjson/cJSON.h> //para parcear a cjson

#define MAX_USERNAME 50
#define MAX_STATUS 20
#define MAX_MESSAGE 1024

// Variables globales
static struct lws *web_socket = NULL;
static char current_username[MAX_USERNAME] = {0};
static char current_status[MAX_STATUS] = "activo";
static int should_exit = 0;

// Estructura para pasarle argumentos al hilo de websocket
struct thread_args {
    struct lws_context *context;
};

// Función para cambiar el status
int change_status(struct lws *wsi) {
    char new_status[MAX_STATUS];
    printf("\nIngrese nuevo status (activo/ocupado/inactivo): ");
    scanf("%19s", new_status);

    // Validar el status
    if (strcmp(new_status, "activo") != 0 &&
        strcmp(new_status, "ocupado") != 0 &&
        strcmp(new_status, "inactivo") != 0) {
        printf("Status inválido. Manteniendo status actual.\n");
        return -1;
        }

    // Preparar mensaje JSON
    char message[256];
    snprintf(message, sizeof(message),
        "{\"type\":\"change_status\", \"username\":\"%s\", \"status\":\"%s\"}",
        current_username, new_status);

    // Enviar mensaje
    unsigned char buf[LWS_PRE + 256];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    // Actualizar status local
    strcpy(current_status, new_status);
    printf("Status cambiado a: %s\n", current_status);
    return 0;
}

//registro
int register_user(struct lws *wsi) {
    printf("\nIngrese nombre de usuario: ");
    scanf("%49s", current_username);
    printf("Ingrese status (activo/ocupado/inactivo): ");
    scanf("%19s", current_status);

    //validar status
    if (strcmp(current_status, "activo") != 0 &&
        strcmp(current_status, "ocupado") != 0 &&
        strcmp(current_status, "inactivo") != 0) {
        printf("Status invalido, se va a poner el default como 'activo'.\n");
        strcpy(current_status, "activo");
    }

    //prepararmensaje JSON de registro
    char message[256];
    snprintf(message, sizeof(message),
        "{\"type\":\"register\", \"username\":\"%s\", \"status\":\"%s\"}",
        current_username, current_status);

    //eviar mensaje de registro
    unsigned char buf[LWS_PRE + 256];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    return 0;
}

// Enviar mensaje al chat general
int send_group_message(struct lws *wsi) {
    if (strlen(current_username) == 0) {
        printf("Debes registrarte primero.\n");
        return -1;
    }

    char message[MAX_MESSAGE];
    int exit_chat = 0;

    while (!exit_chat) {
        printf("Escriba su mensaje (máximo %d caracteres) o 'salir' para abandonar el chat: ", MAX_MESSAGE - 1);
        scanf(" %[^\n]", message);

        if (strcmp(message, "salir") == 0) {
            printf("Saliendo del chat...\n");
            exit_chat = 1;  // Salir del chat
            continue;
        }

        // Preparar mensaje JSON
        char json_message[MAX_MESSAGE + 256];
        snprintf(json_message, sizeof(json_message),
            "{\"type\":\"group_message\", \"username\":\"%s\", \"message\":\"%s\"}",
            current_username, message);

        // Enviar mensaje
        unsigned char buf[LWS_PRE + sizeof(json_message)];
        unsigned char *p = &buf[LWS_PRE];
        size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", json_message);
        lws_write(wsi, p, n, LWS_WRITE_TEXT);
    }

    return 0;
}

// Enviar mensaje privado
int send_private_message(struct lws *wsi) {
    if (strlen(current_username) == 0) {
        printf("Debes registrarte primero.\n");
        return -1;
    }

    char target_username[MAX_USERNAME];
    char message[MAX_MESSAGE];

    printf("Ingrese nombre de usuario destinatario: ");
    scanf("%49s", target_username);
    printf("Escriba su mensaje (máximo %d caracteres): ", MAX_MESSAGE - 1);
    scanf(" %[^\n]", message);

    // Preparar mensaje JSON
    char json_message[MAX_MESSAGE + 256];
    snprintf(json_message, sizeof(json_message),
        "{\"type\":\"private_message\", \"from\":\"%s\", \"to\":\"%s\", \"message\":\"%s\"}",
        current_username, target_username, message);

    // Enviar mensaje
    unsigned char buf[LWS_PRE + sizeof(json_message)];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", json_message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    return 0;
}

// Solicitar lista de usuarios
int list_users(struct lws *wsi) {
    // Preparar mensaje JSON para solicitar lista de usuarios
    char message[256] = "{\"type\":\"list_users\"}";

    unsigned char buf[LWS_PRE + 256];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    return 0;
}
// Mostrar información de un usuario específico
int user_info(struct lws *wsi) {
    char username[MAX_USERNAME];
    printf("Ingrese nombre de usuario para ver información: ");
    scanf("%49s", username);

    // Preparar mensaje JSON para solicitar información de usuario
    char message[256];
    snprintf(message, sizeof(message),
        "{\"type\":\"user_info\", \"username\":\"%s\"}", username);

    unsigned char buf[LWS_PRE + 256];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", message);
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    return 0;
}


//ayuda
void show_help() {
    printf("\n===== AYUDA DEL CHAT 3000 =====\n");
    printf("\n1. Registrarse: Ingresa al sistema con un nombre de usuario\n");
    printf("\n2. Chat general: Envía mensajes a todos los usuarios\n");
    printf("\n3. Mensaje privado: Envía mensajes directos a un usuario\n");
    printf("\n4. Cambiar status: Modifica tu estado (activo/ocupado/inactivo)\n");
    printf("\n5. Listar usuarios: Muestra todos los usuarios conectados\n");
    printf("\n6. Información de usuario: Consulta detalles de un usuario\n");
    printf("\n7. Ayuda: Muestra este menú de ayuda\n");
    printf("\n8. Salir: Cierra la aplicación de chat\n");
}


void menu(struct lws *wsi) {
    int opcion;
    printf("\n======= BIENVENIDO AL CHAT 3000 🤑 ===========");
    printf("\nOpciones: ");
    printf("\n1. Registrarse");
    printf("\n2. Chatear con otros usuarios");
    printf("\n3. Enviar mensaje privado");
    printf("\n4. Cambiar de status");
    printf("\n5. Listar usuarios conectados");
    printf("\n6. Información de usuario");
    printf("\n7. Ayuda");
    printf("\n8. Salir");
	printf("\n");
    printf("\nEscoga una opcion: ");
    scanf("%d", &opcion);

    switch (opcion) {
        case 1:
			register_user(wsi);
			break;
        case 2:
            send_group_message(wsi);
			break;
        case 3:
                send_private_message(wsi);
			break;
        case 4:
            change_status(wsi);
			break;
        case 5:
            list_users(wsi);
        break;
        case 6:
            user_info(wsi);
        break;
        case 7:
            show_help();
        break;
        case 8:
            printf("👋 Chao\n");
        should_exit = 1;  // Cambia el valor para salir del bucle
        break;
        default:
            printf("Opción no valida, vuelva a intentarlo\n");
            break;
    }
}

// Callback del cliente
static int callback_client(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    switch(reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Conexion establecida con el servidor.\n");
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            //parsear mensaje JSON recibido
            cJSON *json = cJSON_Parse((char *)in);
            if (json) {
                const cJSON *type = cJSON_GetObjectItem(json, "type");

                if (type && type->valuestring) {
                    if (strcmp(type->valuestring, "register_response") == 0) {
                        const cJSON *message = cJSON_GetObjectItem(json, "message");
                        printf("%s\n", message ? message->valuestring : "Registro procesado");
                    }

                }

                cJSON_Delete(json);
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            web_socket = NULL;
            printf("Conexion cerrada o error de conexion.\n");
            break;

        default:
            break;
    }
    return 0;
}

//definicion de protocolos
enum protocols {
    PROTOCOL_CLIENT = 0,
    PROTOCOL_COUNT
};

static struct lws_protocols protocols[] = {

	{
        .name                  = "chat-protocol", // nombre del protocolo
        .callback              = callback_client,   // define la funcion de callbak con el procolo
        .per_session_data_size = 0,                  //  tamanio de los datos por sesion
        .rx_buffer_size        = 0,                  //  tamanio del buffer de recepcion, el 0 es que no hay restriccion de tam
        .id                    = 0,                  // id de la version del protocolo, puede ser opcionsl
        .user                  = NULL,               // puntero a los datos de usuario que pueden ser accesibles
        .tx_packet_size        = 0                   // restriccion del tamanio del buffer de transmicion
    },
    LWS_PROTOCOL_LIST_TERM // terminador,  final del protocolo
};

//manejar eventos de websocket con hilos
void* websocket_thread(void* arg) {
    struct thread_args *args = (struct thread_args*)arg;
    struct lws_context *context = args->context;

    while (!should_exit && lws_service(context, 50) >= 0) {
        // Procesar eventos de websocket
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    // configuracion de contexto de websocket
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo = {0};
    struct lws *wsi;

    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;

    // Crear contexto de websocket
    struct lws_context *context = lws_create_context(&info);
    if (context == NULL) {
        fprintf(stderr, "Error al crear el contexto de WebSocket.\n");
        return -1;
    }

    //conexion de cliente
    ccinfo.context = context;
    ccinfo.address = "localhost";
    ccinfo.port = 8080;
    ccinfo.path = "/";
    ccinfo.host = lws_canonical_hostname(context);
    ccinfo.origin = "origin";
    ccinfo.protocol = protocols[0].name;
    ccinfo.pwsi = &wsi;

    wsi = lws_client_connect_via_info(&ccinfo);
    if (wsi == NULL) {
        fprintf(stderr, "Error al conectar con el servidor.\n");
        lws_context_destroy(context);
        return -1;
    }

    //crear hilo para manejar eventos de websocket
    pthread_t ws_thread_id;
    struct thread_args args = { context };
    pthread_create(&ws_thread_id, NULL, websocket_thread, &args);

    //menu
    while (!should_exit) {
        menu(wsi);
    }

    //limpiar
    pthread_join(ws_thread_id, NULL);
    lws_context_destroy(context);
    return 0;
}
