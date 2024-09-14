#include"csapp.h"
#include<stdio.h>
#include<stdbool.h>


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_SIZE 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct URL
{
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}URL;

typedef struct Cache
{
    int lru;
    URL url;
    char data[MAX_OBJECT_SIZE];
}Cache;
Cache c[CACHE_SIZE];

typedef struct Lock
{
    sem_t mutex;
    sem_t lock;
    int cnt;
}Lock;

Lock* sig;

bool urlEqual(URL *a,URL *b)
{
    return strcmp(a->host,b->host)==0&&strcmp(a->port,b->port)==0&&strcmp(a->path,b->path)==0;
}

int maxlru()
{
    int max=0;
    for(int i=0;i<CACHE_SIZE;i++) 
    {
        if(c[i].lru>max) max=c[i].lru;
    }
    return max;
}

void urlCopy(URL *a, const URL *b)
{
    strcpy(a->host, b->host);
    strcpy(a->port, b->port);
    strcpy(a->path, b->path);
}

char *readCache(URL *url)
{
    sem_wait(&sig->mutex);
    if(sig->cnt==1) sem_wait(&sig->lock);
    sig->cnt++;
    sem_post(&sig->mutex);
    char *data=NULL;
    for(int i=0;i<CACHE_SIZE;i++)
    {
        if(urlEqual(url,&c[i].url))
        {
            data=(char*)Malloc(strlen(c[i].data));
            strcpy(data,c[i].data);
            int max=maxlru();
            c[i].lru=max+1;
            break;
        }
    }
    sem_wait(&sig->mutex);
    sig->cnt--;
    if(sig->cnt==0) sem_post(&sig->lock);
    sem_post(&sig->mutex);
    return data;
}

void writeCache(char *data,URL *url)
{
    sem_wait(&sig->lock);
    int i;
    for(i=0;i<CACHE_SIZE;i++)
        if(c[i].lru==0) break;
    if(i==CACHE_SIZE)
    {
        int min=c[0].lru;
        for(int j=1;j<CACHE_SIZE;j++)
        {
            if(c[j].lru<min)
            {
                min=c[j].lru;
                i=j;
            }
        }
    }
    c[i].lru=maxlru()+1;
    urlCopy(&c[i].url,url);
    strcpy(c[i].data,data);
    sem_post(&sig->lock);
    return;
}

void parseUrl(char *s,URL *url)
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

void readClient(rio_t *rio,URL *url,char *data)
{
    char host[MAXLINE];
    char line[MAXLINE];
    char other[MAXLINE];
    char method[MAXLINE],str[MAXLINE],version[MAXLINE];

    Rio_readlineb(rio,line,MAXLINE);
    sscanf(line,"%s %s %s\n",method,str,version);
    parseUrl(str,url);

    sprintf(host,"Host: %s\r\n",url->host);
    while(Rio_readlineb(rio,line,MAXLINE)>0)
    {
        if(strcmp(line,"\r\n")==0) break;
        if(strncmp(line,"Host",4)==0) strcpy(host,line);
        if(strncmp(line,"User-Agent",10)&&
           strncmp(line,"Connection",10)&&
           strncmp(line,"Proxy-Connection",16)) strcat(other,line);
    }
    
    sprintf(data,"%s %s HTTP/1.0\r\n"
                  "%s%s"
                  "Connection: close\r\n"
                  "Proxy-Connection: close\r\n"
                  "%s\r\n",method,url->path,host,user_agent_hdr,other);
}

void doit(int connfd)
{
    rio_t rio;
    char line[MAXLINE];
    char data[MAXLINE];
    size_t n,sum;
    Rio_readinitb(&rio,connfd);
    URL url;
    readClient(&rio,&url,data);
    char *x=readCache(&url);
    if(x!=NULL)
    {
        Rio_writen(connfd, x, strlen(x));
        Free(x);
        return;
    }
    int serverfd=open_clientfd(url.host,url.port);
    rio_readinitb(&rio,serverfd);
    Rio_writen(serverfd,data,strlen(data));
    char cache[MAX_OBJECT_SIZE], *cp = cache;
    n=Rio_readlineb(&rio,line,MAXLINE);
    while(n>0)
    {
        Rio_writen(connfd,line,n);
        sum+=n;
        if(sum<MAX_OBJECT_SIZE)
        {
            strcpy(cp,line);
            cp+=n;
        }
        n=Rio_readlineb(&rio,line,MAXLINE);
    }
    writeCache(data,&url);
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

void lockInit()
{
    sig->cnt=0;
    sem_init(&sig->mutex,0,1);
    sem_init(&sig->lock,0,1);
}


int main(int argc,char **argv) 
{
    int listenfd;
    int *connfd;
    pthread_t tid;
    char host[MAXLINE],serv[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    sig=Malloc(sizeof(struct Lock));
    lockInit();
    listenfd=Open_listenfd(argv[1]);
    while(1) 
    {
        clientlen=sizeof(clientaddr);
        connfd=Malloc(sizeof(int));
        *connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen); 
        Getnameinfo((SA*)&clientaddr,clientlen,host,MAXLINE,serv,MAXLINE,0);
        Pthread_create(&tid,NULL,thread,connfd);
    }
    return 0;
}

