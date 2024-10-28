#include <stdio.h>

#include <allegro5/allegro5.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>

#define NET_BUFFER_LEN 4096
#define NUM_WWIDTH 800
#define NUM_WHEIGHT 600
#define NUM_MWIDTH 2000
#define NUM_MHEIGHT 2000
#define NUM_CHARACTER_SLOTS 200
#define NUM_MESSAGE_SLOTS 10
#define NUM_MESSAGE_BUFFER 512
#define NUM_MOUSE_BUTTON_LEFT 1
#define NUM_MOUSE_BUTTON_RIGHT 2
#define PACKED __attribute__(( packed ))

typedef struct {
	float x, y;
	int tid;
	int imgid;
	int objid;
	int clientid;
} GameObj_t;

typedef struct PACKED {
	uint8_t cmdtype; // '0'
	uint8_t subcmdtype; // 'A'
	uint16_t x, y;
} NP_GOAdd_t;

typedef struct PACKED {
	uint8_t cid;
	uint16_t len;
} stackedevent_hdr_t;

ALLEGRO_FONT *PlFont;
//ALLEGRO_BITMAP *PlImgs[];
char ChatBuffer[NUM_MESSAGE_SLOTS][NUM_MESSAGE_BUFFER];
ALLEGRO_COLOR C_WHITE, C_BLACK;
bool ProgramExit = false;
char CmdBuffer[64];
int CBCursor, CBLength = -1;
int ClientSocket = -1;
GameObj_t Gobjs[NUM_CHARACTER_SLOTS];
int CameraX = 0, CameraY = 0;
int MouseX = 0, MouseY = 0;
int pcurx, pcury;
bool MoveMode = false;

void AddCharacter(float, float);
void DrawScreen();
void KeyHandler(ALLEGRO_EVENT);
void KeyReleaseHandler(ALLEGRO_EVENT);
void TextInputKeyHandler(ALLEGRO_EVENT);
void GameKeyHandler(ALLEGRO_EVENT);
void CommandExecHandler();
void IOHandler(int);
void GameTick();
void MouseHandlerPress(ALLEGRO_EVENT);
int NumConst(int, int, int);
void NetEventHandler(char*, int, int);
void Chat(char*);

int main(int argc, char* argv[]) {
	memset(ChatBuffer, 0, sizeof(ChatBuffer) );
	for(int i = 0; i < NUM_CHARACTER_SLOTS; i++) {
		Gobjs[i].objid = -1;
	}
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa) );
	sa.sa_handler = IOHandler;
	if(sigaction(SIGIO, &sa, NULL) == -1) {
		perror("sigaction fail");
		return 1;
	}
	if(al_init() == false || al_init_primitives_addon() == false) {
		printf("Allegro Init failed.\n");
		return 1;
	}
	al_install_mouse();
	al_install_keyboard();
	C_WHITE = al_map_rgb(255,255,255);
	C_BLACK = al_map_rgb(0, 0, 0);
	ALLEGRO_TIMER *timer = al_create_timer(1.0 / 30.0);
	ALLEGRO_EVENT_QUEUE *queue = al_create_event_queue();
	ALLEGRO_DISPLAY *disp = al_create_display(NUM_WWIDTH, NUM_WHEIGHT);
	if(disp == NULL) {
		printf("Display init failed.\n");
		return 1;
	}
	PlFont = al_create_builtin_font();
	al_register_event_source(queue, al_get_keyboard_event_source() );
	al_register_event_source(queue, al_get_display_event_source(disp) );
	al_register_event_source(queue, al_get_timer_event_source(timer) );
	al_register_event_source(queue, al_get_mouse_event_source() );
	bool redraw = true;
	ALLEGRO_EVENT evt;
	al_start_timer(timer);
	while(ProgramExit == false) {
		al_wait_for_event(queue, &evt);
		switch(evt.type) {
		case ALLEGRO_EVENT_TIMER:
			GameTick();
			redraw = true;
			break;
		case ALLEGRO_EVENT_DISPLAY_CLOSE:
			ProgramExit = true;
			break;
		case ALLEGRO_EVENT_KEY_CHAR:
			KeyHandler(evt);
			break;
		case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
			MouseHandlerPress(evt);
			break;
		case ALLEGRO_EVENT_MOUSE_AXES:
			MouseX = evt.mouse.x;
			MouseY = evt.mouse.y;
			break;
		case ALLEGRO_EVENT_KEY_UP:
			KeyReleaseHandler(evt);
			break;
		}
		if(redraw && al_is_event_queue_empty(queue) ) {
			DrawScreen();
			redraw = true;
		}
	}
	al_destroy_font(PlFont);
	al_destroy_display(disp);
	al_destroy_timer(timer);
	al_destroy_event_queue(queue);
	if(ClientSocket != -1) {
		close(ClientSocket);
	}
	return 0;
}

