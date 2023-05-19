#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FILENAME "list.txt"
#define MAXLEN 100
#define PDR 0.1
#define PORT 8886

FILE *fp;

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
	exit(1);
}

void writeToFile(char *data, int len)
{
	if (ftell(fp))
		fputc(',', fp);

	while (len) {
		fputc(*data, fp);
		data++;
		len--;
	}
	fflush(fp);
}

int main(void)
{
	fp = fopen("list.txt", "w");
	if (!fp) {
		printf("Error opening file %s", FILENAME);
		exit(EXIT_FAILURE);
	}

	int sLen = sizeof(struct sockaddr_in), c1Len = sizeof(struct sockaddr_in), c2Len = sizeof(struct sockaddr_in);
	struct sockaddr_in serverAddr, client1Addr, client2Addr;
	// zero out the structures
	memset((char *)&serverAddr, 0, sizeof(struct sockaddr_in));
	memset((char *)&client1Addr, 0, sizeof(struct sockaddr_in));
	memset((char *)&client2Addr, 0, sizeof(struct sockaddr_in));
	DATA_PKT rcvPkt1, rcvPkt2;
	ACK_PKT ackPkt;
	ackPkt.size = 0;
	ackPkt.type = 'A';

	// create a UDP socket
	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), c1, c2;
	if (s == -1)
		die("socket");

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	// bind socket to port
	if (bind(s, (struct sockaddr *)&serverAddr, sizeof(struct sockaddr)) == -1)
		die("bind");

	if (listen(s, 2) == -1)
		die("listen");

	c1 = accept(s, (struct sockaddr *)&client1Addr, (socklen_t *)&c1Len);
	if (c1Len < 0)
		die("accept c1");
	c2 = accept(s, (struct sockaddr *)&client2Addr, (socklen_t *)&c2Len);
	if (c2Len < 0)
		die("accept c2");
	close(s);

	// Confirming both connections
	if (sendto(c1, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client1Addr, c1Len) == -1)
		die("sendTo()");
	if (sendto(c2, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client2Addr, c2Len) == -1)
		die("sendTo()");

	int c1Seq = 0, c2Seq = 0;

	srand(time(0));
	int state = 0, currClient = 0;
	while (1) {
		switch (state) {
		case 0: {
			if (!currClient) {
				if (recvfrom(c1, &rcvPkt1, sizeof(DATA_PKT), 0, (struct sockaddr *)&client1Addr, (socklen_t *)&c1Len) == -1)
					die("recvfrom()");

				double r = (double)rand() / RAND_MAX;
				if (r <= PDR) {
					printf("DROP PKT: Seq. No. = %d\n", rcvPkt1.sq_no);
					break;
				}

				if (rcvPkt1.sq_no < c1Seq) {
					ackPkt.sq_no = rcvPkt1.sq_no;
					if (sendto(c1, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client1Addr, c1Len) == -1)
						die("sendTo()");
					break;
				}

				c1Seq = rcvPkt1.sq_no + rcvPkt1.size;
				printf("RCVD PKT: Seq. No. = %d, Size = %d Bytes, %s\n", rcvPkt1.sq_no, rcvPkt1.size, rcvPkt1.data);

				writeToFile(rcvPkt1.data, rcvPkt1.size);
			} else {
				if (recvfrom(c2, &rcvPkt2, sizeof(DATA_PKT), 0, (struct sockaddr *)&client2Addr, (socklen_t *)&c2Len) == -1)
					die("recvfrom()");

				double r = (double)rand() / RAND_MAX;
				if (r <= PDR) {
					printf("DROP PKT: Seq. No. = %d\n", rcvPkt2.sq_no);
					break;
				}

				if (rcvPkt2.sq_no < c2Seq) {
					ackPkt.sq_no = rcvPkt2.sq_no;
					if (sendto(c2, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client2Addr, c2Len) == -1)
						die("sendTo()");
					break;
				}

				c2Seq = rcvPkt2.sq_no + rcvPkt2.size;
				printf("RCVD PKT: Seq. No. = %d, Size = %d Bytes, %s\n", rcvPkt2.sq_no, rcvPkt2.size, rcvPkt2.data);

				writeToFile(rcvPkt2.data, rcvPkt2.size);
			}

			state = 1;

			break;
		}
		case 1: {
			ackPkt.sq_no = !currClient ? rcvPkt1.sq_no : rcvPkt2.sq_no;
			if (!currClient) {
				if (sendto(c1, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client1Addr, c1Len) == -1)
					die("sendTo()");
			} else {
				if (sendto(c2, &ackPkt, sizeof(ACK_PKT), 0, (struct sockaddr *)&client2Addr, c2Len) == -1)
					die("sendTo()");
			}

			printf("SENT ACK: Seq. No. = %d\n", ackPkt.sq_no);

			currClient = !currClient;
			state = 0;
			break;
		}
		}
	}
	return 0;
}
