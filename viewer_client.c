#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <json.h>
#include <SDL.h>
#include <errno.h>
#include <math.h>

#include "headers/coord.h"     // Coord tipi için
#include "headers/drone.h"     // DroneState enum'u için
#include "headers/survivor.h"  // SurvivorState enum'u için
// #include "headers/view.h"   // Eğer viewer_sdl.h gibi yeniden adlandırdıysak onu kullan
#include "headers/view.h"   // Şimdilik eski ismiyle kullanalım.

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080      // Sunucu ile aynı port
#define VIEWER_BUFFER_SIZE 8192 // Daha büyük JSON mesajları için
#define CELL_SIZE_PX 25       // Harita hücresi boyutu (piksel), reduced to show more cells

// Global SDL değişkenleri (sadece viewer_client.c için)
SDL_Window* g_vc_window = NULL;
SDL_Renderer* g_vc_renderer = NULL;
int g_vc_map_width_cells = 0;
int g_vc_map_height_cells = 0;

// Renkler (view.h'den extern edilebilir veya burada tanımlanabilir)
const SDL_Color VC_COLOR_BLACK = {0, 0, 0, 255};
const SDL_Color VC_COLOR_RED = {255, 0, 0, 255};         
const SDL_Color VC_COLOR_YELLOW = {255, 255, 0, 255};    
const SDL_Color VC_COLOR_BLUE = {0, 100, 255, 255};      
const SDL_Color VC_COLOR_GREEN = {0, 200, 0, 255};       
const SDL_Color VC_COLOR_WHITE = {200, 200, 200, 255};   
const SDL_Color VC_COLOR_DARK_GREY = {50, 50, 50, 255};

// Extend viewer drone info to animate cell-by-cell
typedef struct {
    char id_str[16];
    Coord coord;
    Coord target;
    DroneState status;
    Coord displayCoord; // for smooth rendering
    char displayInit;   // flag init displayCoord
} ViewerDroneInfo;

typedef struct {
    char info[25];
    Coord coord;
    SurvivorState status;
} ViewerSurvivorInfo;

ViewerDroneInfo viewer_drones_cache[50]; // Maks 50 drone için cache
int num_viewer_drones = 0;
ViewerSurvivorInfo viewer_survivors_cache[100]; // Maks 100 survivor için cache
int num_viewer_survivors = 0;
pthread_mutex_t cache_lock; // Cache'e erişimi korumak için


int vc_init_sdl_window(int map_width_cells, int map_height_cells, int cell_size_pixels) {
    g_vc_map_width_cells = map_width_cells;
    g_vc_map_height_cells = map_height_cells;

    int window_width_px = map_width_cells * cell_size_pixels;
    int window_height_px = map_height_cells * cell_size_pixels;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Viewer: SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    g_vc_window = SDL_CreateWindow("Drone Simulation Viewer", SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED, window_width_px, window_height_px,
                                 SDL_WINDOW_SHOWN);
    if (!g_vc_window) { /* Hata logla, SDL_Quit */ return 1; }
    
    g_vc_renderer = SDL_CreateRenderer(g_vc_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_vc_renderer) { /* Hata logla, Window destroy, SDL_Quit */ return 1; }
    
    printf("Viewer: SDL initialized. Window: %dx%d\n", window_width_px, window_height_px);
    return 0;
}

void vc_draw_cell(int map_x, int map_y, int cell_size_pixels, SDL_Color color) {
    if (!g_vc_renderer || map_x < 0 || map_x >= g_vc_map_height_cells || map_y < 0 || map_y >= g_vc_map_width_cells) return;
    SDL_Rect cell_rect = { map_y * cell_size_pixels, map_x * cell_size_pixels, cell_size_pixels, cell_size_pixels };
    SDL_SetRenderDrawColor(g_vc_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(g_vc_renderer, &cell_rect);
}

void vc_draw_grid(SDL_Renderer *renderer, int map_w_cells, int map_h_cells, int cell_size_px) {
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, VC_COLOR_WHITE.r, VC_COLOR_WHITE.g, VC_COLOR_WHITE.b, 50);
    for (int i = 0; i <= map_h_cells; i++) {
        SDL_RenderDrawLine(renderer, 0, i * cell_size_px, map_w_cells * cell_size_px, i * cell_size_px);
    }
    for (int j = 0; j <= map_w_cells; j++) {
        SDL_RenderDrawLine(renderer, j * cell_size_px, 0, j * cell_size_px, map_h_cells * cell_size_px);
    }
}