void DrawScreen() {
	const int FHeight = al_get_font_line_height(PlFont);
	al_clear_to_color( C_BLACK );
	if(CBLength != -1) {
		//Shorten text until it fits in window
		char showtext[sizeof(CmdBuffer)];
		int beginpos = 0;
		int curx = 0;
		while(CBCursor > beginpos) {
			int cplen = CBCursor - beginpos;
			memcpy(showtext, &CmdBuffer[beginpos], cplen);
			showtext[cplen] = 0;
			curx = al_get_text_width(PlFont, showtext);
			if(curx < NUM_WWIDTH) {
				break;
			}
			beginpos++;
		}
		al_draw_filled_rectangle(0, 0, NUM_WWIDTH, FHeight + 3, C_WHITE );
		al_draw_text(PlFont, C_BLACK, 0, 2, 0, &CmdBuffer[beginpos]);
		al_draw_line(curx, 0, curx, FHeight + 3, C_BLACK, 1);
	}
	al_draw_textf(PlFont, C_WHITE, 0, FHeight + 5, 0, "CameraX=%d CameraY=%d MouseX=%d MouseY=%d", CameraX, CameraY, MouseX, MouseY);
	for(int i = 0; i < NUM_CHARACTER_SLOTS; i++) {
		if(Gobjs[i].objid != -1) {
			float x = Gobjs[i].x - CameraX - 5;
			float y = NUM_WHEIGHT - (Gobjs[i].y - CameraY - 5);
			al_draw_filled_rectangle(x, y, x + 5, y + 5, C_WHITE);
		}
	}
	al_flip_display();
}

void TextInputKeyHandler(ALLEGRO_EVENT evt) {
	int r = evt.keyboard.unichar;
	if(0x20 <= r && r <= 0x7e) {
		if(CBLength < sizeof(CmdBuffer) - 1) {
			//shift right string
			int shiftlen = CBLength - CBCursor;
			for(int i = 0; i < shiftlen; i++) {
				int shiftpos = CBLength - i - 1;
				CmdBuffer[shiftpos + 1] = CmdBuffer[shiftpos];
			}
			CmdBuffer[CBCursor] = r;
			CBLength++;
			CBCursor++;
		}
	} else {
		switch(evt.keyboard.keycode) {
		case ALLEGRO_KEY_BACKSPACE:
			if(CBCursor > 0) {
				//shift left string
				int shiftlen = CBLength - CBCursor;
				for(int i = 0; i < shiftlen; i++) {
					int shiftpos = i + CBCursor;
					CmdBuffer[shiftpos - 1] = CmdBuffer[shiftpos];
				}
				CBLength--;
				CBCursor--;
				CmdBuffer[CBLength] = 0;
			}
		break;
		case ALLEGRO_KEY_LEFT:
			if(CBCursor > 0) {
				CBCursor--;
			}
		break;
		case ALLEGRO_KEY_RIGHT:
			if(CBCursor < CBLength) {
				CBCursor++;
			}
		break;
		case ALLEGRO_KEY_ESCAPE:
			CBCursor = -1;
			break;
		case ALLEGRO_KEY_ENTER:
			if(CBLength > 0) {
				CommandExecHandler();
			}
			CBLength = -1;
			break;
		}
	}
}

