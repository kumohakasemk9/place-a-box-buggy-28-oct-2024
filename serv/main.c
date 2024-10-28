#define NUM_MAX_CLIENTS 20
#define NUM_MESSAGE_BUF_LEN 8192
#define NUM_NET_BUFSIZ 1024

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>

typedef struct __attribute__((packed)) {
	uint8_t cid;
	uint16_t len;
} stackedevent_hdr_t;

typedef struct {
	int fd;
	int bufcur;
	in_addr_t addr;
	in_port_t port;
	char name[32];
} ClientInfo_t;

void IntHwnd(int);
void IOHwnd(int);
int InstallHandler(int, void (*)() );
int ProgramExit = 0;
int MakeAsync(int);
int IOPending(int);
void NewClientHandler();
void CloseClient(int);
void ClientRecvHandler(int);
void AppendEvent(int, uint8_t*, int);

int ServerSocket;
ClientInfo_t Clients[NUM_MAX_CLIENTS];
int MessageBuffer[NUM_MESSAGE_BUF_LEN];
int MessageBufCur = 0;
void ClientRecvHandler(int);

int main(int argc, char* argv[]) {
	for(int i = 0; i < NUM_MAX_CLIENTS; i++) {
		Clients[i].fd = -1;
	}
	//Install Handler
	if(InstallHandler(SIGINT, IntHwnd) == -1 || InstallHandler(SIGIO, IOHwnd) == -1) {
		perror("Signal installation failed");
		return 1;
	}
	//Make socket
	ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(ServerSocket == -1) {
		perror("socket() failed");
		return 1;
	}
	int opt = 1;
	setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt) );
	//Bind
	int portnum = 25566;
	if(argc > 1) {
		int t = atoi(argv[1]);
		if(1 <= t && t <= 65535) {
			portnum = t;
		}
	}
	struct sockaddr_in badr = {
		.sin_family = AF_INET,
		.sin_port = htons(portnum),
		.sin_addr.s_addr = INADDR_ANY
	};
	if(bind(ServerSocket, (struct sockaddr*)&badr, sizeof(badr) ) == -1) {
		perror("bind() failed");
		close(ServerSocket);
		return 1;
	}
	//listen
	if(listen(ServerSocket, 3) == -1) {
		perror("listen failed");
		close(ServerSocket);
		return 1;
	}
	//Make it async
	if(MakeAsync(ServerSocket) == -1) {
		printf("set IO mode failed.\n");
		close(ServerSocket);
		return 1;
	}
	printf("Server started on *:%d\n", portnum);
	while(ProgramExit == 0) {
		usleep(5000);
	}
	printf("Shutting down...\n");
	close(ServerSocket);
	for(int i = 0; i < NUM_MAX_CLIENTS; i++) {
		if(Clients[i].fd != -1) { CloseClient(i); }
	}
	return 0;
}

void IntHwnd(int) {
	ProgramExit = 1;
}

int InstallHandler(int sign, void (*hwnd)() ) {
	struct sigaction s;
	memset(&s, 0, sizeof(s) );
	s.sa_handler = hwnd;
	return sigaction(sign, &s, NULL);
}

int MakeAsync(int fn) {
	if(fcntl(fn, F_SETFL, O_ASYNC) == -1 || fcntl(fn, F_SETOWN, getpid() ) == -1) {
		return -1;
	}
	return 0;
}

void IOHwnd(int) {
	//printf("Pollable IO event.\n");
	if(IOPending(ServerSocket) == 1) {
		NewClientHandler();
	}
	for(int i = 0; i < NUM_MAX_CLIENTS; i++) {
		if(Clients[i].fd != -1 && IOPending(Clients[i].fd) == 1) {
			ClientRecvHandler(i);
		}
	}
}

int IOPending(int fd) {
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN
	};
	return poll(&pfd, 1, 0);
}

