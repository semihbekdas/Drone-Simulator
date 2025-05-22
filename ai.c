#include "headers/globals.h"
#include "headers/ai.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/list.h"
#include "headers/coord.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // sleep, send için
#include <pthread.h>
#include <string.h> // strlen için
#include <sys/socket.h> // send için (doğrudan kullanılıyorsa)
#include <json.h> // JSON işlemleri için

// find_closest_idle_drone fonksiyonu önceki mesajdaki gibi kalacak.
static Drone *find_closest_idle_drone(Coord target_survivor_coord) {
    Drone *closest_drone_found = NULL;
    int min_distance = INT_MAX;

    pthread_mutex_lock(&drones->lock);

    Node *current_node = drones->head;
    while (current_node != NULL) {
        Drone *current_drone = *(Drone**)current_node->data;
        if (current_drone) {
            pthread_mutex_lock(&current_drone->lock);
            if (current_drone->status == IDLE) {
                int dist = abs(current_drone->coord.x - target_survivor_coord.x) +
                           abs(current_drone->coord.y - target_survivor_coord.y);
                if (dist < min_distance) {
                    min_distance = dist;
                    closest_drone_found = current_drone;
                }
            }
            pthread_mutex_unlock(&current_drone->lock);
        }
        current_node = current_node->next;
    }
    pthread_mutex_unlock(&drones->lock);

    return closest_drone_found;
}


void *ai_controller(void *arg) {
    (void)arg;
    printf("AI controller thread started.\n");

    while (1) {
        Survivor *survivor_to_help = NULL;

        pthread_mutex_lock(&survivors->lock); 
        Node *current_survivor_node = survivors->tail; 
        while (current_survivor_node != NULL) {
            Survivor *s = *(Survivor**)current_survivor_node->data; 
            if (s && s->status == WAITING) {
                survivor_to_help = s;
                survivor_to_help->status = ASSIGNED; 
                printf("[AI] Oldest Survivor %s at (%d,%d) status set to ASSIGNED.\n",
                       survivor_to_help->info, survivor_to_help->coord.x, survivor_to_help->coord.y);
                break; 
            }
            current_survivor_node = current_survivor_node->prev; 
        }
        pthread_mutex_unlock(&survivors->lock);

        if (survivor_to_help) {
            Drone *assigned_drone = find_closest_idle_drone(survivor_to_help->coord);

            if (assigned_drone) {
                pthread_mutex_lock(&assigned_drone->lock);

                assigned_drone->target = survivor_to_help->coord;
                assigned_drone->status = ON_MISSION; 
                assigned_drone->current_survivor_target = survivor_to_help;

                printf("[AI] Assigning Drone %d to Survivor %s at (%d,%d). Sending ASSIGN_MISSION msg.\n",
                       assigned_drone->id, survivor_to_help->info,
                       survivor_to_help->coord.x, survivor_to_help->coord.y);

                struct json_object *mission_msg = json_object_new_object();
                json_object_object_add(mission_msg, "type", json_object_new_string("ASSIGN_MISSION"));
                char mission_id_str[32]; 
                snprintf(mission_id_str, sizeof(mission_id_str), "M%d-%ldS%s", assigned_drone->id, time(NULL) % 10000, survivor_to_help->info);
                json_object_object_add(mission_msg, "mission_id", json_object_new_string(mission_id_str));
                json_object_object_add(mission_msg, "priority", json_object_new_string("high")); 
                
                struct json_object *target_coord_obj = json_object_new_object();
                json_object_object_add(target_coord_obj, "x", json_object_new_int(survivor_to_help->coord.x));
                json_object_object_add(target_coord_obj, "y", json_object_new_int(survivor_to_help->coord.y));
                json_object_object_add(mission_msg, "target", target_coord_obj);
                
                if (assigned_drone->socket_fd > 0) { 
                    const char *json_string = json_object_to_json_string_ext(mission_msg, JSON_C_TO_STRING_PLAIN);
                    if (json_string) {
                        {
                            /* Append newline so drone client can parse ASSIGN_MISSION */
                            char msg_nl[strlen(json_string) + 2];
                            snprintf(msg_nl, sizeof(msg_nl), "%s\n", json_string);
                            if (send(assigned_drone->socket_fd, msg_nl, strlen(msg_nl), 0) < 0) {
                                perror("[AI] Failed to send ASSIGN_MISSION to drone");
                                // Görev iptal, survivor'ı WAITING yap, drone'u IDLE yap.
                                // Bu işlemler için ilgili lock'lar alınmalı.
                                // Şimdilik basitleştirilmiş:
                                assigned_drone->status = IDLE; 
                                assigned_drone->current_survivor_target = NULL;
                                // Survivor'ın durumunu da WAITING'e geri almak lazım (survivors->lock altında)
                                pthread_mutex_lock(&survivors->lock);
                                if(survivor_to_help->status == ASSIGNED) survivor_to_help->status = WAITING;
                                pthread_mutex_unlock(&survivors->lock);
                            } else {
                                 printf("[AI] ASSIGN_MISSION sent to Drone %d for survivor %s.\n", assigned_drone->id, survivor_to_help->info);
                            }
                        }
                    } else {
                        fprintf(stderr, "[AI] Failed to stringify ASSIGN_MISSION JSON for Drone %d\n", assigned_drone->id);
                        assigned_drone->status = IDLE; 
                        assigned_drone->current_survivor_target = NULL;
                        pthread_mutex_lock(&survivors->lock);
                        if(survivor_to_help->status == ASSIGNED) survivor_to_help->status = WAITING;
                        pthread_mutex_unlock(&survivors->lock);
                    }
                } else {
                    fprintf(stderr, "[AI] Drone %d has invalid socket_fd, cannot send ASSIGN_MISSION.\n", assigned_drone->id);
                    assigned_drone->status = IDLE; 
                    assigned_drone->current_survivor_target = NULL;
                    pthread_mutex_lock(&survivors->lock);
                    if(survivor_to_help->status == ASSIGNED) survivor_to_help->status = WAITING;
                    pthread_mutex_unlock(&survivors->lock);
                }
                json_object_put(mission_msg); 
                
                // pthread_cond_signal(&assigned_drone->cond); // Client mesajla uyarıldı, bu gereksiz olabilir.
                
                pthread_mutex_unlock(&assigned_drone->lock);
            } else {
                printf("[AI] No idle drone found for survivor %s. Setting status back to WAITING.\n", survivor_to_help->info);
                pthread_mutex_lock(&survivors->lock); 
                if(survivor_to_help->status == ASSIGNED) { 
                    survivor_to_help->status = WAITING;
                }
                pthread_mutex_unlock(&survivors->lock);
            }
        }
        sleep(1); 
    }
    return NULL;
}