void vc_render_all() {
    if (!g_vc_renderer) return;
    SDL_SetRenderDrawColor(g_vc_renderer, VC_COLOR_DARK_GREY.r, VC_COLOR_DARK_GREY.g, VC_COLOR_DARK_GREY.b, 255);
    SDL_RenderClear(g_vc_renderer);

    vc_draw_grid(g_vc_renderer, g_vc_map_width_cells, g_vc_map_height_cells, CELL_SIZE_PX);

    pthread_mutex_lock(&cache_lock); // Cache'i okurken kilitle

    // Survivor'ları çiz (status==HELPED: draw until drone arrives)
    for (int i = 0; i < num_viewer_survivors; i++) {
        ViewerSurvivorInfo *s = &viewer_survivors_cache[i];
        SDL_Color s_color;
        if (s->status == WAITING) s_color = VC_COLOR_RED;
        else if (s->status == ASSIGNED) s_color = VC_COLOR_YELLOW;
        else if (s->status == HELPED) {
            // keep drawing until drone reaches
            int reached = 0;
            for (int j = 0; j < num_viewer_drones; j++) {
                ViewerDroneInfo *d = &viewer_drones_cache[j];
                if (d->displayCoord.x == s->coord.x && d->displayCoord.y == s->coord.y) {
                    reached = 1;
                    break;
                }
            }
            if (reached) continue;
            s_color = VC_COLOR_YELLOW;
        } else continue;
        vc_draw_cell(s->coord.x, s->coord.y, CELL_SIZE_PX, s_color);
    }

    // Drone'ları çiz (animate cell-by-cell)
    for (int i = 0; i < num_viewer_drones; i++) {
        ViewerDroneInfo *d = &viewer_drones_cache[i];
        SDL_Color d_color = (d->status == IDLE) ? VC_COLOR_BLUE : VC_COLOR_GREEN;
        // Animate displayCoord one axis per update
        if (!d->displayInit) {
            d->displayCoord = d->coord;
            d->displayInit = 1;
        } else {
            // Move one axis per frame: X first, then Y
            if (d->displayCoord.x < d->coord.x) {
                d->displayCoord.x++;
            } else if (d->displayCoord.x > d->coord.x) {
                d->displayCoord.x--;
            } else if (d->displayCoord.y < d->coord.y) {
                d->displayCoord.y++;
            } else if (d->displayCoord.y > d->coord.y) {
                d->displayCoord.y--;
            }
        }
        // Draw drone cell
        vc_draw_cell(d->displayCoord.x, d->displayCoord.y, CELL_SIZE_PX, d_color);
        // If on mission, draw arrow
        if (d->status == ON_MISSION) {
            SDL_SetRenderDrawColor(g_vc_renderer, VC_COLOR_GREEN.r, VC_COLOR_GREEN.g, VC_COLOR_GREEN.b, 255);
            int x0 = d->displayCoord.y * CELL_SIZE_PX + CELL_SIZE_PX/2;
            int y0 = d->displayCoord.x * CELL_SIZE_PX + CELL_SIZE_PX/2;
            int x1 = d->target.y * CELL_SIZE_PX + CELL_SIZE_PX/2;
            int y1 = d->target.x * CELL_SIZE_PX + CELL_SIZE_PX/2;
            SDL_RenderDrawLine(g_vc_renderer, x0, y0, x1, y1);
            double angle = atan2(y1 - y0, x1 - x0);
            double arr_len = CELL_SIZE_PX;
            double arr_ang = M_PI/6;
            int ax = x1 - (int)(arr_len * cos(angle - arr_ang));
            int ay = y1 - (int)(arr_len * sin(angle - arr_ang));
            int bx = x1 - (int)(arr_len * cos(angle + arr_ang));
            int by = y1 - (int)(arr_len * sin(angle + arr_ang));
            SDL_RenderDrawLine(g_vc_renderer, x1, y1, ax, ay);
            SDL_RenderDrawLine(g_vc_renderer, x1, y1, bx, by);
        }
    }
    pthread_mutex_unlock(&cache_lock);

    SDL_RenderPresent(g_vc_renderer);
}