void NewClientHandler() {
	int c;
	struct sockaddr_in padr;
	socklen_t rl = sizeof(padr);
	c = accept(ServerSocket, (struct sockaddr*)&padr, &rl);
	if(c == -1) {
		perror("accept");
		return;
	}
	printf("New client from %s (%d)\n", inet_ntoa(padr.sin_addr), ntohs(padr.sin_port) );
	for(int i = 0; i < NUM_MAX_CLIENTS; i++) {
		if(Clients[i].fd == -1) {
			printf("Client is saved as ID %d\n", i);
			Clients[i].fd = c;
			Clients[i].bufcur = 0;
			Clients[i].addr = padr.sin_addr.s_addr;
			Clients[i].port = ntohs(padr.sin_port);
			memset(Clients[i].name, 0, sizeof(Clients[i].name) );
			if(MakeAsync(c) == -1) {
				CloseClient(i);
				printf("Client disconnected, MakeAsync failed.\n");
			}
			return;
		}
	}
	close(c);
	printf("Client disconnected, no more space for clients.\n");
}

void CloseClient(int cliid) {
	close(Clients[cliid].fd);
	Clients[cliid].fd = -1;
}

void ClientRecvHandler(int cliid) {
	uint8_t b[NUM_NET_BUFSIZ];
	int r = recv(Clients[cliid].fd, b, sizeof(b), 0);
	if(r < 1) {
		printf("[Client %d] Less than 1 byte received. Client disconnected or recv error.\n", cliid);
		CloseClient(cliid);
		return;
	}
	printf("[Client %d] RX (%d): ", cliid, r);
	for(int i = 0; i < r; i++) {
		printf("%02x ", b[i]);
	}
	printf("\n");
	switch(b[0]) {
	case '0':
		//command '0', stack data to MessageBuffer
		if(r > 1) {
			if(AppendMessage(cliid, r - 1, &b[1]) == -1) {
				send(Clients[cliid].fd, "0-", 2, MSG_NOSIGNAL);
			}
		}
		break;
	case '1':
		//command '1', receive stacked data
		int pl = MessageBufCur - Clients[cliid].bufcur;
		if(pl != 0) {
			send(Clients[cliid].fd, &MessageBuffer[Clients[cliid].bufcur], pl, MSG_NOSIGNAL);
			Clients[cliid].bufcur += pl;
		}
		break;
	case '2':
		//command '2', login
		if(r - 1 < sizeof(Clients[cliid].name) &&
			strlen(Clients[cliid].name) == 0) {
			memcpy(Clients[cliid].name, &b[1], r - 1);
			send(Clients[cliid].fd, "2+", 2, MSG_NOSIGNAL);
		} else {
			send(Clients[cliid].fd, "2-", 2, MSG_NOSIGNAL);
		}
		break;
	case '3':
		//command '3', getusers
		if(r != 3) {
			send(Clients[cliid].fd, "3-", 2, MSG_NOSIGNAL);
		} else {
			int16_t *t = (int16_t*)&b[1];
			int tt = ntohs(*t);
			if(0 <= tt && tt <= NUM_MAX_CLIENTS) {
				if(Clients[tt].fd != -1 && strlen(Clients[tt].name) != 0) {
					send(Clients[cliid].fd, Clients[tt].name, strlen(Clients[tt].name), MSG_NOSIGNAL);
				} else {
					send(Clients[cliid].fd, "3-", 2, MSG_NOSIGNAL);
				}
			} else {
				send(Clients[cliid].fd, "3-", 2, MSG_NOSIGNAL);
			}
		}
		break;
	}
}

int AppendEvent(int cliid, uint8_t* ctx, int len) {
	stackedevent_hdr_t t = {
		.cid = cliid,
		.len = htons(pl)
	};
	if(MessageBufCur + sizeof(t) >= sizeof(MessageBuffer) ) { return -1; }
	memcpy(&MessageBuffer[MessageBufCur], &t, sizeof(t) );
	memcpy(&MessageBuffer[MessageBufCur + sizeof(t)], ctx, len);
	MessageBufCur += len + sizeof(t);
}
