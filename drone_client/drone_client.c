// drone_client.c (Dosyanın tamamı, ilgili yerler güncellendi)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // TCP_NODELAY için
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h> 
#include <json.h>
#include <errno.h> 
#include <math.h> // Matematiksel işlemler için

#include "../headers/coord.h" 
#include "../headers/drone.h" // Sadece DroneState enum'u için

#define SERVER_IP "127.0.0.1" 
#define SERVER_PORT 8080      
#define BUFFER_SIZE 1024
#define RECV_AGGREGATE_BUFFER_SIZE_CLIENT (BUFFER_SIZE * 4)
#define DRONE_ID_PREFIX "D"  

// Harita boyutları - sunucudaki map.width ve map.height ile uyumlu olmalı
#define MAP_WIDTH 50
#define MAP_HEIGHT 30

typedef struct {
    int id_numeric; 
    char drone_id_str[16]; 
    DroneState status;
    Coord current_pos;
    Coord target_pos;
    int battery_level; 
    char current_mission_id[64]; 
    int visible; // 0: görünmez, 1: görünür
    time_t mission_start_time; // Her drone için görev başlama zamanı
} ClientDroneState;


void send_json_to_server(int sock_fd, struct json_object *json_obj, const char* drone_id_for_log) {
    if (!json_obj) return;
    // Mesajın sonuna \n ekle
    const char *json_str_raw = json_object_to_json_string_ext(json_obj, JSON_C_TO_STRING_PLAIN);
    if (!json_str_raw) {
        fprintf(stderr, "Client %s: Failed to stringify JSON object.\n", drone_id_for_log);
        return;
    }
    char message_with_newline[strlen(json_str_raw) + 2];
    sprintf(message_with_newline, "%s\n", json_str_raw);

    if (send(sock_fd, message_with_newline, strlen(message_with_newline), 0) < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Client %s: send failed", drone_id_for_log);
        perror(error_msg);
    }
}

// Düzenli hareket ve doğru zamanlama için glob simulated_time ve timing_factor
#define MAX_MOVE_SPEED 1      // Her güncellemede maksimum birim hareket
#define MOVE_INTERVAL_MS 200  // Hareketler arası minimum süre (ms)

