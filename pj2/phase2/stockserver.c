/* 
 * echoservert_pre.c - A prethreaded concurrent echo server
 */
/* $begin echoservertpremain */
#include "csapp.h"
#define NTHREADS  4
#define SBUFSIZE  16

typedef struct {
    int* buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;

sbuf_t sbuf; /* Shared buffer of connected descriptors */

typedef struct _item {
    int id;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex_readcnt;
    sem_t mutex_stock;
    struct _item* left;
    struct _item* right;
}item;

item* root = NULL;
int byte_cnt = 0;
/*
struct timeval start, end;
int flag = 0;
*/
int count = 0;

item* insert(item* node, int id, int left, int price);
item* search_node(int id);
void sbuf_init(sbuf_t* sp, int n);
void sbuf_deinit(sbuf_t* sp);
void sbuf_insert(sbuf_t* sp, int item);
int sbuf_remove(sbuf_t* sp);
void make_line(item* node, char* templine);
void print_node(FILE* fp, item* node);
void check_clients(int connfd);
void *thread(void *vargp);


int main(int argc, char **argv) 
{
    int i, listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;
    int tmp_id, tmp_left, tmp_price;
    
    FILE* fp;

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    fp = Fopen("stock.txt", "r");
    while (fscanf(fp, "%d %d %d\n", &tmp_id, &tmp_left, &tmp_price) != EOF) {
        root = insert(root, tmp_id, tmp_left, tmp_price);
    }
    Fclose(fp);

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE); //line:conc:pre:initsbuf
    
    for (i = 0; i < NTHREADS; i++)  /* Create worker threads */ //line:conc:pre:begincreate
	Pthread_create(&tid, NULL, thread, NULL);               //line:conc:pre:endcreate

    while (1) { 
        clientlen = sizeof(struct sockaddr_storage);
	    connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        /*
        if (flag == 0) {
            gettimeofday(&start, 0);
        }
        flag = 1;
        */
        Getnameinfo((SA*)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    }
}

void *thread(void *vargp) 
{  
    //unsigned long e_usec;

    Pthread_detach(pthread_self());
    while (1) { 
	    int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ //line:conc:pre:removeconnfd
        byte_cnt = 0;
        check_clients(connfd);
	    Close(connfd);

        count++;
        if (count == sbuf.rear) {
            FILE* fp;

            /*
            gettimeofday(&end, 0);
            e_usec = ((end.tv_sec * 1000000) + end.tv_usec) - ((start.tv_sec * 1000000) + start.tv_usec);
            printf("elapsed time : %lu\n", e_usec);
            */
            fp = Fopen("stock.txt", "w");
            print_node(fp, root);
            fclose(fp);
            count = 0;
            //flag = 0;
        }
    }
}
/* $end echoservertpremain */

void sbuf_init(sbuf_t* sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t* sp) {
    Free(sp->buf);
}

void sbuf_insert(sbuf_t* sp, int item) {
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear) % (sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t* sp) {
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front) % (sp->n)];
    V(&sp->mutex);
    V(&sp->slots);

    return item;
}

item* insert(item* node, int id, int left, int price) {

    if (node == NULL) {
        node = (item*)malloc(sizeof(item));
        node->left = NULL;
        node->right = NULL;
        node->id = id;
        node->left_stock = left;
        node->price = price;
        node->readcnt = 0;
        Sem_init(&node->mutex_readcnt, 0, 1);
        Sem_init(&node->mutex_stock, 0, 1);
        return node;
    }
    else {
        if (id < node->id) {
            node->left = insert(node->left, id, left, price);
        }
        else {
            node->right = insert(node->right, id, left, price);
        }
    }

    return node;
}

item* search_node(int id) {
    item* temp;

    temp = root;
    while (temp != NULL) {
        if (id == temp->id) {
            return temp;
        }
        else if (id < temp->id) {
            temp = temp->left;
        }
        else {
            temp = temp->right;
        }
    }

    return NULL;
}

void make_line(item* node, char* templine) {
    item* temp;
    char str[50];

    temp = node;
    if (temp == NULL) {
        return;
    }
    P(&temp->mutex_readcnt);
    temp->readcnt++;
    if (temp->readcnt >= 1) {
        P(&temp->mutex_stock);
    }
    V(&temp->mutex_readcnt);

    sprintf(str, "%d %d %d\n", node->id, node->left_stock, node->price);
    strcat(templine, str);

    P(&temp->mutex_readcnt);
    temp->readcnt--;
    if (temp->readcnt == 0) {
        V(&temp->mutex_stock);
    }
    V(&temp->mutex_readcnt);

    make_line(temp->left, templine);
    make_line(temp->right, templine);
}

void print_node(FILE* fp, item* node) {
    item* temp;

    temp = node;
    if (temp == NULL) {
        return;
    }
    fprintf(fp, "%d %d %d\n", temp->id, temp->left_stock, temp->price);

    print_node(fp, temp->left);
    print_node(fp, temp->right);
}
void check_clients(int connfd) {
    int n;
    char buf[MAXLINE];
    rio_t rio;
    sbuf_t* sp;

    sp = &sbuf;
    Rio_readinitb(&rio, connfd);
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        P(&sp->mutex);
        byte_cnt += n;
        printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);
        V(&sp->mutex);

        if (!strncmp(buf, "buy", 3)) {
            int buy_id, buy_num;
            item* trail1;

            P(&sp->mutex);
            for (int j = 0; j < 3; j++) {
                buf[j] = ' ';
            }
            buf[n - 1] = '\0';
            sscanf(buf, "%d %d", &buy_id, &buy_num);
            trail1 = search_node(buy_id);
            if (trail1 == NULL) {
                strcpy(buf, "There is no such stock ID!\n");
                Rio_writen(connfd, buf, MAXLINE);
            }
            else if (trail1->left_stock < buy_num) {
                strcpy(buf, "Not enough left stock\n");
                Rio_writen(connfd, buf, MAXLINE);
            }
            else {
                P(&trail1->mutex_stock);
                trail1->left_stock -= buy_num;
                strcpy(buf, "[buy] success\n");
                Rio_writen(connfd, buf, MAXLINE);
                V(&trail1->mutex_stock);
            }
            V(&sp->mutex);
        }
        else if (!strncmp(buf, "sell", 4)) {
            int sell_id, sell_num;
            item* trail2;

            P(&sp->mutex);
            for (int j = 0; j < 4; j++) {
                buf[j] = ' ';
            }
            buf[n - 1] = '\0';
            sscanf(buf, "%d %d", &sell_id, &sell_num);
            trail2 = search_node(sell_id);
            if (trail2 == NULL) {
                strcpy(buf, "There is no such stock ID!\n");
                Rio_writen(connfd, buf, MAXLINE);
            }
            else {
                P(&trail2->mutex_stock);
                trail2->left_stock += sell_num;
                strcpy(buf, "[sell] success\n");
                Rio_writen(connfd, buf, MAXLINE);
                V(&trail2->mutex_stock);
            }
            V(&sp->mutex);
        }
        else if (!strncmp(buf, "show", 4)) {
            char templine[MAXLINE];

            P(&sp->mutex);
            for (int j = 0; j < MAXLINE; j++) {
                templine[j] = '\0';
            }
            make_line(root, templine);
            strcpy(buf, templine);
            Rio_writen(connfd, buf, MAXLINE);
            V(&sp->mutex);
        }
        else {
            Rio_writen(connfd, buf, MAXLINE);
        }
        byte_cnt = 0;
    }
 }