int vc_check_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
            return 1; // Kapat
        }
    }
    return 0; // Devam et
}

void vc_quit_sdl() {
    if (g_vc_renderer) SDL_DestroyRenderer(g_vc_renderer);
    if (g_vc_window) SDL_DestroyWindow(g_vc_window);
    SDL_Quit();
    printf("Viewer: SDL quit.\n");
}


// Sunucudan gelen SIMULATION_STATE_UPDATE mesajını işleyen fonksiyon
void process_simulation_state(struct json_object *state_json) {
    struct json_object *map_dim_obj, *drones_array_obj, *survivors_array_obj;
    
    // Harita boyutlarını al (ilk mesajda veya her güncellemede)
    if (g_vc_map_width_cells == 0 && json_object_object_get_ex(state_json, "map_dimensions", &map_dim_obj)) {
        struct json_object *width_obj, *height_obj;
        if (json_object_object_get_ex(map_dim_obj, "width", &width_obj) &&
            json_object_object_get_ex(map_dim_obj, "height", &height_obj)) {
            int map_w = json_object_get_int(width_obj);
            int map_h = json_object_get_int(height_obj);
            if (g_vc_window == NULL) { // SDL henüz başlatılmadıysa
                 if (vc_init_sdl_window(map_w, map_h, CELL_SIZE_PX) != 0) {
                    fprintf(stderr, "Viewer: Failed to init SDL from map dimensions.\n");
                    // exit?
                 }
            }
        }
    }

    pthread_mutex_lock(&cache_lock); // Cache'i yazarken kilitle

    // Drone'ları parse et ve cache'e yaz
    if (json_object_object_get_ex(state_json, "drones", &drones_array_obj)) {
        int count = json_object_array_length(drones_array_obj);
        num_viewer_drones = 0; // Cache'i temizle (veya güncelle)
        for (int i = 0; i < count && i < 50; i++) {
            struct json_object *d_obj = json_object_array_get_idx(drones_array_obj, i);
            ViewerDroneInfo *vd = &viewer_drones_cache[i];
            
            struct json_object *id_str_obj, *coord_obj, *target_obj, *status_str_obj;
            json_object_object_get_ex(d_obj, "id_str", &id_str_obj);
            json_object_object_get_ex(d_obj, "coord", &coord_obj);
            json_object_object_get_ex(d_obj, "target", &target_obj);
            json_object_object_get_ex(d_obj, "status", &status_str_obj);

            if(id_str_obj) strncpy(vd->id_str, json_object_get_string(id_str_obj), sizeof(vd->id_str)-1);
            if(coord_obj) {
                json_object_object_get_ex(coord_obj, "x", &id_str_obj); vd->coord.x = json_object_get_int(id_str_obj);
                json_object_object_get_ex(coord_obj, "y", &id_str_obj); vd->coord.y = json_object_get_int(id_str_obj);
            }
            if(target_obj) {
                json_object_object_get_ex(target_obj, "x", &id_str_obj); vd->target.x = json_object_get_int(id_str_obj);
                json_object_object_get_ex(target_obj, "y", &id_str_obj); vd->target.y = json_object_get_int(id_str_obj);
            }
            if(status_str_obj) {
                const char *s_str = json_object_get_string(status_str_obj);
                if(strcmp(s_str, "IDLE")==0) vd->status = IDLE;
                else if (strcmp(s_str, "ON_MISSION")==0) vd->status = ON_MISSION;
            }
            /* Initialize display position for new drones */
            if (!vd->displayInit) {
                vd->displayCoord = vd->coord;
                vd->displayInit = 1;
            }
            num_viewer_drones++;
        }
    }

    // Survivor'ları parse et ve cache'e yaz
    if (json_object_object_get_ex(state_json, "survivors", &survivors_array_obj)) {
        int count = json_object_array_length(survivors_array_obj);
        num_viewer_survivors = 0; // Cache'i temizle
        for (int i = 0; i < count && i < 100; i++) {
            struct json_object *s_obj = json_object_array_get_idx(survivors_array_obj, i);
            ViewerSurvivorInfo *vs = &viewer_survivors_cache[i];

            struct json_object *info_obj, *coord_obj, *status_str_obj;
            json_object_object_get_ex(s_obj, "info", &info_obj);
            json_object_object_get_ex(s_obj, "coord", &coord_obj);
            json_object_object_get_ex(s_obj, "status", &status_str_obj);
            
            if(info_obj) strncpy(vs->info, json_object_get_string(info_obj), sizeof(vs->info)-1);
            if(coord_obj) {
                struct json_object *x_obj, *y_obj;
                json_object_object_get_ex(coord_obj, "x", &x_obj);
                vs->coord.x = json_object_get_int(x_obj);
                json_object_object_get_ex(coord_obj, "y", &y_obj);
                vs->coord.y = json_object_get_int(y_obj);
            }
            if(status_str_obj) {
                const char *s_str = json_object_get_string(status_str_obj);
                if(strcmp(s_str, "WAITING")==0) vs->status = WAITING;
                else if (strcmp(s_str, "ASSIGNED")==0) vs->status = ASSIGNED;
                else if (strcmp(s_str, "HELPED")==0) vs->status = HELPED; // HELPED de gelebilir
            }
            num_viewer_survivors++;
        }
    }
    pthread_mutex_unlock(&cache_lock);
    // printf("Viewer: Cache updated. Drones: %d, Survivors: %d\n", num_viewer_drones, num_viewer_survivors);
}


