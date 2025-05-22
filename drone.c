#include "headers/drone.h"
#include "headers/globals.h" 
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <time.h>   

/**
 * @brief Creates and initializes a new Drone instance for a connected client.
 * @param drone_id_numeric The numeric part of the drone ID.
 * @param drone_id_string The full string ID of the drone (e.g., "D1").
 * @param socket_fd The socket file descriptor for communication with this drone.
 * @return Pointer to the newly created Drone object, or NULL on failure.
 */
Drone* server_create_drone_instance(int drone_id_numeric, const char* drone_id_string, int socket_fd) {
    Drone *d = (Drone*)malloc(sizeof(Drone));
    if (!d) {
        perror("Failed to allocate memory for Drone instance");
        return NULL;
    }

    d->id = drone_id_numeric;
    strncpy(d->id_str, drone_id_string, sizeof(d->id_str) - 1); // Kopyala
    d->id_str[sizeof(d->id_str) - 1] = '\0'; // Null terminate

    d->socket_fd = socket_fd;
    d->status = IDLE; 

    if (map.height > 0 && map.width > 0) {
         d->coord = (Coord){ rand() % map.height, rand() % map.width };
    } else {
         d->coord = (Coord){0,0}; 
    }
    d->target = d->coord; 
    d->current_survivor_target = NULL;
    d->last_heartbeat_time = time(NULL); 
    memset(d->drone_capabilities, 0, sizeof(d->drone_capabilities));

    if (pthread_mutex_init(&d->lock, NULL) != 0) {
        perror("Failed to initialize drone instance mutex");
        free(d);
        return NULL;
    }
    if (pthread_cond_init(&d->cond, NULL) != 0) {
        perror("Failed to initialize drone instance condition variable");
        pthread_mutex_destroy(&d->lock);
        free(d);
        return NULL;
    }

    printf("Server: Created new drone instance for ID %s (Numeric: %d) on socket %d.\n",
           d->id_str, d->id, socket_fd);
    return d;
}

void server_cleanup_drone_instance(Drone *d) {
    if (!d) return;
    printf("Server: Cleaning up drone instance for ID %s (socket %d).\n", d->id_str, d->socket_fd);
    pthread_cond_destroy(&d->cond);
    pthread_mutex_destroy(&d->lock);
    free(d);
}