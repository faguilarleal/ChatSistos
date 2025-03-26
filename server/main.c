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

#define MAX_CLIENTS 10
#define MAX_USERS 10

typedef struct{
	char username[50];
	char status[50];
    char pending_msg[1024]; // mensaje pendiente por enviar
    int has_pending_msg; 

	struct lws *wsi;
} User;

User Users[MAX_USERS];

//declarar funciones para que no de error
void Register();
int addUser(const char *username, const char *status, struct lws *wsi);


void menu(){
	int opcion;
	printf("Escoga una opcion: ");

	printf("\n1. Registro de usuarios");
	printf("\n2. Liberacion de usuarios");
	printf("\n3. Listado de usuarios conectados");
	printf("\n4. Obtencion de informacion de usuario");
	printf("\n5. Cambio de estatus");
	printf("\n6. Boracasting y mensajes directos");
	printf("\n7. salir");

	printf("\nEscoga una opcion: ");
	scanf("%d", &opcion); 

	switch (opcion)
	{
	case 1:
		/* registrar usuario */
        Register();
		break;
	case 2:
		/* code */
		break;
	case 3:
		/* code */
		break;
	case 4:
		/* code */
		break;
	case 5:
		/* code */
		break;
	case 6:
		/* code */
		break;
	case 7:
		printf("👋 chao\n");
		exit(0);
		break;
	default:
		printf("opcion no valida, vuelta a intentarlo ");
		break;
	}

}


void Register(){

    User usuario;

    char username[50];
    printf("\nIngrese el nombre del usuario a registrar: ");
    scanf("%s", username); 
    char status[50];
    printf("\nIngrese el status del usuario (activo, ocupado, inactivo): ");
    scanf("%s", status); 

    if (strcmp(status, "activo") != 0 && strcmp(status, "ocupado") != 0 && strcmp(status, "inactivo") != 0){
        printf("\ningrese correctamente en minusculas el estado ");
        return; 
    }

    int response = addUser(username, status, NULL); //null porque como aqui en esta parte no se usa websocket

    if (response == 0){
        printf("\nBienvenid@  %s\n\n", username);
    }else{
        printf("\no se pudo registral el usuario, vuelva a intentarlo ");
    }

    


}

int addUser(const char *username, const char *status, struct lws *wsi){

    for (int i = 0; i < MAX_USERS; i++) {
        // validar si el nombre ya existe
        if (strcmp(Users[i].username, username) == 0) {
            printf("\nError: El nombre de usuario ya exite");
            return -1;
        }
    }

    for (int i = 0; i < MAX_USERS; i++) {
        //buscar una posocion vacia en el array
        if (strlen(Users[i].username) == 0) { 
            strcpy(Users[i].username, username);
            strcpy(Users[i].status, status);
            Users[i].wsi = wsi; //Asignar el puntero WebSocket 

            printf("Usuario agregado en la posicion %d: %s (%s)\n", i, username, status);;
            return 0;
        }
    }

    // Si no hay espacio disponible
    printf("\nError: No hay espacio para agregar mas usuarios");
    return -1; 
}

void handleRegister(struct lws *wsi, cJSON *json){
    //jalar los datos
    const cJSON *username = cJSON_GetObjectItem(json, "username");
    const cJSON *status = cJSON_GetObjectItem(json, "status");

    if (!username || !status || !cJSON_IsString(username) || !cJSON_IsString(status)) {
        lwsl_err("Registro mal formado\n");
        return;
    }

    if (addUser(username->valuestring, status->valuestring, wsi) == 0) {
        for (int i = 0; i < MAX_USERS; i++) {
            if (Users[i].wsi == wsi) {
                strcpy(Users[i].pending_msg, "{\"type\": \"register_ack\", \"status\": \"ok\"}");
                Users[i].has_pending_msg = 1;
                lws_callback_on_writable(wsi);
                break;
            }
        }
    } else {
        for (int i = 0; i < MAX_USERS; i++) {
            if (Users[i].wsi == wsi) {
                strcpy(Users[i].pending_msg,
                    "{\"type\": \"register_ack\", \"status\": \"error\", \"reason\": \"username_taken_or_full\"}");
                Users[i].has_pending_msg = 1;
                lws_callback_on_writable(wsi);
                break;
            }
        }
    }
}


static struct lws *clients[MAX_CLIENTS] = {NULL};

static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    switch (reason) {
        //Cuando un cliente se conecta, agregar a la  lista de clientes
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_user("Client connected\n");
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] == NULL) {
                clients[i] = wsi;
                break;
            }
        }
        break;


        //cuando recibe un mensaje, se reenvia a los demas clientes
        case LWS_CALLBACK_RECEIVE:
            lwsl_user("Received: %s\n", (char *)in);

            cJSON *json = cJSON_Parse((char *)in);

            if (!json) {
                lwsl_err("Error al parsear JSON\n");
                break;
            }

            const cJSON *type = cJSON_GetObjectItem(json, "type");
            
            //registrar usuarios
            if (strcmp(type->valuestring, "register") == 0) {

                handleRegister(wsi, json);
            

            }else{

                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i] && clients[i] != wsi) {
                        lws_write(clients[i], (unsigned char *)in, len, LWS_WRITE_TEXT);
                    }
                }
            }

            cJSON_Delete(json);
            break;

        case LWS_CALLBACK_SERVER_WRITEABLE:

            for (int i = 0; i < MAX_USERS; i++) {
                if (Users[i].wsi == wsi && Users[i].has_pending_msg) {
                    unsigned char buf[LWS_PRE + 1024];
                    size_t msg_len = strlen(Users[i].pending_msg);
                    memcpy(&buf[LWS_PRE], Users[i].pending_msg, msg_len);
                    lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
                    Users[i].has_pending_msg = 0;
                    break;
                }
            }

        break;


        //cuendo un cliente se desconecta  se elimina de la lista de clientes
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

//Definir el protocolo del chat y asociar con el callback
static const struct lws_protocols protocols[] = {
    {"chat-protocol", callback_chat, 0, 4096},
    {NULL, NULL, 0, 0}
};

int main() {
    //declarar la variable info, que es una estructura para configurar el contexto de websocket
    struct lws_context_creation_info info;
    //inicializar los campos de la estructuta a 0
    memset(&info, 0, sizeof(info));
    //configuracion de puertos y protocolos
    info.port = 9000;
    info.protocols = protocols;
    //creacion de contexto del servidor websocket 
    //el contexto es el entorno en donde el servidor de websocket opera
    struct lws_context *context = lws_create_context(&info);

    //verificacion de la creacion del contexto
    if (!context) {
        lwsl_err("Failed to create WebSocket context\n");
        return -1;
    }

    lwsl_user("WebSocket server started on ws://localhost:9000\n");

    

    while (1) {
        //servivio del contexto
        //el 100 es el tiempo de espera para la funcion para ejecutar las conextiones y mensajes de los clientes
        lws_service(context, 50);

        menu();
    }

    //destruir el contexto
    lws_context_destroy(context);
    return 0;
}
