/*
 * list.c
 * Thread-safe doubly-linked list in a contiguous memory array with a free list.
 */
#include "headers/list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for static helper functions if any (şu an yok)
// static Node *find_memcell_fornode(List *list); // Artık free_list kullanacağı için ismi değişebilir veya doğrudan implemente edilebilir.

/**
 * @brief Create a list object, allocates new memory for list, and sets its data members.
 *        Initializes a free list for node reuse.
 */
List *create_list(size_t datasize, int capacity) {
    List *list = malloc(sizeof(List));
    if (!list) {
        perror("Failed to allocate memory for list_t");
        return NULL;
    }
    // memset(list, 0, sizeof(List)); // Gerekli değil, aşağıda tüm alanlar atanıyor.

    list->datasize = datasize;
    list->nodesize = sizeof(Node) + datasize; // Node başlığı + veri alanı

    list->startaddress = malloc(list->nodesize * capacity);
    if (!list->startaddress) {
        perror("Failed to allocate memory for list nodes");
        free(list);
        return NULL;
    }
    // memset(list->startaddress, 0, list->nodesize * capacity); // Gerekli değil, free_list oluşturulurken node'lar hazırlanacak.

    list->capacity = capacity;
    list->number_of_elements = 0;
    list->head = NULL;
    list->tail = NULL;
    list->lastprocessed = NULL; // Bu alan artık free_list ile pek kullanılmayacak

    /* Initialize free list: Tüm node'ları free_list'e bağla */
    list->free_list = NULL;
    for (int i = 0; i < capacity; i++) {
        // Node'u bitişik bellek bloğundan al
        Node *current_node = (Node *)(list->startaddress + i * list->nodesize);
        current_node->occupied = 0; // Başlangıçta boş
        current_node->prev = NULL;  // Free list'te prev'e gerek yok
        current_node->next = list->free_list; // Mevcut free_list başına ekle
        list->free_list = current_node;
    }
    list->endaddress = list->startaddress + (list->nodesize * capacity); // Sadece bilgi amaçlı

    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&list->lock, NULL) != 0) {
        perror("Failed to initialize list mutex");
        free(list->startaddress);
        free(list);
        return NULL;
    }
    if (pthread_cond_init(&list->not_empty, NULL) != 0) {
        perror("Failed to initialize list not_empty condition variable");
        pthread_mutex_destroy(&list->lock);
        free(list->startaddress);
        free(list);
        return NULL;
    }
    if (pthread_cond_init(&list->not_full, NULL) != 0) {
        perror("Failed to initialize list not_full condition variable");
        pthread_cond_destroy(&list->not_empty);
        pthread_mutex_destroy(&list->lock);
        free(list->startaddress);
        free(list);
        return NULL;
    }

    /* Assign operations */
    list->self = list; // Kendi kendini işaret etmesi gereksiz olabilir, kaldırılabilir.
    list->add = add;
    list->removedata = removedata;
    list->removenode = removenode;
    list->pop = pop;
    list->peek = peek;
    list->destroy = destroy;
    list->printlist = printlist;
    list->printlistfromtail = printlistfromtail;

    return list;
}

/**
 * @brief Deallocates all memory associated with the list.
 *        Does NOT free the data pointed to by nodes if they are pointers.
 */
void destroy(List *list) {
    if (!list) return;

    // Senkronizasyon kaynaklarını yok et
    pthread_cond_destroy(&list->not_full);
    pthread_cond_destroy(&list->not_empty);
    pthread_mutex_destroy(&list->lock);

    // Node'lar için ayrılan bitişik bellek alanını serbest bırak
    if (list->startaddress) {
        free(list->startaddress);
        list->startaddress = NULL;
    }

    // Listenin kendisini serbest bırak
    // memset(list, 0, sizeof(List)); // free'den önce memset gereksiz.
    free(list);
}

/**
 * @brief Get an unoccupied node from the free list.
 * @return Pointer to a node, or NULL if free list is empty (should not happen if not_full condition is used).
 */
