#ifndef LIST_H
#define LIST_H

#include <time.h>
#include <pthread.h>
#include <stddef.h> // size_t için

typedef struct node {
    struct node *prev;
    struct node *next;
    char occupied;  // Bu alan free_list mantığı ile gereksiz hale gelebilir ama şimdilik kalsın
    char data[];
} Node;

typedef struct list {
    /* Doubly-linked list stored in a contiguous node array */
    Node *head;
    Node *tail;
    int number_of_elements;    /* Current element count */
    int capacity;              /* Maximum elements */
    int datasize;              /* Size of each data block */
    int nodesize;              /* sizeof(Node) + datasize */
    char *startaddress;        /* Array base pointer */
    char *endaddress;          /* Array end pointer */
    Node *lastprocessed;       /* Bu alan free_list ile gereksiz hale gelebilir */
    Node *free_list;           /* Kullanılmayan node'ların bağlı listesi */

    /* Synchronization primitives */
    pthread_mutex_t lock;      /* Mutex for all list operations */
    pthread_cond_t not_empty;  /* Wait when list is empty */
    pthread_cond_t not_full;   /* Wait when list is full */

    /* Operations */
    Node *(*add)(struct list *list, void *data);
    int  (*removedata)(struct list *list, void *data);
    int  (*removenode)(struct list *list, Node *node);
    void *(*pop)(struct list *list, void *dest);
    void *(*peek)(struct list *list);
    void (*destroy)(struct list *list);
    void (*printlist)(struct list *list, void (*print)(void *));
    void (*printlistfromtail)(struct list *list, void (*print)(void *));

    struct list *self;
} List;

/* Create a new list with each element sized datasize and capacity */
List *create_list(size_t datasize, int capacity);

/* Basic list operations (prototipleri burada kalsın, implementasyonları list.c'de olacak) */
/* Bu prototipler zaten global olduğu için list->add = add gibi atamalarla struct içinde tutuluyor.
   Ayrıca global fonksiyon olarak da tanımlanabilirler ya da sadece struct içinden çağrılabilirler.
   Mevcut yapıda hem global hem struct içinde gibi duruyor. Tutarlılık açısından sadece struct üzerinden
   erişim daha iyi olabilir, ama mevcut yapıyı çok değiştirmeyelim şimdilik.
*/
Node *add(List *list, void *data);
int removedata(List *list, void *data);
int removenode(List *list, Node *node);
void *pop(List *list, void *dest);
void *peek(List *list);
void destroy(List *list);
void printlist(List *list, void (*print)(void *));
void printlistfromtail(List *list, void (*print)(void *));

#endif /* LIST_H */