/* Drone hareketi simülasyonu - modern, yumusak ve güvenilir */
void simulate_movement(ClientDroneState *drone_state, int sock_fd) {
    static int move_counter = 0;  // Batarya tüketimi için sayaç
    static struct timespec last_move_time = {0, 0};
    
    // Zaman kontrolü - bugünkü zamanı al
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    // İlk çağrı için zamanı başlat
    if (last_move_time.tv_sec == 0 && last_move_time.tv_nsec == 0) {
        last_move_time = current_time;
        return; // İlk çağrıda sadece zamanı başlat, hareket etme
    }
    
    // İki güncelleme arasında geçen süre (ms)
    long elapsed_ms = (current_time.tv_sec - last_move_time.tv_sec) * 1000 + 
                     (current_time.tv_nsec - last_move_time.tv_nsec) / 1000000;
    
    // Hareket hızı için minimum interval kontrolü
    if (elapsed_ms < MOVE_INTERVAL_MS) {
        return; // Henüz hareket etme zamanı gelmedi
    }
    
    // Her MOVE_INTERVAL_MS milisaniye geçtiğinde hareket et (kararlı hız için)
    if (drone_state->status == ON_MISSION) {
        int moved = 0;
        
        // Hedef koordinatların geçerli olup olmadığını kontrol et
        if (drone_state->target_pos.x < 0 || drone_state->target_pos.y < 0 ||
            drone_state->target_pos.x >= MAP_WIDTH || drone_state->target_pos.y >= MAP_HEIGHT) {
            printf("Drone %s: Invalid target position (%d,%d), resetting to IDLE\n", 
                   drone_state->drone_id_str, drone_state->target_pos.x, drone_state->target_pos.y);
            drone_state->status = IDLE;
            return;
        }
        
        // Zaman aşımı kontrolü - uzun süren görevleri iptal et
        if (drone_state->status == IDLE) {
            drone_state->mission_start_time = 0;
        } else {
            if (drone_state->mission_start_time == 0) drone_state->mission_start_time = time(NULL);
            
            // 2 dakikadan uzun süren görevleri iptal et
            if (time(NULL) - drone_state->mission_start_time > 120) {
                printf("Drone %s: Mission timed out after 2 minutes, resetting to IDLE\n", drone_state->drone_id_str);
                drone_state->status = IDLE;
                drone_state->visible = 0;
                return;
            }
        }
        
        // Hedefe olan mesafeyi hesapla
        int dx = drone_state->target_pos.x - drone_state->current_pos.x;
        int dy = drone_state->target_pos.y - drone_state->current_pos.y;
        int distance = abs(dx) + abs(dy); // Manhattan mesafesi
        
        // Hedefe ulaştı mı kontrol et
        if (distance == 0) {
            printf("Drone %s: Already at target position (%d,%d). Mission complete!\n", 
                   drone_state->drone_id_str, drone_state->current_pos.x, drone_state->current_pos.y);
            drone_state->status = IDLE;
            return;
        }

        // Daha düzgün hareket için gerçek hareket mantığı 
        // (X ve Y eksenlerinde aynı anda hareket edebilir - diagonal)
        if (dx != 0) {
            // X ekseninde hareket
            int move_x = (dx > 0) ? 1 : -1; // Sağa veya sola
            drone_state->current_pos.x += move_x;
            moved = 1;
        }
        
        if (dy != 0 && !moved) {  // Eğer X'te hareket etmediyse Y'de hareket et
            // Y ekseninde hareket
            int move_y = (dy > 0) ? 1 : -1; // Aşağı veya yukarı
            drone_state->current_pos.y += move_y;
            moved = 1;
        }
        
        // Eğer hareket ettiysek, zamanlama ve durumu güncelle
        if (moved) {
            // Son hareket zamanını güncelle
            last_move_time = current_time;
            
            move_counter++;
            
            // Manhattan mesafesi = |x2-x1| + |y2-y1|
            int remaining = abs(drone_state->target_pos.x - drone_state->current_pos.x) + 
                           abs(drone_state->target_pos.y - drone_state->current_pos.y);
            
            printf("Drone %s: At (%d,%d) moving to (%d,%d) - %d steps remaining\n", 
                   drone_state->drone_id_str,
                   drone_state->current_pos.x, drone_state->current_pos.y,
                   drone_state->target_pos.x, drone_state->target_pos.y,
                   remaining);
            
            // Bataryayı her 10 harekette bir azalt
            if (move_counter % 10 == 0 && drone_state->battery_level > 0) {
                drone_state->battery_level--;
                printf("Drone %s: Battery: %d%%\n", drone_state->drone_id_str, drone_state->battery_level);
            }

            // Her adımda STATUS_UPDATE mesajı gönder
            struct json_object *status_update_msg = json_object_new_object();
            json_object_object_add(status_update_msg, "type", json_object_new_string("STATUS_UPDATE"));
            json_object_object_add(status_update_msg, "drone_id", json_object_new_string(drone_state->drone_id_str));
            json_object_object_add(status_update_msg, "timestamp", json_object_new_int64(time(NULL)));
            struct json_object *loc = json_object_new_object();
            json_object_object_add(loc, "x", json_object_new_int(drone_state->current_pos.x));
            json_object_object_add(loc, "y", json_object_new_int(drone_state->current_pos.y));
            json_object_object_add(status_update_msg, "location", loc);
            json_object_object_add(status_update_msg, "status", json_object_new_string(drone_state->status == IDLE ? "idle" : (drone_state->status == ON_MISSION ? "busy" : "unknown")));
            json_object_object_add(status_update_msg, "battery", json_object_new_int(drone_state->battery_level));
            json_object_object_add(status_update_msg, "speed", json_object_new_int(1));
            send_json_to_server(sock_fd, status_update_msg, drone_state->drone_id_str);
            json_object_put(status_update_msg);

            // Hedefe ulaştık mı kontrol et
            if (drone_state->current_pos.x == drone_state->target_pos.x &&
                drone_state->current_pos.y == drone_state->target_pos.y) {
                printf("Drone %s: Reached target! Mission (%s) complete.\n", 
                       drone_state->drone_id_str, 
                       drone_state->current_mission_id);
                drone_state->status = IDLE;
                drone_state->visible = 0; // Görünmez yap
                
                // Görev tamamlandı bilgisini sunucuya bildir
                struct json_object *mission_complete = json_object_new_object();
                json_object_object_add(mission_complete, "type", 
                                     json_object_new_string("MISSION_COMPLETE"));
                json_object_object_add(mission_complete, "drone_id", 
                                     json_object_new_string(drone_state->drone_id_str));
                json_object_object_add(mission_complete, "mission_id", 
                                     json_object_new_string(drone_state->current_mission_id));
                json_object_object_add(mission_complete, "timestamp", 
                                     json_object_new_int64(time(NULL)));
                json_object_object_add(mission_complete, "success", 
                                     json_object_new_boolean(1));
                
                // MISSION_COMPLETE mesajını gönder
                send_json_to_server(sock_fd, mission_complete, drone_state->drone_id_str);
                json_object_put(mission_complete);
            }
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <numeric_drone_id>\nExample: %s 1\n", argv[0], argv[0]);
        return 1;
    }

    ClientDroneState my_drone;
    my_drone.id_numeric = atoi(argv[1]);
    if (my_drone.id_numeric <= 0) {
        fprintf(stderr, "Error: Drone ID must be a positive integer.\n");
        return 1;
    }
    snprintf(my_drone.drone_id_str, sizeof(my_drone.drone_id_str), "%s%d", DRONE_ID_PREFIX, my_drone.id_numeric);
    
    srand(time(NULL) + my_drone.id_numeric); 
    my_drone.status = IDLE; 
    my_drone.current_pos = (Coord){rand() % 20, rand() % 30}; 
    my_drone.target_pos = my_drone.current_pos;
    my_drone.battery_level = 100; 
    memset(my_drone.current_mission_id, 0, sizeof(my_drone.current_mission_id));

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("Client: socket creation failed"); return 1; }
    
    // Socket ayarlarını optimize et
    int yes = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("Client: setsockopt SO_REUSEADDR failed");
    }
    
    // TCP_NODELAY: Nagle algoritmasını devre dışı bırak (küçük paketlerin hemen gönderilmesi için)
    if (setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) {
        perror("Client: setsockopt TCP_NODELAY failed");
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Client: inet_pton failed"); close(sock_fd); return 1;
    }

    printf("Drone %s: Connecting to server %s:%d...\n", my_drone.drone_id_str, SERVER_IP, SERVER_PORT);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Client: connect failed"); close(sock_fd); return 1;
    }
    printf("Drone %s: Connected to server.\n", my_drone.drone_id_str);

    struct json_object *handshake_msg = json_object_new_object();
    json_object_object_add(handshake_msg, "type", json_object_new_string("HANDSHAKE"));
    json_object_object_add(handshake_msg, "drone_id", json_object_new_string(my_drone.drone_id_str));
    struct json_object *caps = json_object_new_object();
    json_object_object_add(caps, "max_speed", json_object_new_int(1)); 
    json_object_object_add(caps, "battery_capacity", json_object_new_int(1000)); 
    json_object_object_add(caps, "payload", json_object_new_string("aid_package_v2"));
    json_object_object_add(handshake_msg, "capabilities", caps);
    send_json_to_server(sock_fd, handshake_msg, my_drone.drone_id_str);
    json_object_put(handshake_msg); 

    time_t last_status_update_time = 0;
    int status_update_interval_secs = 5; 

    char aggregate_buffer_client[RECV_AGGREGATE_BUFFER_SIZE_CLIENT];
    memset(aggregate_buffer_client, 0, sizeof(aggregate_buffer_client));
    int aggregate_len_client = 0;

    fd_set read_fds;
    struct timeval tv;
    int running = 1;
    int handshake_ack_received = 0;

    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(sock_fd, &read_fds);
        // FD_SET(STDIN_FILENO, &read_fds); // Kullanıcıdan komut almak için

        // select() için daha kısa bir timeout - simülasyon döngüsünün daha sık çalışmasını sağlar
        tv.tv_sec = 0; 
        tv.tv_usec = 20000; // 0.02 saniye - daha yumuşak hareket için daha sık güncellemeler

        int activity = select(sock_fd + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) { 
            perror("Client: select error"); break;
        }
        
        if (FD_ISSET(sock_fd, &read_fds)) {
            if (aggregate_len_client >= sizeof(aggregate_buffer_client) -1) {
                fprintf(stderr, "Client %s: Aggregate buffer critically full.\n", my_drone.drone_id_str);
                running = 0; continue;
            }
            ssize_t bytes_received = recv(sock_fd, aggregate_buffer_client + aggregate_len_client, 
                                          sizeof(aggregate_buffer_client) - aggregate_len_client - 1, 0);
            if (bytes_received <= 0) {
                if (bytes_received == 0) printf("Drone %s: Server closed connection.\n", my_drone.drone_id_str);
                else perror("Client: recv error from server");
                running = 0; continue;
            }
            aggregate_len_client += bytes_received;
            aggregate_buffer_client[aggregate_len_client] = '\0';

            char *newline_pos_client;
            while((newline_pos_client = strchr(aggregate_buffer_client, '\n')) != NULL) {
                *newline_pos_client = '\0';
                char single_json_str_client[BUFFER_SIZE]; 
                strncpy(single_json_str_client, aggregate_buffer_client, sizeof(single_json_str_client)-1);
                single_json_str_client[sizeof(single_json_str_client)-1] = '\0';
                
                memmove(aggregate_buffer_client, newline_pos_client + 1, aggregate_len_client - (newline_pos_client - aggregate_buffer_client + 1));
                aggregate_len_client -= (newline_pos_client - aggregate_buffer_client + 1);
                aggregate_buffer_client[aggregate_len_client] = '\0';

                // printf("Client %s: Processing JSON: %s\n", my_drone.drone_id_str, single_json_str_client); // Debug
                struct json_object *parsed_json = json_tokener_parse(single_json_str_client);
                if (parsed_json) {
                    struct json_object *msg_type_obj;
                    if (json_object_object_get_ex(parsed_json, "type", &msg_type_obj)) {
                        const char *msg_type = json_object_get_string(msg_type_obj);
                        if (strcmp(msg_type, "HANDSHAKE_ACK") == 0) {
                            printf("Drone %s: HANDSHAKE_ACK received.\n", my_drone.drone_id_str);
                            handshake_ack_received = 1;
                            struct json_object *config_obj, *status_interval_obj;
                            if (json_object_object_get_ex(parsed_json, "config", &config_obj) &&
                                json_object_object_get_ex(config_obj, "status_update_interval", &status_interval_obj)) {
                                status_update_interval_secs = json_object_get_int(status_interval_obj);
                                printf("Drone %s: Status update interval set to %d seconds by server.\n", my_drone.drone_id_str, status_update_interval_secs);
                            }
                        } else if (strcmp(msg_type, "ASSIGN_MISSION") == 0) {
                            // printf("Drone %s: ASSIGN_MISSION received (raw: %s)\n", my_drone.drone_id_str, single_json_str_client);
                            struct json_object *target_obj, *x_obj, *y_obj, *mission_id_obj_recv;
                            const char* mission_id_recv_str = "UNKNOWN_MISSION";
                            if (json_object_object_get_ex(parsed_json, "mission_id", &mission_id_obj_recv)) {
                                mission_id_recv_str = json_object_get_string(mission_id_obj_recv);
                                strncpy(my_drone.current_mission_id, mission_id_recv_str, sizeof(my_drone.current_mission_id)-1);
                            }

                            if (json_object_object_get_ex(parsed_json, "target", &target_obj) &&
                                json_object_object_get_ex(target_obj, "x", &x_obj) &&
                                json_object_object_get_ex(target_obj, "y", &y_obj)) {
                                
                                my_drone.target_pos.x = json_object_get_int(x_obj);
                                my_drone.target_pos.y = json_object_get_int(y_obj);
                                my_drone.status = ON_MISSION;
                                printf("Drone %s: New mission (%s) assigned. Target: (%d,%d).\n",
                                       my_drone.drone_id_str, my_drone.current_mission_id,
                                       my_drone.target_pos.x, my_drone.target_pos.y);
                            } else {
                                fprintf(stderr, "Drone %s: Malformed ASSIGN_MISSION (missing target/coords).\n", my_drone.drone_id_str);
                            }
                        } else if (strcmp(msg_type, "HEARTBEAT") == 0) {
                            struct json_object *hb_resp = json_object_new_object();
                            json_object_object_add(hb_resp, "type", json_object_new_string("HEARTBEAT_RESPONSE"));
                            json_object_object_add(hb_resp, "drone_id", json_object_new_string(my_drone.drone_id_str));
                            json_object_object_add(hb_resp, "timestamp", json_object_new_int64(time(NULL)));
                            send_json_to_server(sock_fd, hb_resp, my_drone.drone_id_str);
                            json_object_put(hb_resp);
                        } else if (strcmp(msg_type, "ERROR") == 0) {
                            struct json_object *error_msg_obj, *error_type_obj;
                            const char *err_str = NULL;
                            int err_type = 0;
                            if (json_object_object_get_ex(parsed_json, "error_msg", &error_msg_obj))
                                err_str = json_object_get_string(error_msg_obj);
                            if (json_object_object_get_ex(parsed_json, "error_type", &error_type_obj))
                                err_type = json_object_get_int(error_type_obj);
                            fprintf(stderr, "Drone %s: Received ERROR from server: %s (type: %d)\n", my_drone.drone_id_str, err_str ? err_str : "(no msg)", err_type);
                            if (err_type == 1 || err_type == 2) { // handshake veya JSON hatası
                                running = 0;
                            }
                        }
                    }
                    json_object_put(parsed_json);
                } else {
                    fprintf(stderr, "Drone %s: Failed to parse JSON from server: '%s'\n", my_drone.drone_id_str, single_json_str_client);
                }
                if(strlen(aggregate_buffer_client) == 0) break; // İşlenecek başka mesaj yoksa iç döngüden çık
            } 
        } 

        // --- Sadece handshake_ack_received == 1 ise ana drone işlemlerini yap ---
        if (!handshake_ack_received) continue;

        time_t current_time = time(NULL);
        DroneState previous_status_for_mission_complete = my_drone.status;
        
        if (running) simulate_movement(&my_drone, sock_fd); 
        
        if (previous_status_for_mission_complete == ON_MISSION && my_drone.status == IDLE && running) {
            struct json_object *mission_comp_msg = json_object_new_object();
            json_object_object_add(mission_comp_msg, "type", json_object_new_string("MISSION_COMPLETE"));
            json_object_object_add(mission_comp_msg, "drone_id", json_object_new_string(my_drone.drone_id_str));
            json_object_object_add(mission_comp_msg, "mission_id", json_object_new_string(my_drone.current_mission_id[0] ? my_drone.current_mission_id : "UNKNOWN_COMPLETED")); 
            json_object_object_add(mission_comp_msg, "timestamp", json_object_new_int64(time(NULL)));
            json_object_object_add(mission_comp_msg, "success", json_object_new_boolean(1));
            json_object_object_add(mission_comp_msg, "details", json_object_new_string("Aid delivered successfully."));
            send_json_to_server(sock_fd, mission_comp_msg, my_drone.drone_id_str);
            json_object_put(mission_comp_msg);
            memset(my_drone.current_mission_id, 0, sizeof(my_drone.current_mission_id)); 
        }

        if (current_time - last_status_update_time >= status_update_interval_secs && running) {
            struct json_object *status_update_msg = json_object_new_object();
            json_object_object_add(status_update_msg, "type", json_object_new_string("STATUS_UPDATE"));
            json_object_object_add(status_update_msg, "drone_id", json_object_new_string(my_drone.drone_id_str));
            json_object_object_add(status_update_msg, "timestamp", json_object_new_int64(current_time));
            struct json_object *loc = json_object_new_object();
            json_object_object_add(loc, "x", json_object_new_int(my_drone.current_pos.x));
            json_object_object_add(loc, "y", json_object_new_int(my_drone.current_pos.y));
            json_object_object_add(status_update_msg, "location", loc);
            json_object_object_add(status_update_msg, "status", json_object_new_string(my_drone.status == IDLE ? "idle" : (my_drone.status == ON_MISSION ? "busy" : "unknown")));
            json_object_object_add(status_update_msg, "battery", json_object_new_int(my_drone.battery_level)); 
            json_object_object_add(status_update_msg, "speed", json_object_new_int(1)); 
            send_json_to_server(sock_fd, status_update_msg, my_drone.drone_id_str);
            json_object_put(status_update_msg);
            last_status_update_time = current_time;
        }
        // Check battery depletion
        if (my_drone.battery_level <= 0 && running) {
            printf("Drone %s: Battery depleted! Sending mission complete if on mission.\n", my_drone.drone_id_str);
            if (my_drone.status == ON_MISSION) {
                struct json_object *mc = json_object_new_object();
                json_object_object_add(mc, "type", json_object_new_string("MISSION_COMPLETE"));
                json_object_object_add(mc, "drone_id", json_object_new_string(my_drone.drone_id_str));
                json_object_object_add(mc, "mission_id", json_object_new_string(my_drone.current_mission_id[0] ? my_drone.current_mission_id : "UNKNOWN"));
                json_object_object_add(mc, "timestamp", json_object_new_int64(time(NULL)));
                json_object_object_add(mc, "success", json_object_new_boolean(1));
                send_json_to_server(sock_fd, mc, my_drone.drone_id_str);
                json_object_put(mc);
            }
            running = 0;
        }

        usleep(16667); // 60 FPS için gecikme
    } // while(running)

    printf("Drone %s: Disconnecting.\n", my_drone.drone_id_str);
    close(sock_fd);
    return 0;
}