int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Initialize caches to zero for displayCoord flags
    memset(viewer_drones_cache, 0, sizeof(viewer_drones_cache));
    memset(viewer_survivors_cache, 0, sizeof(viewer_survivors_cache));

    if (pthread_mutex_init(&cache_lock, NULL) != 0) {
        perror("Viewer: Failed to init cache_lock"); return 1;
    }

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("Viewer: socket creation failed"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Viewer: inet_pton failed"); close(sock_fd); return 1;
    }

    printf("Viewer: Connecting to server %s:%d...\n", SERVER_IP, SERVER_PORT);
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Viewer: connect failed"); close(sock_fd); return 1;
    }
    printf("Viewer: Connected to server.\n");

    // Viewer olarak handshake gönder (sunucunun drone'dan ayırt etmesi için)
    struct json_object *viewer_handshake = json_object_new_object();
    json_object_object_add(viewer_handshake, "type", json_object_new_string("VIEWER_HANDSHAKE"));
    json_object_object_add(viewer_handshake, "viewer_id", json_object_new_string("ViewerAlpha"));
    // Mesaj sonuna \n ekle
    const char *hs_str_raw = json_object_to_json_string_ext(viewer_handshake, JSON_C_TO_STRING_PLAIN);
    char hs_msg_nl[strlen(hs_str_raw) + 2];
    sprintf(hs_msg_nl, "%s\n", hs_str_raw);
    send(sock_fd, hs_msg_nl, strlen(hs_msg_nl), 0);
    json_object_put(viewer_handshake);
    printf("Viewer: Sent VIEWER_HANDSHAKE.\n");


    char aggregate_buffer_viewer[VIEWER_BUFFER_SIZE];
    memset(aggregate_buffer_viewer, 0, sizeof(aggregate_buffer_viewer));
    int aggregate_len_viewer = 0;
    int running = 1;

    // İlk harita boyutlarını almak için bir bekleme veya ilk mesajı düzgün işleme
    // SDL penceresi, harita boyutları bilinmeden açılamaz.
    // Sunucudan ilk SIMULATION_STATE_UPDATE mesajı gelene kadar beklenebilir.
    // Veya handshake_ack içinde map boyutları gönderilebilir.
    // Şimdilik, process_simulation_state içinde ilk kez init ediliyor.

    while(running) {
        if (vc_check_events()) {
            running = 0;
            break;
        }

        // Sunucudan veri al
        // select() kullanarak non-blocking recv
        fd_set read_fds_viewer;
        FD_ZERO(&read_fds_viewer);
        FD_SET(sock_fd, &read_fds_viewer);
        struct timeval tv_viewer;
        tv_viewer.tv_sec = 0;
        tv_viewer.tv_usec = 16666; // ~16ms (60 FPS) check

        int activity = select(sock_fd + 1, &read_fds_viewer, NULL, NULL, &tv_viewer);
        if (activity < 0 && errno != EINTR) { perror("Viewer: select error"); break; }

        if (FD_ISSET(sock_fd, &read_fds_viewer)) {
            if (aggregate_len_viewer >= sizeof(aggregate_buffer_viewer) -1) { /* Buffer dolu, hata */ break; }
            ssize_t bytes_received = recv(sock_fd, aggregate_buffer_viewer + aggregate_len_viewer,
                                          sizeof(aggregate_buffer_viewer) - aggregate_len_viewer - 1, 0);
            if (bytes_received <= 0) { /* Bağlantı kesildi veya hata */ running = 0; break; }
            
            aggregate_len_viewer += bytes_received;
            aggregate_buffer_viewer[aggregate_len_viewer] = '\0';

            char *newline_pos_v;
            while((newline_pos_v = strchr(aggregate_buffer_viewer, '\n')) != NULL) {
                *newline_pos_v = '\0';
                char single_json_str_v[VIEWER_BUFFER_SIZE]; // Her bir JSON mesajı için
                strncpy(single_json_str_v, aggregate_buffer_viewer, sizeof(single_json_str_v)-1);
                single_json_str_v[sizeof(single_json_str_v)-1] = '\0';
                
                memmove(aggregate_buffer_viewer, newline_pos_v + 1, aggregate_len_viewer - (newline_pos_v - aggregate_buffer_viewer + 1));
                aggregate_len_viewer -= (newline_pos_v - aggregate_buffer_viewer + 1);
                aggregate_buffer_viewer[aggregate_len_viewer] = '\0';

                struct json_object *parsed_json = json_tokener_parse(single_json_str_v);
                if (parsed_json) {
                    struct json_object *type_obj_v;
                    if (json_object_object_get_ex(parsed_json, "type", &type_obj_v)) {
                        const char *type_str = json_object_get_string(type_obj_v);
                        if (strcmp(type_str, "SIMULATION_STATE_UPDATE") == 0) {
                            process_simulation_state(parsed_json);
                        } else if (strcmp(type_str, "VIEWER_HANDSHAKE_ACK") == 0) { // Sunucu viewer'ı onaylarsa
                            printf("Viewer: VIEWER_HANDSHAKE_ACK received.\n");
                             // Belki ilk harita boyutları burada gelir.
                        }
                    }
                    json_object_put(parsed_json);
                } else {
                     fprintf(stderr, "Viewer: Failed to parse JSON from server: '%s'\n", single_json_str_v);
                }
                if(strlen(aggregate_buffer_viewer) == 0) break;
            }
        }
        
        if (g_vc_renderer) { // Sadece SDL init edilmişse çiz
            vc_render_all();
        }
        SDL_Delay(16); // ~60 FPS
    }

    printf("Viewer: Disconnecting.\n");
    close(sock_fd);
    vc_quit_sdl();
    pthread_mutex_destroy(&cache_lock);
    return 0;
}