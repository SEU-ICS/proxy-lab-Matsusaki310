#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct URL {
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
} URL;

void parseUrl(char *s, URL *url)
{
    char *ptr=strstr(s,"//");
    if(ptr!=NULL) s=ptr+2;
    
    ptr=strchr(s,'/');
    if(ptr!=NULL)
    {
        strcpy(url->path,ptr);
        *ptr='\0';
    }
    
    ptr=strchr(s,':');
    if(ptr!=NULL)
    {
        strcpy(url->port,ptr+1);
        *ptr='\0';
    }
    else strcpy(url->port,"80");

    strcpy(url->host,s);
}

void readClient(rio_t *rio, URL *url, char *data) {
    char host[MAXLINE];
    char line[MAXLINE];
    char other[MAXLINE];
    char method[MAXLINE], urlstr[MAXLINE], version[MAXLINE];

    Rio_readlineb(rio, line, MAXLINE);
    sscanf(line, "%s %s %s\n", method, urlstr, version);
    parseUrl(urlstr, url);

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

void doit(int connfd)
{
    rio_t rio;
    char line[MAXLINE];
    char data[MAXLINE];
    int len;
    Rio_readinitb(&rio,connfd);
    URL url;
    readClient(&rio,&url,data);
    int serverfd=open_clientfd(url.host, url.port);
    rio_readinitb(&rio, serverfd);
    Rio_writen(serverfd, data, strlen(data));
    len=Rio_readlineb(&rio,line,MAXLINE);
    while(len>0)
    {
        Rio_writen(connfd, line, len);
        len=Rio_readlineb(&rio,line,MAXLINE);
    }
    Close(serverfd);
}

void *thread(void *vargp)
{
    int connfd=*((int*)vargp);
    Pthread_detach(Pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int main(int argc, char **argv) 
{
    int listenfd;
    int *connfd;
    pthread_t tid;
    char host[MAXLINE], serv[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    listenfd = Open_listenfd(argv[1]);
    while(1) 
    {
        clientlen = sizeof(clientaddr);
        connfd=Malloc(sizeof(int));
        connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen); 
        Getnameinfo((SA*)&clientaddr,clientlen,host,MAXLINE,serv,MAXLINE,0);
        Pthread_creat(&tid,NULL,thread,connfd);
    }
    return 0;
}

