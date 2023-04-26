#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define FILENAME "id.txt"
#define MAXLEN 100	// Max length of buffer
#define SERVERIP "127.0.0.1"
#define PORT 8886  // The port on which to send data

FILE *fp;
int fileEnd = 0;

typedef struct packet1 {
	int size;
	int sq_no;
	// 'A' for ACK, 'D' for data
	char type;
} ACK_PKT;

typedef struct packet2 {
	int size;
	int sq_no;
	// 'A' for ACK, 'D' for data
	char type;
	char data[MAXLEN];
} DATA_PKT;

void die(const char *s)
{
	perror(s);
	exit(EXIT_FAILURE);
}

void getNextData(char *buf)
{
	char *bufptr = buf;
	while ((int)(bufptr - buf) < (MAXLEN - 1) && (*bufptr = fgetc(fp)) != EOF && *bufptr != ',' && *bufptr != '.')
		bufptr++;

	if (*bufptr == '.')
		fileEnd = 1;

	*bufptr = '\0';
}

int main(void)
{
	// File handling
	fp = fopen(FILENAME, "r");
	if (!fp) {
		printf("Error opening file %s", FILENAME);
		exit(EXIT_FAILURE);
	}

	struct timeval timeout;
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;

	int sLen = sizeof(struct sockaddr_in);
	struct sockaddr_in serverAddr;
	memset((char *)&serverAddr, 0, sizeof(struct sockaddr_in));
	DATA_PKT send_pkt;
	ACK_PKT rcv_ack;
	send_pkt.sq_no = 0;

	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == -1)
		die("socket");

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = inet_addr(SERVERIP);

	if (connect(s, (struct sockaddr *)&serverAddr, sizeof(struct sockaddr)) == -1) {
		printf("Error while establishing connection\n");
		exit(EXIT_FAILURE);
	}

	// Waiting for client 2 to also connect
	if (recvfrom(s, &rcv_ack, sizeof(ACK_PKT), 0, (struct sockaddr *)&serverAddr, (socklen_t *)&sLen) == -1)
		die("recvfrom()");

	int flags = 1;
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
		die("fcntl");

	fd_set fds;
	int state = 0, ackRecv = 1, retransmit = 0;
	while (!(fileEnd && ackRecv)) {
		timeout.tv_sec = 2;
		switch (state) {
		case 0: {
			if (ackRecv) {
				getNextData(send_pkt.data);
				send_pkt.size = strlen(send_pkt.data);
				send_pkt.type = 'D';
			} else
				retransmit = 1;
			if (sendto(s, &send_pkt, sizeof(DATA_PKT), 0, (struct sockaddr *)&serverAddr, sLen) == -1)
				die("sendto()");

			ackRecv = 0;

			if (!retransmit)
				printf("SENT PKT: Seq. No. = %d, Size = %d Bytes\n", send_pkt.sq_no, send_pkt.size);
			else {
				printf("RE-TRANSMIT PKT: Seq. No. = %d, Size = %d Bytes\n", send_pkt.sq_no, send_pkt.size);
				retransmit = 0;
			}

			state = 1;
			break;
		}
		case 1: {
			FD_ZERO(&fds);
			FD_SET(s, &fds);

			int rc = select(s + 1, &fds, NULL, NULL, &timeout);
			if (rc < 0)
				die("select");
			else if (rc == 0) {
				state = 0;
				break;
			}

			if (recvfrom(s, &rcv_ack, sizeof(ACK_PKT), 0, (struct sockaddr *)&serverAddr, (socklen_t *)&sLen) == -1)
				die("recvfrom()");
			ackRecv = 1;

			printf("RCVD ACK: Seq. No. = %d\n", rcv_ack.sq_no);

			if (rcv_ack.sq_no == send_pkt.sq_no)
				send_pkt.sq_no += send_pkt.size;

			state = 0;
			break;
		}
		}
	}

	close(s);
	return 0;
}
