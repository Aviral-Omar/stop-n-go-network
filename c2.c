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
	DATA_PKT sendPkt;
	ACK_PKT rcvAck;
	sendPkt.sq_no = 0;

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
	if (recvfrom(s, &rcvAck, sizeof(ACK_PKT), 0, (struct sockaddr *)&serverAddr, (socklen_t *)&sLen) == -1)
		die("recvfrom()");

	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
		die("fcntl");

	fd_set fds;
	int state = 0, ackRecv = 1, retransmit = 0, offset = 0;
	while (!(fileEnd && ackRecv)) {
		timeout.tv_sec = 2;
		switch (state) {
		case 0: {
			if (ackRecv) {
				getNextData(sendPkt.data);
				sendPkt.sq_no = offset;
				sendPkt.size = strlen(sendPkt.data);
				sendPkt.type = 'D';
			} else
				retransmit = 1;
			if (sendto(s, &sendPkt, sizeof(DATA_PKT), 0, (struct sockaddr *)&serverAddr, sLen) == -1)
				die("sendto()");

			ackRecv = 0;

			if (!retransmit)
				printf("SENT PKT: Seq. No. = %d, Size = %d Bytes\n", sendPkt.sq_no, sendPkt.size);
			else {
				printf("RE-TRANSMIT PKT: Seq. No. = %d, Size = %d Bytes\n", sendPkt.sq_no, sendPkt.size);
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

			if (recvfrom(s, &rcvAck, sizeof(ACK_PKT), 0, (struct sockaddr *)&serverAddr, (socklen_t *)&sLen) == -1)
				die("recvfrom()");

			printf("RCVD ACK: Seq. No. = %d\n", rcvAck.sq_no);

			if (rcvAck.sq_no != offset) {
				state = 0;
				break;
			}
			ackRecv = 1;


			offset = sendPkt.sq_no + sendPkt.size;

			state = 0;
			break;
		}
		}
	}

	close(s);
	return 0;
}
