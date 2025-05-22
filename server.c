// server.c
#include "headers/globals.h"
#include "headers/list.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/ai.h"
#include "headers/map.h"
#include "headers/connection_handling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <json.h>
#include <errno.h>

#define SERVER_PORT 8080
#define MAX_PENDING_CONNECTIONS 15
#define BUFFER_SIZE 1024
#define RECV_AGGREGATE_BUFFER_SIZE (BUFFER_SIZE * 4)
#define VIEWER_UPDATE_INTERVAL_MS 40 // 25 fps ≃ 40 ms

// --- Global Değişkenler ---
List *viewers_list = NULL;
pthread_mutex_t viewers_list_lock;

// Sinyal işleyici ve ana döngü tarafından kullanılan global değişkenler
int server_socket_fd = -1;
volatile sig_atomic_t server_running = 1;

// --- Fonksiyon Prototipleri ---
void* handle_viewer_connection(void* arg);
void* handle_drone_connection(void* arg);
void server_signal_handler(int signum);
void send_json_to_client_socket(int sock_fd, struct json_object *json_obj, const char* log_prefix);
struct json_object* create_simulation_state_update_json();
// Hata mesajı göndermek için yardımcı fonksiyon
typedef enum { ERROR_NONE, ERROR_HANDSHAKE, ERROR_JSON, ERROR_TIMEOUT, ERROR_MISSION } ErrorType;
void send_error_to_client(int sock_fd, const char* error_msg, ErrorType err_type) {
    struct json_object *err_json = json_object_new_object();
    json_object_object_add(err_json, "type", json_object_new_string("ERROR"));
    json_object_object_add(err_json, "error_msg", json_object_new_string(error_msg));
    json_object_object_add(err_json, "error_type", json_object_new_int(err_type));
    const char *json_str_raw = json_object_to_json_string_ext(err_json, JSON_C_TO_STRING_PLAIN);
    char message_with_newline[strlen(json_str_raw) + 2];
    sprintf(message_with_newline, "%s\n", json_str_raw);
    send(sock_fd, message_with_newline, strlen(message_with_newline), 0);
    json_object_put(err_json);
}

void send_json_to_client_socket(int sock_fd, struct json_object *json_obj, const char* log_prefix) {
    if (!json_obj) return;
    const char *json_str_raw = json_object_to_json_string_ext(json_obj, JSON_C_TO_STRING_PLAIN);
    if (!json_str_raw) {
        fprintf(stderr, "%s: Failed to stringify JSON object for socket %d.\n", log_prefix, sock_fd);
        return;
    }

    char message_with_newline[strlen(json_str_raw) + 2];
    sprintf(message_with_newline, "%s\n", json_str_raw);

    if (send(sock_fd, message_with_newline, strlen(message_with_newline), 0) < 0) {
        fprintf(stderr, "%s: Failed to send JSON to socket %d\n", log_prefix, sock_fd);
    }
}

struct json_object* create_simulation_state_update_json() {
    struct json_object *state_update_msg = json_object_new_object();
    if (!state_update_msg) return NULL;

    json_object_object_add(state_update_msg, "type", json_object_new_string("SIMULATION_STATE_UPDATE"));
    json_object_object_add(state_update_msg, "timestamp", json_object_new_int64(time(NULL)));

    // Map boyutu
    struct json_object *map_dim_obj = json_object_new_object();
    if(map_dim_obj) {
        json_object_object_add(map_dim_obj, "width", json_object_new_int(map.width));
        json_object_object_add(map_dim_obj, "height", json_object_new_int(map.height));
        json_object_object_add(state_update_msg, "map_dimensions", map_dim_obj);
    }

