//#include <tchar.h>
//#include <fcntl.h>
#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <windows.h>
//############################################################//
#pragma hdrstop
#pragma argsused
#pragma comment(lib, "Ws2_32.lib")
//############################################################//
#define PORT_1 69
#define PORT_2 4259
#define BUFLEN 512
//############################################################//
#ifdef _WIN32
#include <tchar.h>
#else
  typedef char _TCHAR;
  #define _tmain main
#endif
//############################################################//
// structure for statistic
struct stat {
	int blocks;
	int errors;
} stat;
// error code
const char *error_str[] = {
	"OK",
	"err1",                     // WSAS сломался
	"err2",                     // Привязка сокета
	"err3",                     // Бинд сервера не сработал, проверьте ip адрес и порт
	"err4",                     // Превышен интервал ожидания запроса
	"err5",                     // Работа с серверным таймером сломалсь, возможно вы ввели слишком маленькое значение timeout
	"err6",                     // Ошибка принятия запроса, перепроверьте введённые данные
	"err7",                     // Получено сообщение об ошибке
	"err8",                     // Передача файла не удалась
	"err9",                     // Получено подтвержение на неверный блок!
	"err10",                    // Открытие файла произошло не удачно
	"err11"                     // На будущее
};

// tftp transfer mode
enum mode {
	 NETASCII=1,
	 OCTET
};
// tftp opcode mnemonic
enum opcode {
	 RRQ=1,
	 WRQ,
	 DATA,
	 ACK,
	 ERR0R
};
// tftp message structure
typedef union {
	 uint16_t opcode;
	 struct {
		  uint16_t opcode; // RRQ
		  uint8_t filename_and_mode[BUFLEN + 2];
     } request;
	 struct {
		  uint16_t opcode; // DATA
		  uint16_t block_number;
		  uint8_t data[BUFLEN];
     } data;
	 struct {
		  uint16_t opcode; // ACK
		  uint16_t block_number;
     } ack;
	 struct {
		  uint16_t opcode; // ERR0R
          uint16_t error_code;
		  uint8_t error_string[BUFLEN];
     } error;
} tftp_message;
//############################################################//
		// ---  send a error message to client  --- //
ssize_t tftp_send_error(int s, int error_code, char *error_string, struct sockaddr_in *sock, int slen) {
     tftp_message m;
     ssize_t c;

	 if(strlen(error_string) >= BUFLEN) {
		  fprintf(stderr, "server: tftp_send_error(): error string too long\n");
          return -1;
     }

     m.opcode = htons(ERR0R);
     m.error.error_code = error_code;
	 strcpy((char*)m.error.error_string, error_string);

	 if ((c = sendto(s, (char*)&m, 4 + strlen(error_string) + 1, 0, (struct sockaddr *) sock, slen)) < 0)
		  perror("server: sendto()");

	 return c;
}
		// check request for file name and mode; after check file availability
FILE *file_open(tftp_message *m, int cnt, struct sockaddr_in client, int sock_output, char *dir) {
	char *mode, *tmp;
	char filename[256];
	FILE *fd;
	//----------------------------------------------------------------------
	// Parse client request

	//filename = (char*)m->request.filename_and_mode;
	tmp 	 = (char*)m->request.filename_and_mode;

	snprintf(filename, sizeof(filename), "%s%s" , (char*) dir, (char*) tmp);

	//----------------------------------------------------------------------
	// Try to find a file name on request
	int	i = cnt - 2;
	while (i--) {
	   if (*tmp == 0)
		  break;
	   tmp++;
	}

	if (*tmp)
		tftp_send_error(sock_output, 3, "broken file", &client, sizeof(&client));				// no end of line

	if (i == 0)
		tftp_send_error(sock_output, 4, "not found mod transfer!", &client, sizeof(&client));	// no mode

	mode = tmp + 1;
	i--;

	//----------------------------------------------------------------------
	// Try to find a mode transfer on request
	while (i--) {
	   if (*tmp == 0)
		  break;
	   tmp++;
	}

	if (*tmp)
		tftp_send_error(sock_output, 3, "broken file", &client, sizeof(&client));				// no end of line

	if (strcmp(mode, "octet") != 0)
		tftp_send_error(sock_output, 4, "error mode transfer!", &client, sizeof(&client));		// check mode for octet

	if ((fd = fopen(filename, "rb")) == NULL) {
		printf("cant's open file %s\n\n ", filename);
		tftp_send_error(sock_output, 5, "can't open a file!", &client, sizeof(&client));
		return NULL;
	}
	else
		return fd;

}
		// time of taking request (timeout)    --- //
