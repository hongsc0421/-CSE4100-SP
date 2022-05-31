/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

typedef struct {
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
}pool;

typedef struct _item {
    int id;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex;
    struct _item* left;
    struct _item* right;
}item;

item* root = NULL;
int byte_cnt = 0;
//struct timeval start, end;

item* insert(item* node, int id, int left, int price);
item* search_node(int id);
void make_line(item* node, char* templine);
void print_node(FILE* fp, item* node);
void init_pool(int listenfd, pool* p);
void add_client(int connfd, pool* p);
void check_clients(pool* p);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    static pool pool;
    FILE* fp;
    int tmp_id, tmp_left, tmp_price;
    int k; 
    //int flag = 0;

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
    init_pool(listenfd, &pool);

    while (1) {
        /* Wait for listening/connected descriptor(s) to become ready */
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);

        /* If listening descriptor ready, add new client to pool */
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
            /*if (flag == 0) {
                gettimeofday(&start, 0);
            }
            flag = 1;
            */
            add_client(connfd, &pool);
            Getnameinfo((SA*)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
        }
	    
        /* Echo a text line from each ready connected descriptor */
        check_clients(&pool);
        for (k = 0; k < FD_SETSIZE; k++) {
            if (pool.clientfd[k] != -1) break;
        }
        if (k == FD_SETSIZE) {
            FILE* fp;
            /*unsigned long e_usec;

            gettimeofday(&end, 0);
            e_usec = ((end.tv_sec * 1000000) + end.tv_usec) - ((start.tv_sec * 1000000) + start.tv_usec);
            printf("elapsed time : %lu\n", e_usec);
            */
            fp = Fopen("stock.txt", "w");
            print_node(fp, root);
            fclose(fp);
        }
    }
    exit(0);
}

void init_pool(int listenfd, pool* p) {
    /* Initially, there are no connected descriptors */
    int i;

    p->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++) {
        p->clientfd[i] = -1;
    }

    /* Initially, listenfd is only member of select read set */
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool* p) {
    int i;

    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++) {   /* Find an available slot */
        if (p->clientfd[i] < 0) {
            /* Add connected descriptor to the pool */
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);

            /* Add the descripter to descriptor set */
            FD_SET(connfd, &p->read_set);

            /* Update max descriptor and pool high water mark */
            if (connfd > p->maxfd) {
                p->maxfd = connfd;
            }
            if (i > p->maxi) {
                p->maxi = i;
            }
            break;
        }
    }
    if (i == FD_SETSIZE) { /* Couldn't find an empty slot*/
        app_error("add_client error : Too many clients");
    }
}

void check_clients(pool* p) {
    int i, connfd, n;
    char buf[MAXLINE];
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        /* If the descriptor is ready, echo a text line from it */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
            p->nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
                byte_cnt += n;
                printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);
                
                if (!strncmp(buf, "buy", 3)) {
                    int buy_id, buy_num;
                    item* trail1;

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
                        trail1->left_stock -= buy_num;
                        strcpy(buf, "[buy] success\n");
                        Rio_writen(connfd, buf, MAXLINE);
                    }
                }
                else if (!strncmp(buf, "sell", 4)) {
                    int sell_id, sell_num;
                    item* trail2;

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
                        trail2->left_stock += sell_num;
                        strcpy(buf, "[sell] success\n");
                        Rio_writen(connfd, buf, MAXLINE);
                    }
                }
                else if (!strncmp(buf, "show", 4)) {
                    char templine[MAXLINE];

                    for (int j = 0; j < MAXLINE; j++) {
                        templine[j] = '\0';
                    }
                    make_line(root, templine);
                    strcpy(buf, templine);
                    Rio_writen(connfd, buf, MAXLINE);
                }
                else {
                    Rio_writen(connfd, buf, MAXLINE);
                }
            }

            /* EOF detected, remove descriptor from pool */
            else {
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }
        }
    }
}
/* $end echoserverimain */
item* insert(item* node, int id, int left, int price) {
    
    if (node == NULL) {
        node = (item*)malloc(sizeof(item));
        node->left = NULL;
        node->right = NULL;
        node->id = id;
        node->left_stock = left;
        node->price = price;
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
    sprintf(str, "%d %d %d\n", node->id, node->left_stock, node->price);
    strcat(templine, str);

    make_line(temp->left, templine);
    make_line(temp->right, templine);
}

void print_node(FILE* fp, item* node){
    item* temp;

    temp = node;
    if (temp == NULL) {
        return;
    }
    fprintf(fp, "%d %d %d\n", temp->id, temp->left_stock, temp->price);
    
    print_node(fp, temp->left);
    print_node(fp, temp->right);
}