void GameKeyHandler(ALLEGRO_EVENT evt) {
	switch(evt.keyboard.keycode) {
	case ALLEGRO_KEY_T:
		MoveMode = false;
		CBCursor = 0;
		memset(CmdBuffer, 0, sizeof(CmdBuffer) );
		CBLength = 0;
	break;
	case ALLEGRO_KEY_M:
		pcurx = MouseX;
		pcury = MouseY;
		MoveMode = true;
	break;
	}
}

void KeyHandler(ALLEGRO_EVENT evt) {
	//printf("keyhwnd\n");
	if(CBLength == -1) {
		GameKeyHandler(evt);
	} else {
		TextInputKeyHandler(evt);
	}
}

void CommandExecHandler() {
	if(memcmp(CmdBuffer, "/connect ", 9) == 0) {
		//Connect Command
		if(ClientSocket != -1) {
			printf("Already connected.\n");
			return;
		}
		char hostname[256];
		char portname[6] = "25566";
		char *seppos = strchr(&CmdBuffer[9], ':');
		if(seppos != NULL) {
			int hnlen = seppos - &CmdBuffer[9];
			if(hnlen >= sizeof(hostname) ) {
				printf("Too long hostname.\n");
				return;
			}
			memcpy(hostname, &CmdBuffer[9], hnlen);
			hostname[hnlen] = 0;
			strncpy(portname, seppos + 1, sizeof(portname) );
			portname[sizeof(portname) - 1] = 0;
		} else {
			strncpy(hostname, &CmdBuffer[9], sizeof(hostname) );
			hostname[sizeof(hostname) - 1] = 0;
		}
		printf("Connecting to %s:%s\n", hostname, portname);
		//Make Socket
		ClientSocket = socket(AF_INET, SOCK_STREAM, 0);
		if(ClientSocket == -1) {
			perror("Socket failed");
			return;
		}
		//Make address
		struct addrinfo *caddr;
		int r = getaddrinfo(hostname, portname, NULL, &caddr);
		if(r != 0) {
			if(r == EAI_SYSTEM) {
				perror("getaddrinfo failed");
			} else {
				printf("getaddrinfo failed: %s\n", gai_strerror(r) );
			}
			close(ClientSocket);
			ClientSocket = -1;
			return;
		}
		//Connect
		if(connect(ClientSocket, caddr->ai_addr, caddr->ai_addrlen) == -1) {
			perror("connect");
			close(ClientSocket);
			ClientSocket = -1;
		}
		freeaddrinfo(caddr);
		if(ClientSocket == -1) {
			return;
		}
		if(fcntl(ClientSocket, F_SETFL, O_ASYNC) == -1 || fcntl(ClientSocket, F_SETOWN, getpid() ) == -1) {
			close(ClientSocket);
			return;
		}
	} else if(strcmp(CmdBuffer, "/disconnect") == 0) {
		//disconnect command
		if(ClientSocket == -1) {
			printf("Not connected.\n");
			return;
		}
		close(ClientSocket);
		ClientSocket = -1;
	} else {
		//Unknown command
		if(ClientSocket != -1) {
			char sendpacket[sizeof(CmdBuffer) + 3];
			sendpacket[0] = '0';
			sendpacket[1] = 'c';
			sendpacket[2] = CBLength;
			memcpy(&sendpacket[3], CmdBuffer, CBLength);
			send(ClientSocket, sendpacket, CBLength + 3, MSG_NOSIGNAL);
		}
	}
}

