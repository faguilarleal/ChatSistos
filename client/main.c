
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
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

int enviar_json(cJSON *json, struct lws *wsi) {
    // Convertir JSON a una cadena
    char *msg_str = cJSON_PrintUnformatted(json);
    if (msg_str == NULL) {
        printf("Error al convertir JSON a cadena.\n");
        return -1;
    }

    size_t len = strlen(msg_str);

    // Reservar memoria para el buffer (incluyendo espacio para LWS_PRE)
    unsigned char *buffer = malloc(LWS_PRE + len);
    if (buffer == NULL) {
        printf("Error al reservar memoria para el buffer.\n");
        free(msg_str);
        return -1;
    }

    // Copiar la cadena JSON al buffer (empezando después de LWS_PRE)
    memcpy(buffer + LWS_PRE, msg_str, len);

    // Llamar a lws_callback_on_writable y enviar el mensaje
    lws_callback_on_writable(wsi);
    lws_write(wsi, buffer + LWS_PRE, len, LWS_WRITE_TEXT);

    // Liberar la memoria después de enviar
    free(buffer);  // Liberar el buffer
    free(msg_str); // Liberar la cadena JSON

    // Eliminar el objeto JSON
    cJSON_Delete(json);

    return 0;
}

// Función para obtener el timestamp actual en formato de texto
void get_current_timestamp(char *timestamp, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    strftime(timestamp, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Función para cambiar el status
int change_status(struct lws *wsi) {
    // Crear objeto JSON
    char msg[MAX_MESSAGE];
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("Error al crear el objeto JSON.\n");
        return -1;
    }
    printf("MENSAJE: ", MAX_MESSAGE - 1);
    fgets(msg, sizeof(msg), stdin);
    msg[strcspn(msg, "\n")] = '\0';  // Eliminar salto de línea

    // Solicitar el nuevo estado
    strcpy(msg, "Nuevo estado:");
    printf("%s ", msg);

    char estado[32];
    fgets(estado, sizeof(estado), stdin);
    estado[strcspn(estado, "\n")] = '\0';  // Eliminar salto de línea de fgets

    // Validar el estado
    if (strcmp(estado, "ACTIVO") != 0 &&
        strcmp(estado, "OCUPADO") != 0 &&
        strcmp(estado, "INACTIVO") != 0) {
        printf("Status inválido. Manteniendo el status actual.\n");
        cJSON_Delete(json);
        return -1;
        }

    // Añadir los campos al objeto JSON
    cJSON_AddStringToObject(json, "type", "change_status");
    cJSON_AddStringToObject(json, "sender", current_username);
    cJSON_AddStringToObject(json, "content", estado);

    // Enviar el JSON
    enviar_json(json, wsi);

    // Actualizar el status local
    strcpy(current_status, estado);
    printf("Status cambiado a: %s\n", current_status);


    return 0;
}


//registro
int register_user(struct lws *wsi, const char *curren_username) {

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


// Enviar mensaje como JSON
int send_group_message(struct lws *wsi) {
    if (strlen(current_username) == 0) {
        printf("Debes registrarte primero.\n");
        return -1;
    }

    char msg[MAX_MESSAGE];
    int exit_chat = 0;

    while (!exit_chat) {
        // Solicitar el mensaje del usuario
        fgets(msg, sizeof(msg), stdin);
        msg[strcspn(msg, "\n")] = '\0';  // Eliminar salto de línea

        if (strcmp(msg, "salir") == 0) {
            printf("Saliendo del chat...\n");
            exit_chat = 1;  // Salir del chat
            continue;
        }

        // Obtener el timestamp actual
        char timestamp[MAX_MESSAGE];
        get_current_timestamp(timestamp, sizeof(timestamp));

        // Crear el objeto JSON
        cJSON *json = cJSON_CreateObject();
        if (json == NULL) {
            printf("Error al crear el objeto JSON.\n");
            return -1;
        }

        // Añadir los campos al objeto JSON
        cJSON_AddStringToObject(json, "type", "broadcast");
        cJSON_AddStringToObject(json, "sender", current_username);
        cJSON_AddStringToObject(json, "content", msg);
        cJSON_AddStringToObject(json, "timestamp", timestamp);

        // Convertir el objeto JSON a una cadena
        char *json_string = cJSON_PrintUnformatted(json);
        if (json_string == NULL) {
            printf("Error al convertir JSON a cadena.\n");
            cJSON_Delete(json);
            return -1;
        }

        // Enviar el mensaje JSON por WebSocket
        size_t json_length = strlen(json_string);
        unsigned char buf[LWS_PRE + json_length];  // Tamaño adecuado para el buffer
        unsigned char *p = &buf[LWS_PRE];
        size_t n = snprintf((char *)p, json_length + 1, "%s", json_string);  // Incluye '\0' en el tamaño

        if (n < 0 || n > json_length) {
            printf("Error al formar el mensaje JSON.\n");
            free(json_string);
            cJSON_Delete(json);
            return -1;
        }

        // Enviar los datos a través del WebSocket
        lws_write(wsi, p, n, LWS_WRITE_TEXT);

        // Liberar memoria
        free(json_string);
        cJSON_Delete(json);
    }

    return 0;
}


// Función para enviar un mensaje privado
int send_private_message(struct lws *wsi) {
    if (strlen(current_username) == 0) {
        printf("Debes registrarte primero.\n");
        return -1;
    }

    char target_username[MAX_USERNAME];
    char message[MAX_MESSAGE];

    // Solicitar el nombre de usuario del destinatario
    printf("Ingrese nombre de usuario destinatario: ");
    scanf("%49s", target_username);

    // Solicitar el mensaje
    printf("Escriba su mensaje (máximo %d caracteres): ", MAX_MESSAGE - 1);
    scanf(" %[^\n]", message);

    // Crear objeto JSON
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("Error al crear el objeto JSON.\n");
        return -1;
    }

    // Obtener el timestamp actual
    char timestamp[MAX_MESSAGE];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // Añadir los campos al objeto JSON
    cJSON_AddStringToObject(json, "type", "private");
    cJSON_AddStringToObject(json, "sender", current_username);
    cJSON_AddStringToObject(json, "target", target_username);
    cJSON_AddStringToObject(json, "content", message);
    cJSON_AddStringToObject(json, "timestamp", timestamp);

    // Convertir el objeto JSON a una cadena
    char *json_string = cJSON_PrintUnformatted(json);
    if (json_string == NULL) {
        printf("Error al convertir JSON a cadena.\n");
        cJSON_Delete(json);
        return -1;
    }

    // Mostrar el JSON generado para depuración
    printf("JSON generado: %s\n", json_string);

    // Enviar el mensaje JSON por WebSocket
    unsigned char buf[LWS_PRE + strlen(json_string)+1];
    unsigned char *p = &buf[LWS_PRE];
    size_t n = snprintf((char *)p, sizeof(buf) - LWS_PRE, "%s", json_string);

    // Asegurarse de que el tamaño del mensaje no sea mayor que el buffer
    if (n > sizeof(buf) - LWS_PRE) {
        printf("Error: El mensaje es demasiado grande para el buffer.\n");
        free(json_string);
        cJSON_Delete(json);
        return -1;
    }

    // Enviar el mensaje por WebSocket
    lws_write(wsi, p, n, LWS_WRITE_TEXT);

    // Liberar memoria
    free(json_string);
    cJSON_Delete(json);

    return 0;
}

pthread_mutex_t list_users_mutex = PTHREAD_MUTEX_INITIALIZER;

int list_users(struct lws *wsi) {
    // Bloquear el mutex para evitar condiciones de carrera
    pthread_mutex_lock(&list_users_mutex);

    // Crear objeto JSON
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("Error al crear el objeto JSON.\n");
        pthread_mutex_unlock(&list_users_mutex);  // Desbloquear antes de salir
        return -1;
    }

    // Verificar que current_username esté inicializado
    if (current_username == NULL) {
        printf("current_username no está inicializado.\n");
        cJSON_Delete(json);
        pthread_mutex_unlock(&list_users_mutex);  // Desbloquear antes de salir
        return -1;
    }

    // Añadir los campos al objeto JSON
    cJSON_AddStringToObject(json, "type", "list_users");
    cJSON_AddStringToObject(json, "sender", "Francis");  // Usando el nombre de usuario actual



    // Depuración: Verificar el contenido de la cadena JSON antes de enviarla
    printf("JSON generado: %s\n", json);

    // Enviar el mensaje (suponiendo que tengas una función para eso)
    int r = enviar_json(json, wsi);
    if (r < 0) {
        printf("Error al enviar el mensaje.\n");
    }


    // Desbloquear el mutex después de completar la operación
    pthread_mutex_unlock(&list_users_mutex);

    return 0;
}




