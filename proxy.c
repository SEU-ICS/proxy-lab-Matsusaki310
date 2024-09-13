#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NTHREADS 4
#define SBUFSIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct URL
{
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}URL;

typedef struct
{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

sbuf_t sbuf;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void doit(int connfd);
void parseUrl(char *s, URL *url);
void thread(void *vargp);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char host[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    sbuf_init(&sbuf, SBUFSIZE);
    for (int i=0;i<NTHREADS; ++i) Pthread_create(&tid, NULL, thread, NULL);

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen=sizeof(clientaddr);
        connfd=Accept(listenfd,(SA *)&clientaddr,&clientlen);
        Getnameinfo((SA *) &clientaddr,clientlen,host,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s, %s)\n",host,port);
        sbuf_insert(&sbuf,connfd);
    }
}

void thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while(1)
    {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int connfd)
{
    rio_t rio;
    char line[MAXLINE];
    Rio_readinitb(&rio, connfd);
    
    URL url;
    char data[MAXLINE];
    readClient(&rio, &url, data);

    int serverfd = open_clientfd(url.host, url.port);
    if (serverfd < 0) printf("Connection failed!\n");
    
    rio_readinitb(&rio, serverfd);
    Rio_writen(serverfd, data, strlen(data));
    
    int len;
    while ((len = Rio_readlineb(&rio, line, MAXLINE)) > 0)
        Rio_writen(connfd, line, len);
    
    Close(serverfd);
}

void parseUrl(char *s, URL *url) 
{
    char *ptr = strstr(s, "//");
    if (ptr != NULL) s = ptr + 2;

    ptr = strchr(s, '/');
    if (ptr != NULL) {
        strcpy(url->path, ptr);
        *ptr = '\0';
    }
    
    ptr = strchr(s, ':');
    if (ptr != NULL) {
        strcpy(url->port, ptr + 1);
        *ptr = '\0';
    } else strcpy(url->port, "80");

    strcpy(url->host, s);
}

void readClient(rio_t *rio, URL *url, char *data) {
    char host[MAXLINE];
    char line[MAXLINE];
    char other[MAXLINE];
    char method[MAXLINE], str[MAXLINE], version[MAXLINE];

    Rio_readlineb(rio, line, MAXLINE);
    sscanf(line, "%s %s %s\n", method, str, version);
    parseUrl(str, url);

    sprintf(host, "Host: %s\r\n", url->host);
    while (Rio_readlineb(rio, line, MAXLINE) > 0) {
        if (strcmp(line, "\r\n") == 0) break;
        if (strncmp(line, "Host", 4) == 0) strcpy(host, line);
        if (strncmp(line, "User-Agent", 10) &&
            strncmp(line, "Connection", 10) &&
            strncmp(line, "Proxy-Connection", 16)) strcat(other, line);
    }
    
    sprintf(data, "%s %s HTTP/1.0\r\n"
                     "%s%s"
                     "Connection: close\r\n"
                     "Proxy-Connection: close\r\n"
                     "%s\r\n", method, url->path, host, user_agent_hdr, other);
}

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int)); 
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0); 
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}