    // Droneları ekle
    struct json_object *drones_json_array = json_object_new_array();
    if (drones && drones_json_array) {
        pthread_mutex_lock(&drones->lock);
        Node *d_node = drones->head;
        while (d_node != NULL) {
            Drone *d = *(Drone**)d_node->data;
            if (d) {
                pthread_mutex_lock(&d->lock);
                struct json_object *d_json = json_object_new_object();
                if (d_json) {
                    json_object_object_add(d_json, "id_str", json_object_new_string(d->id_str));
                    struct json_object *coord_json = json_object_new_object();
                    if (coord_json) {
                        json_object_object_add(coord_json, "x", json_object_new_int(d->coord.x));
                        json_object_object_add(coord_json, "y", json_object_new_int(d->coord.y));
                        json_object_object_add(d_json, "coord", coord_json);
                    }
                    struct json_object *target_json = json_object_new_object();
                    if (target_json) {
                        json_object_object_add(target_json, "x", json_object_new_int(d->target.x));
                        json_object_object_add(target_json, "y", json_object_new_int(d->target.y));
                        json_object_object_add(d_json, "target", target_json);
                    }
                    json_object_object_add(d_json, "status", json_object_new_string(d->status == IDLE ? "IDLE" : "ON_MISSION"));
                    json_object_array_add(drones_json_array, d_json);
                }
                pthread_mutex_unlock(&d->lock);
            }
            d_node = d_node->next;
        }
        pthread_mutex_unlock(&drones->lock);
    }
    json_object_object_add(state_update_msg, "drones", drones_json_array);

    // Survivorları ekle
    struct json_object *survivors_json_array = json_object_new_array();
    if (survivors && survivors_json_array) {
        pthread_mutex_lock(&survivors->lock);
        Node *s_node = survivors->head;
        while (s_node != NULL) {
            Survivor *s = *(Survivor**)s_node->data;
            if (s) {
                struct json_object *s_json = json_object_new_object();
                if (s_json) {
                    json_object_object_add(s_json, "info", json_object_new_string(s->info));
                    struct json_object *coord_json_s = json_object_new_object();
                    if (coord_json_s) {
                        json_object_object_add(coord_json_s, "x", json_object_new_int(s->coord.x));
                        json_object_object_add(coord_json_s, "y", json_object_new_int(s->coord.y));
                        json_object_object_add(s_json, "coord", coord_json_s);
                    }

                    const char *status_str;
                    if (s->status == WAITING) status_str = "WAITING";
                    else if (s->status == ASSIGNED) status_str = "ASSIGNED";
                    else if (s->status == HELPED) status_str = "HELPED";
                    else status_str = "UNKNOWN";

                    json_object_object_add(s_json, "status", json_object_new_string(status_str));
                    // Always include survivors; viewer handles HELPED drawing
                    json_object_array_add(survivors_json_array, s_json);
                }
            }
            s_node = s_node->next;
        }
        pthread_mutex_unlock(&survivors->lock);
    }
    json_object_object_add(state_update_msg, "survivors", survivors_json_array);