void IOHandler(int) {
	char b[NET_BUFFER_LEN];
	int r;
	r = recv(ClientSocket, b, sizeof(b), 0);
	if(r < 1) {
		if(r == 0) {
			printf("Server connection closed\n");
		} else {
			perror("recv");
		}
		close(ClientSocket);
		ClientSocket = -1;
		return;
	}
	printf("RX: ");
	for(int i = 0; i < r; i++) {
		printf("%02x ", (uint8_t)b[i]);
	}
	printf("\n");
	int p = 0;
	while(r > p) {
		stackedevent_hdr_t *evinfo = (stackedevent_hdr_t*)&b[p];
		printf("event src=%d len=%d\n", evinfo->cid, ntohs(evinfo->len) );
		if(ntohs(evinfo->len) < 2) {
			printf("Bad event length.\n");
		} else {
			NetEventHandler(&b[p + sizeof(stackedevent_hdr_t)], ntohs(evinfo->len), evinfo->cid);
		}
		p += ntohs(evinfo->len) + sizeof(stackedevent_hdr_t);
	}
}

void NetEventHandler(char *evdata, int evlen, int evsrc) {
	switch(evdata[0]) {
		case 'c':
		int chatlen = evdata[1];
		char chatctx[1024];
		memcpy(chatctx, &evdata[2], chatlen);
		chatctx[chatlen] = 0;
		printf("[Net] Chat Rreceived[%d]: %s\n", evsrc, chatctx);
		break;
		case 'A':
		uint16_t *x = (uint16_t*)&evdata[1];
		uint16_t *y = (uint16_t*)&evdata[3];
		printf("[Net] Add Character[%d]: X: %d Y:%d\n", evsrc, ntohs(*x), ntohs(*y) );
		AddCharacter(ntohs(*x), ntohs(*y) );
		break;
		default:
		printf("[Net] Unknown packet\n");
		break;
	}
}

void GameTick() {
	if(ClientSocket != -1) {
		send(ClientSocket, "1", 1, MSG_NOSIGNAL);
	}
	if(MoveMode == true) {
		if(pcurx != MouseX || pcury != MouseY && (pcurx > 0 && pcury > 0) ) {
			CameraX = NumConst(CameraX + (MouseX - pcurx), 0, NUM_MWIDTH - NUM_WWIDTH);
			CameraY = NumConst(CameraY + (MouseY - pcury), 0, NUM_MHEIGHT - NUM_WHEIGHT);
		}
		pcurx = MouseX;
		pcury = MouseY;
	}
}

void MouseHandlerPress(ALLEGRO_EVENT evt) {
	printf("mouse: %d\n", evt.mouse.button);
	switch(evt.mouse.button) {
	case NUM_MOUSE_BUTTON_LEFT:
		int x = CameraX + MouseX;
		int y = (NUM_WHEIGHT - MouseY) + CameraY;
		AddCharacter(x, y);
		if(ClientSocket != -1) {
			NP_GOAdd_t t = {
				.cmdtype = '0',
				.subcmdtype = 'A',
				.x = htons(x),
				.y = htons(y)
			};
			send(ClientSocket, &t, sizeof(t), MSG_NOSIGNAL);
		}
	break;
	}
}

void KeyReleaseHandler(ALLEGRO_EVENT evt) {
	switch(evt.keyboard.keycode) {
	case ALLEGRO_KEY_M:
		MoveMode = false;
	break;
	}
}

int NumConst(int v, int mi, int ma) {
	if(v < mi) {
		return mi;
	} else if(v > ma) {
		return ma;
	}
	return v;
}

void AddCharacter(float x, float y) {
	printf("AddCharacter(%f, %f)\n", x, y);
	for(int i = 0; i < NUM_CHARACTER_SLOTS; i++) {
		if(Gobjs[i].objid == -1) {
			Gobjs[i].objid = 0;
			Gobjs[i].x = x;
			Gobjs[i].y = y;
			return;
		}
	}
	ProgramExit = true;
	printf("Too many objects, Program will now close.\n");
}

void Chat(char *ctx) {
	
}
