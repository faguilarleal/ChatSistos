/*
ejecutar por medio de:
<nombredelcliente><nombredeusuario><IPdelservidor>
<puertodelservidor>

<nombredelcliente> es el nombre del programa
<IPdelservidor> y<puertodelservidor> es a donde tiene que llegar la solicitud de conexion del cliente 

el cliente tenfra la interfaz para que le de opcion de:
- chatear con otros usuarios
- enviar y recibir mensajes directos, privados, aparte del chat general
- cambiar de status
- listar los usuarios conectados al sistema de chat
- desplegar  informacion de un usuario en particulas
- ayuda 
- salir

para chatear tiene que tener el formato <usuario><mensaje>
el status permite al cliente elegir entre activo, ocupado e inactivo
la eleccion envia una solicitud de actualizacion de informacion al servidor y el listado de usuarios se desplegara.

componentes del cliente:
- chateo general con usuarios
- chateo privado con multithreading
- cambio de status
- listado de usuarios e informacion de un usuario

*/
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//variable para almacenar la conexion del cliente websocket
static struct lws *web_socket = NULL;

#define EXAMPLE_TX_BUFFER_BYTES 10


void menu(){
	int opcion;
	printf("Escoga una opcion: ");

	printf("\n1. chatear con otros usuarios");
	printf("\n2. enviar y recibir mensajes directos, privados, aparte del chat general");
	printf("\n3. cambiar de status");
	printf("\n4. listar los usuarios conectados al sistema de chat");
	printf("\n5. desplegar  informacion de un usuario en particulas");
	printf("\n6. ayuda");
	printf("\n7. salir");

	printf("\nEscoga una opcion: ");
	scanf("%d", &opcion); 

	switch (opcion)
	{
	case 1:
		/* code */
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




static int callback_example( struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len )
{
	switch( reason )
	{
		// cuando se establece la conexion se marca el cliente como listo para escribit
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
			lws_callback_on_writable( wsi );
			break;

		// manejo de mensajes entrantes
		case LWS_CALLBACK_CLIENT_RECEIVE:
			/* Handle incomming messages here. */
			break;

		//cuando el cliente esta listo para escribir, va a escribir un numero random al servidor
		case LWS_CALLBACK_CLIENT_WRITEABLE:
		{
			unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + EXAMPLE_TX_BUFFER_BYTES + LWS_SEND_BUFFER_POST_PADDING];
			unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
			size_t n = sprintf( (char *)p, "%u", rand() );
			lws_write( wsi, p, n, LWS_WRITE_TEXT );
			break;
		}

		//cuando la conexion se cierra o hay un error se establece el websocket a null
		case LWS_CALLBACK_CLIENT_CLOSED:
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
			web_socket = NULL;
			break;

		default:
			break;
	}

	return 0;
}


//definicion de protocolo
enum protocols
{
	PROTOCOL_EXAMPLE = 0,
	PROTOCOL_COUNT
};

static struct lws_protocols protocols[] =
{
    {
        .name                  = "example-protocol", // nombre del protocolo
        .callback              = callback_example,   // define la funcion de callbak con el procolo
        .per_session_data_size = 0,                  //  tamanio de los datos por sesion 
        .rx_buffer_size        = 0,                  //  tamanio del buffer de recepcion, el 0 es que no hay restriccion de tam
        .id                    = 0,                  // id de la version del protocolo, puede ser opcionsl
        .user                  = NULL,               // puntero a los datos de usuario que pueden ser accesibles
        .tx_packet_size        = 0                   // restriccion del tamanio del buffer de transmicion
    },
    LWS_PROTOCOL_LIST_TERM // terminador,  final del protocolo
};

int main( int argc, char *argv[] )
{
	//declarar la variable info, que es una estructura para configurar el contexto de websocket
	struct lws_context_creation_info info;
	//inicializar los campos de la estructuta a 0
	memset( &info, 0, sizeof(info) );

	//configuracion de puertos y protocolos
	info.port = CONTEXT_PORT_NO_LISTEN;  //no se corre ningun server
	info.protocols = protocols;
	
	info.gid = -1;
	info.uid = -1;

	//creacion de contexto del servidor websocket 
    //el contexto es el entorno en donde el servidor de websocket opera
	struct lws_context *context = lws_create_context( &info );

	menu();

	//servicio
	time_t old = 0;
	while( 1 )
	{
		struct timeval tv;
		gettimeofday( &tv, NULL );

		//si no hay conexion al web socket y paso un segundo del tv.tv_Sec, intentar conectar al servidor en local host
		if( !web_socket && tv.tv_sec != old )
		{
			//configurar la info,como en el de server
			struct lws_client_connect_info ccinfo;
			memset(&ccinfo, 0, sizeof(ccinfo));

			ccinfo.context = context;
			ccinfo.address = "localhost";
			ccinfo.port = 8000;
			ccinfo.path = "/";
			ccinfo.host = lws_canonical_hostname( context );
			ccinfo.origin = "origin";
			ccinfo.protocol = protocols[PROTOCOL_EXAMPLE].name;

			web_socket = lws_client_connect_via_info(&ccinfo);
		}

		//envio de mensajes
		//cada segundo se marca el cliente listo para escribir y acualiza con old para el timpo actual
		if( tv.tv_sec != old )
		{
			//enviar un numero random al server cada segundo
			lws_callback_on_writable( web_socket );
			old = tv.tv_sec;
		}

		//servivio del contexto
		//el 250 es el tiempo de espera para la funcion para ejecutar las conextiones y mensajes de los clientes
		lws_service( context, 250 ); 
	}

	//destruir el contexto
	lws_context_destroy( context );

	return 0;
}