    return state_update_msg;
}
void* handle_drone_connection(void* arg) {
    struct handler_args *args = (struct handler_args*)arg;
    int client_socket_fd = args->client_fd;
    // Parse handshake from main thread
    char *hs_str = args->initial_msg;
    free(args);
    struct json_object *handshake_json = json_tokener_parse(hs_str);
    if (!handshake_json) {
        fprintf(stderr, "[DroneH ?] Invalid HANDSHAKE JSON: %s\n", hs_str);
        close(client_socket_fd); return NULL;
    }
    struct json_object *type_obj_hs, *id_obj_hs;
    json_object_object_get_ex(handshake_json, "type", &type_obj_hs);
    json_object_object_get_ex(handshake_json, "drone_id", &id_obj_hs);
    const char *msg_type_hs = json_object_get_string(type_obj_hs);
    const char *drone_id_str = json_object_get_string(id_obj_hs);
    int parsed_id = -1;
    if (drone_id_str && (drone_id_str[0]=='D'||drone_id_str[0]=='d')) parsed_id = atoi(drone_id_str+1);
    if (!msg_type_hs || strcmp(msg_type_hs,"HANDSHAKE")!=0 || parsed_id<=0) {
        fprintf(stderr, "[DroneH ?] Invalid HANDSHAKE format.\n");
        json_object_put(handshake_json);
        close(client_socket_fd); return NULL;
    }
    Drone *this_drone_ptr = server_create_drone_instance(parsed_id, drone_id_str, client_socket_fd);
    if (!this_drone_ptr) {
        send_error_to_client(client_socket_fd, "Failed to create drone", ERROR_HANDSHAKE);
        json_object_put(handshake_json);
        close(client_socket_fd); return NULL;
    }
    drones->add(drones, &this_drone_ptr);
    // send ACK
    struct json_object *ack_msg = json_object_new_object();
    json_object_object_add(ack_msg, "type", json_object_new_string("HANDSHAKE_ACK"));
    struct json_object *config_obj = json_object_new_object();
    json_object_object_add(config_obj, "status_update_interval", json_object_new_int(0));
    json_object_object_add(config_obj, "heartbeat_interval", json_object_new_int(10));
    json_object_object_add(ack_msg, "config", config_obj);
    send_json_to_client_socket(client_socket_fd, ack_msg, "[Drone]");
    json_object_put(ack_msg);
    json_object_put(handshake_json);

    char client_ip_str[INET_ADDRSTRLEN];

    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);
    if (getpeername(client_socket_fd, (struct sockaddr*)&peer_addr, &peer_addr_len) == 0) {
        inet_ntop(AF_INET, &peer_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
    } else {
        strncpy(client_ip_str, "UNKNOWN_IP", sizeof(client_ip_str) - 1);
        client_ip_str[sizeof(client_ip_str) - 1] = '\0';
    }

    char log_prefix_drone[64];
    snprintf(log_prefix_drone, sizeof(log_prefix_drone), "[DroneH %s(S%d)]", client_ip_str, client_socket_fd);
    printf("%s: Thread started.\n", log_prefix_drone);

    char aggregate_buffer[RECV_AGGREGATE_BUFFER_SIZE];
    memset(aggregate_buffer, 0, sizeof(aggregate_buffer));
    int aggregate_len = 0;
    ssize_t bytes_received;

    time_t last_server_heartbeat_sent = time(NULL);
    int server_heartbeat_interval = 10;

    pthread_mutex_lock(&this_drone_ptr->lock);
    this_drone_ptr->last_heartbeat_time = time(NULL);
    pthread_mutex_unlock(&this_drone_ptr->lock);

    while (server_running) {
        fd_set read_fds;
        struct timeval tv;

        FD_ZERO(&read_fds);
        FD_SET(client_socket_fd, &read_fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(client_socket_fd + 1, &read_fds, NULL, NULL, &tv);
        if (!server_running) break;
        if (activity < 0 && errno != EINTR) {
            perror(log_prefix_drone);
            break;
        }

        time_t current_time = time(NULL);

        if (FD_ISSET(client_socket_fd, &read_fds)) {
            if (aggregate_len >= RECV_AGGREGATE_BUFFER_SIZE - 1) break;

            bytes_received = recv(client_socket_fd, aggregate_buffer + aggregate_len, RECV_AGGREGATE_BUFFER_SIZE - aggregate_len - 1, 0);
            if (bytes_received <= 0) break;

            aggregate_len += bytes_received;
            aggregate_buffer[aggregate_len] = '\0';

            pthread_mutex_lock(&this_drone_ptr->lock);
            this_drone_ptr->last_heartbeat_time = current_time;
            pthread_mutex_unlock(&this_drone_ptr->lock);

            char *newline_pos_loop;
            while ((newline_pos_loop = strchr(aggregate_buffer, '\n')) != NULL) {
                *newline_pos_loop = '\0';
                char single_json_str_loop[BUFFER_SIZE];
                strncpy(single_json_str_loop, aggregate_buffer, sizeof(single_json_str_loop) - 1);
                single_json_str_loop[sizeof(single_json_str_loop) - 1] = '\0';

                memmove(aggregate_buffer, newline_pos_loop + 1, aggregate_len - (newline_pos_loop - aggregate_buffer + 1));
                aggregate_len -= (newline_pos_loop - aggregate_buffer + 1);

                struct json_object *parsed_json = json_tokener_parse(single_json_str_loop);
                if (!parsed_json) {
                    fprintf(stderr, "%s: Invalid JSON in loop: %s\n", log_prefix_drone, single_json_str_loop);
                    continue;
                }

                struct json_object *msg_type_obj_loop;
                if (json_object_object_get_ex(parsed_json, "type", &msg_type_obj_loop)) {
                    const char *msg_type = json_object_get_string(msg_type_obj_loop);

                    if (strcmp(msg_type, "STATUS_UPDATE") == 0) {
                        struct json_object *loc_obj, *status_str_obj, *id_confirm_obj;
                        const char *id_confirm_str = NULL;

                        if (json_object_object_get_ex(parsed_json, "drone_id", &id_confirm_obj))
                            id_confirm_str = json_object_get_string(id_confirm_obj);

                        if (!id_confirm_str || strcmp(id_confirm_str, this_drone_ptr->id_str) != 0) {
                            fprintf(stderr, "%s: STATUS_UPDATE mismatched ID. Expected %s, got %s\n",
                                    log_prefix_drone, this_drone_ptr->id_str, id_confirm_str ? id_confirm_str : "N/A");
                        } else {
                            pthread_mutex_lock(&this_drone_ptr->lock);
                            if (json_object_object_get_ex(parsed_json, "location", &loc_obj)) {
                                struct json_object *x_obj, *y_obj;
                                if (json_object_object_get_ex(loc_obj, "x", &x_obj)) this_drone_ptr->coord.x = json_object_get_int(x_obj);
                                if (json_object_object_get_ex(loc_obj, "y", &y_obj)) this_drone_ptr->coord.y = json_object_get_int(y_obj);
                            }
                            if (json_object_object_get_ex(parsed_json, "status", &status_str_obj)) {
                                const char *status_str = json_object_get_string(status_str_obj);
                                if (strcmp(status_str, "idle") == 0) this_drone_ptr->status = IDLE;
                                else if (strcmp(status_str, "busy") == 0 || strcmp(status_str, "on_mission") == 0) this_drone_ptr->status = ON_MISSION;
                            }
                            pthread_mutex_unlock(&this_drone_ptr->lock);
                        }

                    } else if (strcmp(msg_type, "MISSION_COMPLETE") == 0) {
                        printf("%s: MISSION_COMPLETE received.\n", log_prefix_drone);
                        struct json_object *success_obj, *mission_id_obj;
                        const char *mission_id_str = NULL;
                        int mission_success = 0;

                        if (json_object_object_get_ex(parsed_json, "mission_id", &mission_id_obj))
                            mission_id_str = json_object_get_string(mission_id_obj);
                        if (json_object_object_get_ex(parsed_json, "success", &success_obj))
                            mission_success = json_object_get_boolean(success_obj);

                        pthread_mutex_lock(&this_drone_ptr->lock);
                        if (this_drone_ptr->current_survivor_target && mission_success) {
                            Survivor *helped_survivor = this_drone_ptr->current_survivor_target;
                            if (helped_survivor->status != HELPED) {
                                helped_survivor->status = HELPED;
                                time_t now; time(&now);
                                localtime_r(&now, &helped_survivor->helped_time);
                                printf("%s: Survivor %s helped. Mission: %s\n", log_prefix_drone,
                                       helped_survivor->info, mission_id_str ? mission_id_str : "N/A");

                                // Remove survivor immediately once helped
                                survivors->removedata(survivors, &helped_survivor);
                                Coord sc = helped_survivor->coord;
                                if (sc.x >= 0 && sc.x < map.height && sc.y >= 0 && sc.y < map.width) {
                                    if (map.cells[sc.x][sc.y].survivors)
                                        map.cells[sc.x][sc.y].survivors->removedata(map.cells[sc.x][sc.y].survivors, &helped_survivor);
                                }
                                helpedsurvivors->add(helpedsurvivors, &helped_survivor);
                            }
                        }
                        this_drone_ptr->status = IDLE;
                        this_drone_ptr->current_survivor_target = NULL;
                        pthread_mutex_unlock(&this_drone_ptr->lock);

                    } else if (strcmp(msg_type, "HEARTBEAT_RESPONSE") == 0) {
                        // sadece sessizlik bozulsun diye loglanabilir
                        // printf("%s: HEARTBEAT_RESPONSE\n", log_prefix_drone);
                    }
                }
                json_object_put(parsed_json);
            }

            if (strlen(aggregate_buffer) == 0) aggregate_len = 0;
        }

        if (current_time - last_server_heartbeat_sent >= server_heartbeat_interval) {
            struct json_object *hb_msg = json_object_new_object();
            if (hb_msg) {
                json_object_object_add(hb_msg, "type", json_object_new_string("HEARTBEAT"));
                json_object_object_add(hb_msg, "timestamp", json_object_new_int64(current_time));
                send_json_to_client_socket(client_socket_fd, hb_msg, log_prefix_drone);
                json_object_put(hb_msg);
            }
            last_server_heartbeat_sent = current_time;
        }

        pthread_mutex_lock(&this_drone_ptr->lock);
        time_t last_hb_from_drone = this_drone_ptr->last_heartbeat_time;
        pthread_mutex_unlock(&this_drone_ptr->lock);

        if (current_time - last_hb_from_drone > 30) {
            fprintf(stderr, "%s: Drone timed out (no client activity/heartbeat).\n", log_prefix_drone);
            break;
        }
    }

    if (this_drone_ptr) {
        if (drones->removedata(drones, &this_drone_ptr) == 0) {
            printf("%s: Removed from list. Total: %d\n", log_prefix_drone, drones->number_of_elements);
        }
        server_cleanup_drone_instance(this_drone_ptr);
        this_drone_ptr = NULL;
    }

    close(client_socket_fd);
    printf("%s: Connection closed and thread exiting.\n", log_prefix_drone);
    pthread_exit(NULL);
    return NULL;
}
void* handle_viewer_connection(void* arg) {
    struct handler_args *args = (struct handler_args*)arg;
    int viewer_socket_fd = args->client_fd;
    free(args);

    char client_ip_str_v[INET_ADDRSTRLEN];
    struct sockaddr_in peer_addr_v;
    socklen_t peer_addr_len_v = sizeof(peer_addr_v);
    if (getpeername(viewer_socket_fd, (struct sockaddr*)&peer_addr_v, &peer_addr_len_v) == 0) {
        inet_ntop(AF_INET, &peer_addr_v.sin_addr, client_ip_str_v, sizeof(client_ip_str_v));
    } else {
        strncpy(client_ip_str_v, "UNKN_VIEWER_IP", sizeof(client_ip_str_v)-1);
        client_ip_str_v[sizeof(client_ip_str_v)-1] = '\0';
    }

    char log_prefix_viewer[64];
    snprintf(log_prefix_viewer, sizeof(log_prefix_viewer), "[ViewerH %s(S%d)]", client_ip_str_v, viewer_socket_fd);
    printf("%s: Connection established.\n", log_prefix_viewer);

    // Handshake ACK gönder
    struct json_object *ack_msg_v = json_object_new_object();
    if (ack_msg_v) {
        json_object_object_add(ack_msg_v, "type", json_object_new_string("VIEWER_HANDSHAKE_ACK"));
        json_object_object_add(ack_msg_v, "message", json_object_new_string("Viewer connection accepted."));
        struct json_object *map_dim_obj_v = json_object_new_object();
        if (map_dim_obj_v) {
            json_object_object_add(map_dim_obj_v, "width", json_object_new_int(map.width));
            json_object_object_add(map_dim_obj_v, "height", json_object_new_int(map.height));
            json_object_object_add(ack_msg_v, "initial_map_dimensions", map_dim_obj_v);
        }
        send_json_to_client_socket(viewer_socket_fd, ack_msg_v, log_prefix_viewer);
        json_object_put(ack_msg_v);
    }

    // viewers_list'e ekle
    int *fd_ptr_for_list = malloc(sizeof(int));
    if (!fd_ptr_for_list) {
        perror("malloc for viewer fd");
        close(viewer_socket_fd);
        pthread_exit(NULL);
    }
    *fd_ptr_for_list = viewer_socket_fd;

    pthread_mutex_lock(&viewers_list_lock);
    viewers_list->add(viewers_list, &fd_ptr_for_list);
    pthread_mutex_unlock(&viewers_list_lock);

    while (server_running) {
        struct json_object *state_update = create_simulation_state_update_json();
        if (state_update) {
            send_json_to_client_socket(viewer_socket_fd, state_update, log_prefix_viewer);
            json_object_put(state_update);
        } else {
            fprintf(stderr, "%s: Failed to create state update JSON.\n", log_prefix_viewer);
        }

        fd_set readfds_viewer_loop;
        FD_ZERO(&readfds_viewer_loop);
        FD_SET(viewer_socket_fd, &readfds_viewer_loop);
        struct timeval tv_viewer_check_loop;
        tv_viewer_check_loop.tv_sec = 0;
        tv_viewer_check_loop.tv_usec = 10000;

        int activity_v = select(viewer_socket_fd + 1, &readfds_viewer_loop, NULL, NULL, &tv_viewer_check_loop);
        if (!server_running) break;

        if (activity_v > 0 && FD_ISSET(viewer_socket_fd, &readfds_viewer_loop)) {
            char temp_buf[16];
            if (recv(viewer_socket_fd, temp_buf, sizeof(temp_buf) - 1, 0) <= 0) {
                printf("%s: Viewer client disconnected.\n", log_prefix_viewer);
                break;
            }
        } else if (activity_v < 0 && errno != EINTR) {
            perror("Viewer handler select error");
            break;
        }

        struct timespec ts = {0, VIEWER_UPDATE_INTERVAL_MS * 1000000L};
        nanosleep(&ts, NULL);
    }

    pthread_mutex_lock(&viewers_list_lock);
    if (viewers_list->removedata(viewers_list, &fd_ptr_for_list) == 0) {
        printf("%s: Removed from active viewers list.\n", log_prefix_viewer);
    } else {
        fprintf(stderr, "%s: Failed to remove from active viewers list.\n", log_prefix_viewer);
    }
    pthread_mutex_unlock(&viewers_list_lock);

    free(fd_ptr_for_list);
    close(viewer_socket_fd);
    printf("%s: Connection closed and thread exiting.\n", log_prefix_viewer);
    pthread_exit(NULL);
    return NULL;
}
void server_signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        if (!server_running) {
            fprintf(stderr, "\nForced exit during shutdown.\n");
            _exit(2);
        }
        printf("\nSignal %d received, server shutting down gracefully...\n", signum);
        server_running = 0;

        if (server_socket_fd != -1) {
            close(server_socket_fd);
            server_socket_fd = -1;
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand(time(NULL));

    struct sigaction sa;
    sa.sa_handler = server_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Server starting on port %d...\n", SERVER_PORT);

    survivors = create_list(sizeof(Survivor*), 100);
    helpedsurvivors = create_list(sizeof(Survivor*), 500);
    drones = create_list(sizeof(Drone*), 50);
    viewers_list = create_list(sizeof(int*), 10);
    if (!survivors || !helpedsurvivors || !drones || !viewers_list) {
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&viewers_list_lock, NULL) != 0) {
        perror("Failed to init viewers_list_lock");
        exit(EXIT_FAILURE);
    }
    printf("Global lists created for server.\n");

    init_map(20, 30);
    printf("Map initialized: %dx%d\n", map.width, map.height);
    printf("Map initialized for server.\n");

    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(SERVER_PORT);

    if (bind(server_socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("bind");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket_fd, MAX_PENDING_CONNECTIONS) < 0) {
        perror("listen");
        close(server_socket_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d.\n", SERVER_PORT);

    pthread_t survivor_thread, ai_thread;
    pthread_create(&survivor_thread, NULL, survivor_generator, NULL);
    pthread_create(&ai_thread, NULL, ai_controller, NULL);
    printf("Survivor generator and AI controller threads started for server.\n");

    printf("Server entering main accept loop...\n");

    while (server_running) {
        int *client_fd_ptr = malloc(sizeof(int));
        if (!client_fd_ptr) continue;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        *client_fd_ptr = accept(server_socket_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (*client_fd_ptr < 0) {
            free(client_fd_ptr);
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        // Read initial handshake message
        char buffer[BUFFER_SIZE];
        ssize_t bytes = recv(*client_fd_ptr, buffer, sizeof(buffer) - 1, MSG_PEEK);
        if (bytes <= 0) {
            close(*client_fd_ptr);
            free(client_fd_ptr);
            continue;
        }
        buffer[bytes] = '\0';
        char *newline = strchr(buffer, '\n'); if (newline) *newline = '\0';
        struct json_object *initial_json = json_tokener_parse(buffer);
        if (!initial_json) {
            close(*client_fd_ptr);
            free(client_fd_ptr);
            continue;
        }
        struct json_object *type_obj;
        pthread_t handler_thread;
        int dispatched = 0;
        if (json_object_object_get_ex(initial_json, "type", &type_obj)) {
            const char *type = json_object_get_string(type_obj);
            struct handler_args *args = malloc(sizeof(*args));
            args->client_fd = *client_fd_ptr;
            strncpy(args->initial_msg, buffer, sizeof(args->initial_msg)-1);
            args->initial_msg[sizeof(args->initial_msg)-1] = '\0';
            if (strcmp(type, "HANDSHAKE") == 0) {
                dispatched = (pthread_create(&handler_thread, NULL, handle_drone_connection, args) == 0);
                if (dispatched) pthread_detach(handler_thread), printf("Server: Dispatched drone handler for socket %d.\n", *client_fd_ptr);
            } else if (strcmp(type, "VIEWER_HANDSHAKE") == 0) {
                dispatched = (pthread_create(&handler_thread, NULL, handle_viewer_connection, args) == 0);
                if (dispatched) pthread_detach(handler_thread), printf("Server: Dispatched viewer handler for socket %d.\n", *client_fd_ptr);
            }
            if (!dispatched) {
                free(args);
                close(*client_fd_ptr);
            }
        }
        json_object_put(initial_json);
        free(client_fd_ptr);
    }

    close(server_socket_fd);
    server_socket_fd = -1;

    pthread_cancel(survivor_thread);
    pthread_cancel(ai_thread);
    pthread_join(survivor_thread, NULL);
    pthread_join(ai_thread, NULL);

    if (viewers_list) viewers_list->destroy(viewers_list);
    if (helpedsurvivors) helpedsurvivors->destroy(helpedsurvivors);
    if (survivors) survivors->destroy(survivors);
    if (drones) drones->destroy(drones);
    pthread_mutex_destroy(&viewers_list_lock);
    freemap();

    printf("Server shutdown complete.\n");
    return 0;
}