int recvfromTimeOutUDP(SOCKET socket, long sec){
	//--------------------------------------------------------------------------
	// Setup timeval variable
	struct fd_set fds;
	struct timeval timeout;
	timeout.tv_sec = sec;
	//--------------------------------------------------------------------------
	// Setup fd_set structure
	FD_ZERO(&fds);
	FD_SET(socket, &fds);
	//--------------------------------------------------------------------------
	// Returns value:
	// -1: error occurred
	// 0: timed out
	// > 0: data ready to be read
	return select(0, &fds, 0, 0, &timeout);
}

const char *tftp_server(char *dir, int time, char *server_ip, char *client_ip, struct stat *stat) {

	FILE *fd;                               // file descriptor

	int code = 0;
	int timing	= 0;		 				// timer, buffer, close
	int sock 	= 0;						// socket for get a request and socket for send a request
	int cnt 	= 0;						// temp to save a answert of requests

	ssize_t dlen = 0;
	ssize_t errors_number = 0;              // temp to check the number of errors
	ssize_t c_len = 0;

	uint8_t data[BUFLEN];                   // Buffer for data
	uint16_t block_number = 0;              // Block number

	WSADATA wsaData;

	//--------------------------------------------------------------------------
	// Structures
	struct sockaddr_in server, si_other;	// Server
	struct sockaddr_in client;              // Real client ip address

	//--------------------------------------------------------------------------
	// Really ip server address
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(server_ip);
	server.sin_port = htons(PORT_1);

	//--------------------------------------------------------------------------
	// Structures for message
	tftp_message get_m = { 0 };
	tftp_message send_m = { 0 };

	//--------------------------------------------------------------------------
	// Initialize Winsock

	if ((cnt = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
		printf("WSAStartup failed: %d\n", cnt);
		code = 1;
        goto close_all;     //all close
	}

	//--------------------------------------------------------------------------
	// Create a SOCKET for listening for incoming connection requests

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		printf("socket function failed with error: %u\n", WSAGetLastError());
		code = 2;
		goto close_all;     //all close
	}

	//--------------------------------------------------------------------------
	// Bind the socket.

	if (bind(sock ,(struct sockaddr *)&server , sizeof(server)) == SOCKET_ERROR){
		printf("Bind failed with error code : %d" , WSAGetLastError());
		code = 3;
		goto close_all;     //all close
	}

	printf("\rtftp server: listening on %d\n", ntohs(server.sin_port));

	c_len = sizeof(client);

	//--------------------------------------------------------------------------
	// Get first true request

	while (1) {
		timing = recvfromTimeOutUDP(sock, time);

		switch (timing){

			//----------------------------------------------------------------------
			// Timeout is coming
			case 0:
			printf("timeout client request. server is closing.");
				code = 4;
				goto close_all;     //all close
			break;
			//----------------------------------------------------------------------
			// error
			case -1:
				printf("Server: Some error encountered with code number: %d\n\n\nTimeout function get error.\n Restart a server please!", WSAGetLastError());
				code = 5;
				goto close_all;     //all close
			break;
			//----------------------------------------------------------------------
			// ALL OKEY LEST sart
			default:
			{

				//----------------------------------------------------------------------
				// Try to receive some data, this is a blocking call

				if ((cnt = recvfrom(sock, (char*) &get_m, BUFLEN, 0, (struct sockaddr*) &client, &c_len)) == SOCKET_ERROR) {
					printf("recvfrom() failed with error code : %d" , WSAGetLastError());
					code = 6;
					goto close_all;     //all close
				}

				printf("Received packet from %s:%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

				//----------------------------------------------------------------------
				// Comparison of theoretical ip with real ip , if it's not a client ip, when wait new request

				if (client.sin_addr.s_addr != inet_addr(client_ip)){
					printf("Was try connecting from %s ip, but teoretical ip is %s\n", (char*)&client.sin_addr.s_addr, (char*)&client_ip);
					continue;
				}

				//----------------------------------------------------------------------
				// Check for Error code and if it's error, then close app

				if (ntohs(get_m.opcode) == ERROR)  {
					printf("%s.%u: error message received: %u %s\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port), ntohs(get_m.error.error_code), get_m.error.error_string);
					tftp_send_error(sock, 0, "get a request with ERROR code", &client, c_len);
					code = 7;
					goto close_all;     //all close
				}

				//----------------------------------------------------------------------
				// Check for first request

				if (ntohs(get_m.opcode) == RRQ && block_number == 0) {

					//----------------------------------------------------------------------
					// check the size of getting packet

					if (cnt < 9) {
						tftp_send_error(sock, 0, "invalid size", &client, c_len);
						continue;
					}

					//----------------------------------------------------------------------
					// check file and if file is valiable send first packet

					if ((fd = file_open(&get_m, cnt, client, sock, dir)) == NULL){
						printf("File opening failed!");
						code = 10;
						continue;
					}

					//---------------------------------------------------------------------
					// rebind a socket to new port

					closesocket(sock);

					server.sin_port = htons(PORT_2);

					if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
						printf("socket function failed with error: %u\n", WSAGetLastError());
						code = 2;
						goto close_all;     //all close
					}

					//----------------------------------------------------------------------
					// Bind the socket.

					if (bind(sock ,(struct sockaddr *)&server , sizeof(server)) == SOCKET_ERROR){
						printf("Bind failed with error code : %d" , WSAGetLastError());
						code = 3;
						goto close_all;     //all close
					}
				}

				//--------------------------------------------------------------
				// check for incorrect ACK

				if (ntohs(get_m.opcode) == ACK) {

					// Check for incorrect block number
					if (ntohs(get_m.ack.block_number) == block_number - 1) {

						if ((cnt = sendto(sock,(char*) &send_m, 4 + dlen, 0, (struct sockaddr *) &client, c_len)) < 0) {
							printf("%s.%u: transfer killed\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
							code = 8;
							goto close_all;
						}

						errors_number++;
                        continue;
					}

					if (ntohs(get_m.ack.block_number) != block_number) { // the ack number is too high or low
						printf("%s.%u: invalid ack number received\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
						tftp_send_error(sock, 5, "invalid ack number", &client, c_len);
						code = 9;
						goto close_all;         // all close;
					}

					if (dlen < BUFLEN && block_number > 0) {
						printf("sending sucsessful!");
						code = 0;
						goto close_all;
					}

				}

				//--------------------------------------------------------------
				//Read next block of file and sending
				dlen = fread(send_m.data.data, 1, sizeof(data), fd);

				if ((dlen != sizeof(data)) && (ferror(fd))) {
					printf("Error of freading file");
					tftp_send_error(sock, 6, "Error of opening file", &client, c_len);
					code = 10;
					goto close_all;
				}

				//отправка пакета
				block_number++;

				send_m.opcode = htons(DATA);
				send_m.data.block_number = htons(block_number);

				if ((cnt = sendto(sock,(char*) &send_m, 4 + dlen, 0, (struct sockaddr *) &client, c_len)) < 0) {
					printf("%s.%u: transfer killed\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));
					code = 8;
					goto close_all;
				}

			}
		}

	}

close_all:

	Sleep(10);

	if (sock) {
		closesocket(sock);
		sock = 0;
	}
	WSACleanup();

	if (code == 0) {
		stat->blocks = block_number;
		stat->errors = errors_number;
		return 0;
	}

	else {
		stat->blocks = 0;
		stat->errors = 0;
		return error_str[code];
	}
}

int main(void){
	const char *res;
	char *dir = "C:\\tftp\\";

	char *client_ip = "192.168.1.1";
	char *server_ip = "192.168.1.2";

	int time = 50;                      // 50 sec timeout

    res = tftp_server(dir, time, server_ip, client_ip, &stat);
	printf("%s\n", res);
}
