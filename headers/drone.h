#ifndef DRONE_H
#define DRONE_H

#include "coord.h"
#include <pthread.h>
#include "survivor.h" 

typedef enum {
    IDLE = 0,
    ON_MISSION = 1,
} DroneState;

typedef struct drone {
    int id;                     
    char id_str[16];            // Drone ID'sinin string hali (örn: "D1") -> YENİ
    int socket_fd;              
    DroneState status;          
    Coord coord;                
    Coord target;               
    pthread_mutex_t lock;       
    pthread_cond_t cond;        
    Survivor *current_survivor_target; 

    time_t last_heartbeat_time; 
    char drone_capabilities[128]; 

} Drone;

Drone* server_create_drone_instance(int drone_id_numeric, const char* drone_id_string, int socket_fd); // Prototip güncellendi
void server_cleanup_drone_instance(Drone *d);

#endif