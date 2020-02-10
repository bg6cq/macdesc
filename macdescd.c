/*
887E25	Extreme Networks, Inc.
086083	zte corporation
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define MAXEVENTS 64

#define MAXLEN 1024

int port = 80;
int fork_and_do = 0;
int debug = 0;
int ipv6 = 0;
char ouidbfilename[MAXLEN];

char *http_head =
    "HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Type: text/html; charset=UTF-8\r\nServer: web server by james@ustc.edu.cn, data from http://standards-oui.ieee.org/oui.txt\r\n\r\n";

#define HASHBKT 20000

#define OUILEN 6

struct oui_struct {
	char oui[OUILEN + 1];
	char *org;
	struct oui_struct *next;
};

struct oui_struct *ouies[HASHBKT];

static inline unsigned int hash_key(char *s)
{
	unsigned int k = 0;
	while (*s)
		k = (k << 5) + k + *s++;
	k = k % HASHBKT;
	return k;
}

static inline void hash_add(struct oui_struct *s)
{
	unsigned int k = hash_key(s->oui);
	s->next = ouies[k];
	ouies[k] = s;
}

static inline struct oui_struct *hash_find(char *oui)
{
	struct oui_struct *s;
	unsigned int k = hash_key(oui);
	if (debug >= 2)
		printf("hash_find %s\n", oui);
	s = ouies[k];
	while (s) {
		if (strcmp(s->oui, oui) == 0)
			return s;
		s = s->next;
	}
	return NULL;
}

void find(char *mac, char *result, int len)
{
	char oui[OUILEN + 1];
	oui[0] = 0;
	if (debug >= 2)
		printf("find: %s\n", mac);
	char *p;
	p = mac;
	int n;
	for (n = 0; n < OUILEN; n++) {
		while (*p && (*p != ' ')) {
			if ((*p >= '0' && *p <= '9')
			    || (*p >= 'A' && *p <= 'F'))
				break;
			else if (*p >= 'a' && *p <= 'f') {
				*p = *p - 'a' + 'A';
				break;
			} else {
				p++;
				continue;
			}
		}
		if ((*p) == ' ')
			break;
		oui[n] = *p;
		if ((*p) == 0)
			break;
		p++;
	}
	oui[n] = 0;
	result[0] = 0;
	if (strlen(oui) != OUILEN)
		return;
	struct oui_struct *s;
	s = hash_find(oui);
	if (s)
		strncpy(result, s->org, len);
	if (debug >= 2)
		printf("result %s\n", result);
	return;
}

void load_oui(char *filename)
{
	FILE *fp;
	char buf[MAXLEN];
	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("file %s open error\n", filename);
		exit(-1);
	}
	while (fgets(buf, MAXLEN, fp)) {
		char *p;
		if (strlen(buf) < 7)
			continue;
		if (buf[strlen(buf) - 1] == '\n')
			buf[strlen(buf) - 1] = 0;
		p = buf + 6;
		if (*p != '\t')
			continue;
		*p = 0;
		p++;
		struct oui_struct *s;
		s = malloc(sizeof(s));
		if (s == NULL)
			continue;
		s->org = malloc(strlen(p));
		if (s->org == NULL) {
			free(s);
			continue;
		}
		strcpy(s->org, p);
		strcpy(s->oui, buf);
		hash_add(s);
	}
	fclose(fp);
}

void respond(int cfd, char *mesg)
{
	char buf[MAXLEN], *p = mesg;
	char result[MAXLEN];
	int len = 0;

	if (debug >= 2)
		printf("From Client(fd %d):\n%s##END\n", cfd, mesg);

	buf[0] = 0;
	if (memcmp(p, "GET /", 5) == 0) {
		if (memcmp(p + 5, "favicon.ico", 11) == 0)
			len = snprintf(buf, MAXLEN, "HTTP/1.0 404 OK\r\nConnection: close\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n");
		else {
			find(p + 5, result, 128);
			if (result[0])
				len = snprintf(buf, MAXLEN, "%s%s", http_head, result);
			else
				len = snprintf(buf, MAXLEN, "%s未知", http_head);
		}
	}

	if (debug >= 2)
		printf("Send to Client(fd %d):\n%s##END\n", cfd, buf);
	write(cfd, buf, len);
}

int set_socket_non_blocking(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;
	return 0;
}

void set_socket_keepalive(int fd)
{
	int keepalive = 1;	// 开启keepalive属性
	int keepidle = 5;	// 如该连接在60秒内没有任何数据往来,则进行探测
	int keepinterval = 5;	// 探测时发包的时间间隔为5 秒
	int keepcount = 3;	// 探测尝试的次数。如果第1次探测包就收到响应了,则后2次的不再发
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive, sizeof(keepalive));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *)&keepidle, sizeof(keepidle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount));
}

void usage(void)
{
	printf("Usage:\n");
	printf("   macdescd [ -d debug_level ] [ -f ] [ -6 ] [ -o ouifile_name ] [ tcp_port ]\n");
	printf("        -d debug, level 1: print socket op, 2: print msg\n");
	printf("        -f fork and do\n");
	printf("        -6 support ipv6\n");
	printf("        -o ouidbfile_name, default is oui.db\n");
	printf("        default port is 80\n");
	exit(0);
}

int bind_and_listen(void)
{
	int listenfd;
	int enable = 1;

	if (ipv6)
		listenfd = socket(AF_INET6, SOCK_STREAM, 0);
	else
		listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		perror("error: socket");
		exit(-1);
	}
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("error: setsockopt(SO_REUSEADDR)");
		exit(-1);
	}
	if (ipv6) {
		static struct sockaddr_in6 serv_addr6;
		memset(&serv_addr6, 0, sizeof(serv_addr6));
		serv_addr6.sin6_family = AF_INET6;
		serv_addr6.sin6_port = htons(port);
		if (bind(listenfd, (struct sockaddr *)&serv_addr6, sizeof(serv_addr6)) < 0) {
			perror("error: bind");
			exit(-1);
		}
	} else {
		static struct sockaddr_in serv_addr;
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		serv_addr.sin_port = htons(port);
		if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
			perror("error: bind");
			exit(-1);
		}
	}
	if (set_socket_non_blocking(listenfd) < 0) {
		perror("error: set_socket_non_blocking");
		exit(-1);
	}
	if (listen(listenfd, 64) < 0) {
		perror("error: listen");
		exit(-1);
	}
	return listenfd;
}

int main(int argc, char *argv[])
{
	int listenfd, efd;
	int idle_fd = open("/dev/null", O_RDONLY);	// fd for accept no file err
	struct epoll_event event, *events;

	strcpy(ouidbfilename, "oui.db");

	int c;
	while ((c = getopt(argc, argv, "d:o:f6h")) != EOF)
		switch (c) {
		case 'd':
			debug = atoi(optarg);;
			break;
		case 'o':
			strncpy(ouidbfilename, optarg, MAXLEN - 1);
			break;
		case 'f':
			fork_and_do = 1;
			break;
		case '6':
			ipv6 = 1;
			break;
		case 'h':
			usage();

		};
	if (optind == argc - 1)
		port = atoi(argv[optind]);
	if (port < 0 || port > 65535) {
		printf("Invalid port number %d, please try [1,65535]", port);
		exit(-1);
	}

	(void)signal(SIGCLD, SIG_IGN);
	(void)signal(SIGHUP, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);

	if (fork_and_do) {
		if (debug)
			printf("I am parent, pid: %d\n", getpid());
		while (1) {
			int pid = fork();
			if (pid == 0)	// child do the job
				break;
			else {
				if (debug)
					printf("I am parent, waiting for child...\n");
				wait(NULL);
			}
			if (debug)
				printf("child exit? I will restart it.\n");
			sleep(2);
		}
		if (debug)
			printf("I am child, I am doing the job\n");
	}
	printf("web server started at port: %d, my pid: %d\n", port, getpid());

	load_oui(ouidbfilename);

	listenfd = bind_and_listen();
	if ((efd = epoll_create1(0)) < 0) {
		perror("error: epoll_create1");
		exit(-1);
	}
	event.data.fd = listenfd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		perror("error: epoll_ctl_add of listenfd");
		exit(-1);
	}
	/* Buffer where events are returned */
	events = calloc(MAXEVENTS, sizeof event);
	if (events == NULL) {
		perror("error: calloc memory");
		exit(-1);
	}
	// Event Loop 
	while (1) {
		int n, i;
		n = epoll_wait(efd, events, MAXEVENTS, -1);
		for (i = 0; i < n; i++) {
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
				/* An error has occured on this fd, or the socket is not
				 * ready for reading (why were we notified then?) */
				printf("epollerr or epollhup event of fd %d\n", events[i].data.fd);
				close(events[i].data.fd);
				continue;
			}
			if (!(events[i].events & EPOLLIN)) {
				printf("error: unknow event of fd %d\n", events[i].data.fd);
				close(events[i].data.fd);
				continue;
			}
			if (listenfd == events[i].data.fd) {
				/* notification on the listening socket, which
				 * means one or more incoming connections. */
				while (1) {
					int infd;
					infd = accept(listenfd, NULL, 0);
					if (infd == -1) {
						if ((errno == EAGAIN) || (errno == EWOULDBLOCK))	/*  all incoming connections processed. */
							break;
						else if ((errno == EMFILE) || (errno == ENFILE)) {
							perror("error: first accept");
							close(idle_fd);
							infd = accept(listenfd, NULL, 0);
							if (infd == -1) {
								if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {	/*  all incoming connections processed. */
									idle_fd = open("/dev/null", O_RDONLY);
									break;
								} else {
									perror("error: sencond accept");
									exit(-1);
								}
							}
							close(infd);
							idle_fd = open("/dev/null", O_RDONLY);
							continue;
						} else {
							perror("error: accept new client");
							exit(-1);
						}
					}
					if (debug) {
						struct sockaddr_storage in_addr;
						socklen_t in_len = sizeof(in_addr);
						char hbuf[INET6_ADDRSTRLEN];

						getpeername(infd, (struct sockaddr *)&in_addr, &in_len);
						if (in_addr.ss_family == AF_INET6) {
							struct sockaddr_in6 *r = (struct sockaddr_in6 *)&in_addr;
							inet_ntop(AF_INET6, &r->sin6_addr, hbuf, sizeof(hbuf));
							printf("new connection on fd %d " "(host=%s, port=%d)\n", infd, hbuf, ntohs(r->sin6_port));
						} else if (in_addr.ss_family == AF_INET) {
							struct sockaddr_in *r = (struct sockaddr_in *)&in_addr;
							inet_ntop(AF_INET, &r->sin_addr, hbuf, sizeof(hbuf));
							printf("new connection on fd %d " "(host=%s, port=%d)\n", infd, hbuf, ntohs(r->sin_port));
						}
					}

					/* set the incoming socket non-blocking and add it to the list of fds to monitor. */
					if (set_socket_non_blocking(infd) < 0) {
						perror("error: set_socket_non_blocking of new client");
						close(infd);
						continue;
					}
					set_socket_keepalive(infd);
					event.data.fd = infd;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event) < 0) {
						perror("error: epoll_ctl_add new client");
						close(infd);
					}
				}
				continue;
			} else if (events[i].events & EPOLLIN) {
				/* new data on the fd waiting to be read.
				 *
				 * We only read the first packet, for normal http client, it's OK */
				ssize_t count;
				char buf[MAXLEN];

				count = read(events[i].data.fd, buf, MAXLEN - 1);
				if (count > 0) {
					buf[count] = 0;
					respond(events[i].data.fd, buf);
				}
				if (debug)
					printf("close fd %d\n", events[i].data.fd);
				shutdown(events[i].data.fd, SHUT_RDWR);
				close(events[i].data.fd);
			}
		}
	}
}