// Mostrar información de un usuario específico
int user_info(struct lws *wsi) {
    char target_username[MAX_USERNAME];
    printf("Ingrese nombre de usuario para ver información: ");
    scanf("%49s", target_username);

    // Crear objeto JSON
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        printf("Error al crear el objeto JSON.\n");
        return -1;
    }

    // Añadir los campos al objeto JSON
    cJSON_AddStringToObject(json, "type", "user_info");
    cJSON_AddStringToObject(json, "sender", current_username);  // Usando el nombre de usuario actual
    cJSON_AddStringToObject(json, "target", target_username);    // Nombre del usuario objetivo

    // Enviar el JSON
    enviar_json(json, wsi);

    // Liberar memoria

    return 0;
}


//ayuda
void show_help() {
    printf("\n===== AYUDA DEL CHAT 3000 =====\n");
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
            printf("✅ Conectado al servidor\n");


            cJSON *registro = cJSON_CreateObject();
            cJSON_AddStringToObject(registro, "type", "register");
            cJSON_AddStringToObject(registro, "sender", current_username);
            cJSON_AddNullToObject(registro, "content");
            enviar_json(registro, wsi);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Convertimos los datos recibidos en formato JSON
            cJSON *json = cJSON_Parse((char *)in);

            if (json) {
                const cJSON *type = cJSON_GetObjectItem(json, "type");
                if (type && type->valuestring) {
                    // Verificamos si es un mensaje de tipo "broadcast"
                    if (strcmp(type->valuestring, "broadcast") == 0) {
                        const cJSON *sender = cJSON_GetObjectItem(json, "sender");
                        const cJSON *content = cJSON_GetObjectItem(json, "content");
                        const cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                        // Si existen los elementos necesarios, imprimimos el mensaje
                        if (sender && content && timestamp) {
                            printf("[%s] %s: %s\n", timestamp->valuestring, sender->valuestring, content->valuestring);
                        }
                    }
                    //  Si es una lista de usuarios
                    if (strcmp(type->valuestring, "list_users_response") == 0) {
                        const cJSON *users = cJSON_GetObjectItem(json, "content");
                        if (cJSON_IsArray(users)) {
                            printf(" Lista de usuarios conectados:\n");

                            int num_users = cJSON_GetArraySize(users);
                            for (int i = 0; i < num_users; i++) {
                                cJSON *user = cJSON_GetArrayItem(users, i);
                                if (cJSON_IsString(user)) {
                                    printf("🔹 %s\n", user->valuestring);
                                }
                            }
                            printf(" Total de usuarios: %d\n", num_users);
                        } else {
                            printf(" No se encontró la lista de usuarios.\n");
                        }
                    }
                    // Si es un mensaje privado
                    else if (strcmp(type->valuestring, "private") == 0) {
                        const cJSON *sender = cJSON_GetObjectItem(json, "sender");
                        const cJSON *target = cJSON_GetObjectItem(json, "target");
                        const cJSON *content = cJSON_GetObjectItem(json, "content");
                        const cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                        if (sender && content && timestamp) {
                            // Imprimimos el mensaje privado
                            printf("[Privado de: %s] [%s] %s: %s\n", sender->valuestring,timestamp->valuestring,  target->valuestring,content->valuestring);
                        }
                    }
                    else if (strcmp(type->valuestring, "user_info_response") == 0) {
                        const cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");
                        const cJSON *content = cJSON_GetObjectItem(json, "content");
                        const cJSON *target = cJSON_GetObjectItem(json, "target");

                        // Verificar si el campo 'timestamp' existe y es una cadena
                        if (timestamp && cJSON_IsString(timestamp)) {
                            printf("Información del usuario recibida [%s]:\n", timestamp->valuestring);
                        } else {
                            printf("Información del usuario recibida:\n");
                        }

                        // Verificar si 'target' está presente y es una cadena
                        if (target && cJSON_IsString(target)) {
                            printf("Usuario objetivo: %s\n", target->valuestring);
                        } else {
                            printf("Error: 'target' no está presente o no es una cadena válida.\n");
                        }

                        // Verificar si 'content' es un objeto JSON válido
                        if (content && cJSON_IsObject(content)) {
                            const cJSON *ip = cJSON_GetObjectItem(content, "ip");
                            const cJSON *status = cJSON_GetObjectItem(content, "status");

                            // Verificar si 'ip' y 'status' están presentes y son cadenas
                            if (ip && cJSON_IsString(ip)) {
                                printf("IP: %s\n", ip->valuestring);
                            } else {
                                printf("Error: 'ip' no está presente o no es una cadena válida.\n");
                            }

                            if (status && cJSON_IsString(status)) {
                                printf("Estado: %s\n", status->valuestring);
                            } else {
                                printf("Error: 'status' no está presente o no es una cadena válida.\n");
                            }
                        } else {
                            printf("Error: 'content' no es un objeto JSON válido.\n");
                        }
                    }

                }
                // Si es una respuesta de información del usuario



                // Liberamos el objeto JSON después de usarlo
                cJSON_Delete(json);
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Conexión cerrada o error en la conexión.\n");
        web_socket = NULL;
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

// Protocolo WebSocket
static struct lws_protocols protocols[] = {
    { "chat-protocol", callback_client, 0, MAX_MESSAGE },
    { NULL, NULL, 0, 0 }
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
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <nombredeusuario> <IPdelservidor> <puertodelservidor>\n", argv[0]);
        return -1;
    }

    // Asignar el nombre de usuario desde el parámetro
    strncpy(current_username, argv[1], MAX_USERNAME - 1);
    current_username[MAX_USERNAME - 1] = '\0';  // Asegurarse de que el nombre esté correctamente terminado

    const char *server_ip = argv[2];
    int server_port = atoi(argv[3]);

    // Configuración de contexto de websocket
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

    // Configuración de la conexión de cliente WebSocket
    ccinfo.context = context;
    ccinfo.address = server_ip;
    ccinfo.port = server_port;
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

    // Crear un hilo para manejar la conexión WebSocket
    pthread_t thread;
    struct thread_args args = { context };
    if (pthread_create(&thread, NULL, websocket_thread, &args) != 0) {
        fprintf(stderr, "Error al crear el hilo del WebSocket.\n");
        lws_context_destroy(context);
        return -1;
    }

    // Mostrar el menú después de la conexión
    while (!should_exit) {
        menu(wsi);
    }

    // Limpiar recursos antes de salir
    pthread_join(thread, NULL);
    lws_context_destroy(context);

    return 0;
}