static Node *get_node_from_freelist(List *list) {
    if (!list->free_list) {
        // Bu durum, not_full condition variable doğru çalışıyorsa olmamalı.
        // Eğer olursa, bir mantık hatası var demektir.
        fprintf(stderr, "Error: Free list is empty when trying to get a node!\n");
        return NULL;
    }
    Node *node = list->free_list;
    list->free_list = node->next; // Bir sonraki boş node'a ilerle
    
    node->next = NULL; // Yeni node için hazırla
    node->prev = NULL;
    node->occupied = 1; // Artık meşgul
    return node;
}

/**
 * @brief Return a node to the free list.
 */
static void return_node_to_freelist(List *list, Node *node) {
    if (!node) return;
    
    // memset(node->data, 0, list->datasize); // İsteğe bağlı: veriyi temizle
    node->occupied = 0;
    node->prev = NULL; // free list'te prev'e gerek yok
    node->next = list->free_list;
    list->free_list = node;
}

Node *add(List *list, void *data) {
    if (!list || !data) return NULL; // Temel kontrol

    pthread_mutex_lock(&list->lock);

    /* Wait if list is full */
    while (list->number_of_elements >= list->capacity) {
        // printf("List is full. Waiting...\n"); // Debug
        if (pthread_cond_wait(&list->not_full, &list->lock) != 0) {
            perror("pthread_cond_wait for not_full failed");
            pthread_mutex_unlock(&list->lock);
            return NULL; // Hata durumu
        }
    }

    Node *node = get_node_from_freelist(list);
    if (!node) { // Teorik olarak olmamalı (yukarıdaki while döngüsü sayesinde)
        pthread_mutex_unlock(&list->lock);
        fprintf(stderr, "Failed to get node from freelist even after waiting.\n");
        return NULL;
    }

    memcpy(node->data, data, list->datasize); // Veriyi kopyala

    /* Insert at head */
    node->prev = NULL;
    node->next = list->head;
    if (list->head) {
        list->head->prev = node;
    } else {
        // Liste boştu, bu ilk eleman aynı zamanda tail olacak
        list->tail = node;
    }
    list->head = node;
    // list->lastprocessed = node; // Bu alan free_list ile önemini yitirdi.
    list->number_of_elements++;

    /* Signal that list is not empty anymore */
    pthread_cond_signal(&list->not_empty);

    pthread_mutex_unlock(&list->lock);
    return node; // Eklenen node'u döndür
}

/**
 * @brief Removes the first occurrence of data from the list.
 * @param list The list.
 * @param data Pointer to the data to be matched and removed.
 *             The content at this pointer is compared with node data.
 * @return 0 on success, 1 if data not found or error.
 */
int removedata(List *list, void *data_to_match) {
    if (!list || !data_to_match) return 1;

    pthread_mutex_lock(&list->lock);

    Node *current = list->head;
    while (current != NULL) {
        // Veriyi karşılaştır (datasize kadar byte)
        if (memcmp(current->data, data_to_match, list->datasize) == 0) {
            // Eşleşme bulundu, bu node'u çıkar
            if (current->prev) {
                current->prev->next = current->next;
            } else { // Head çıkarılıyor
                list->head = current->next;
            }
            if (current->next) {
                current->next->prev = current->prev;
            } else { // Tail çıkarılıyor
                list->tail = current->prev;
            }

            list->number_of_elements--;
            return_node_to_freelist(list, current);

            /* Signal that list is not full anymore */
            pthread_cond_signal(&list->not_full);
            pthread_mutex_unlock(&list->lock);
            return 0; // Başarılı
        }
        current = current->next;
    }

    pthread_mutex_unlock(&list->lock);
    return 1; // Veri bulunamadı
}

/**
 * @brief Removes a specific node from the list.
 * @return 0 on success, 1 if node is NULL or not found (bu implementasyon node'un listede olduğunu varsayar).
 */
int removenode(List *list, Node *node_to_remove) {
    if (!list || !node_to_remove) return 1;

    pthread_mutex_lock(&list->lock);

    // Node'un listede olup olmadığını kontrol etmek maliyetli olabilir.
    // Bu fonksiyon genellikle node'a zaten sahip olunduğunda çağrılır.
    // Eğer node'un listede olup olmadığını doğrulamak gerekiyorsa, ek kontrol gerekir.
    // Şimdilik, node'un geçerli ve listede olduğunu varsayıyoruz.

    if (node_to_remove->prev) {
        node_to_remove->prev->next = node_to_remove->next;
    } else { // Head çıkarılıyor
        list->head = node_to_remove->next;
    }
    if (node_to_remove->next) {
        node_to_remove->next->prev = node_to_remove->prev;
    } else { // Tail çıkarılıyor
        list->tail = node_to_remove->prev;
    }

    list->number_of_elements--;
    return_node_to_freelist(list, node_to_remove);

    /* Signal that list is not full anymore */
    pthread_cond_signal(&list->not_full);
    pthread_mutex_unlock(&list->lock);
    return 0; // Başarılı
}


/**
 * @brief Removes and returns the data from the head of the list.
 * @param list The list.
 * @param dest Pointer to a buffer where the popped data will be copied.
 *             If NULL, data is not copied, but the node is still removed.
 * @return Pointer to dest if successful and dest is not NULL,
 *         (void*)1 (veya başka bir non-NULL değer) if successful and dest is NULL,
 *         NULL on error or if list was empty (ama cond_wait ile beklenir).
 */
void *pop(List *list, void *dest) {
    if (!list) return NULL;

    pthread_mutex_lock(&list->lock);

    /* Wait if list is empty */
    while (list->number_of_elements == 0) {
        // printf("List is empty. Waiting for pop...\n"); // Debug
        if (pthread_cond_wait(&list->not_empty, &list->lock) != 0) {
            perror("pthread_cond_wait for not_empty failed");
            pthread_mutex_unlock(&list->lock);
            return NULL; // Hata durumu
        }
    }

    Node *node_to_pop = list->head; // Baştaki elemanı al

    if (dest) { // Eğer dest buffer'ı verilmişse, veriyi kopyala
        memcpy(dest, node_to_pop->data, list->datasize);
    }

    // Listeden çıkar (removenode mantığının bir kısmı)
    list->head = node_to_pop->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        // Liste boşaldı
        list->tail = NULL;
    }

    list->number_of_elements--;
    return_node_to_freelist(list, node_to_pop);

    /* Signal that list is not full anymore */
    pthread_cond_signal(&list->not_full);
    pthread_mutex_unlock(&list->lock);

    return dest ? dest : (void*)1; // dest NULL ise, sadece başarılı olduğunu belirtmek için non-NULL bir şey döndür.
}

/**
 * @brief Returns a pointer to the data at the head of the list without removing it.
 *        WARNING: The returned pointer is to the internal data. Do not free it.
 *                 It is valid only as long as the list is locked and the node is not removed.
 *                 For safer access, consider a version that copies data to a user-provided buffer.
 * @return Pointer to data, or NULL if list is empty (ama cond_wait ile beklenir).
 */
void *peek(List *list) {
    if (!list) return NULL;

    pthread_mutex_lock(&list->lock);

    /* Wait if list is empty */
    while (list->number_of_elements == 0) {
        // printf("List is empty. Waiting for peek...\n"); // Debug
        if (pthread_cond_wait(&list->not_empty, &list->lock) != 0) {
            perror("pthread_cond_wait for not_empty failed");
            pthread_mutex_unlock(&list->lock);
            return NULL; // Hata durumu
        }
    }

    void *data_ptr = NULL;
    if (list->head) {
        data_ptr = list->head->data;
    }

    pthread_mutex_unlock(&list->lock);
    return data_ptr; // list->head->data'yı döndürür.
}

void printlist(List *list, void (*print_data_func)(void *)) {
    if (!list || !print_data_func) return;

    pthread_mutex_lock(&list->lock);
    printf("List (H->T): ");
    Node *temp = list->head;
    while (temp) {
        print_data_func(temp->data);
        if (temp->next) printf(" <-> ");
        temp = temp->next;
    }
    printf(" (Elements: %d)\n", list->number_of_elements);
    pthread_mutex_unlock(&list->lock);
}

void printlistfromtail(List *list, void (*print_data_func)(void *)) {
    if (!list || !print_data_func) return;

    pthread_mutex_lock(&list->lock);
    printf("List (T->H): ");
    Node *temp = list->tail;
    while (temp) {
        print_data_func(temp->data);
        if (temp->prev) printf(" <-> ");
        temp = temp->prev;
    }
    printf(" (Elements: %d)\n", list->number_of_elements);
    pthread_mutex_unlock(&list->lock);
}