#include <SDL2/SDL.h>
#include "headers/globals.h"  // map, drone_fleet, num_drones, map.cells[][].survivors listesi
#include "headers/drone.h"    // Drone tipi, DroneState
#include "headers/map.h"      // Map tipi (globals.h'den de gelebilir)
#include "headers/survivor.h" // Survivor tipi, SurvivorState
#include "headers/list.h"     // List ve Node tipleri (globals.h'den de gelebilir)

#define CELL_SIZE 20  // Her harita hücresinin piksel boyutu

// SDL Global Değişkenleri (sadece view.c içinde kullanılır)
SDL_Window* sdl_window = NULL;   // İsim çakışmasını önlemek için 'sdl_' prefixi eklendi
SDL_Renderer* sdl_renderer = NULL; // İsim çakışmasını önlemek için 'sdl_' prefixi eklendi
SDL_Event sdl_event;             // İsim çakışmasını önlemek için 'sdl_' prefixi eklendi
int window_render_width, window_render_height; // Pencere boyutları

// Renk Tanımları
const SDL_Color COLOR_BLACK = {0, 0, 0, 255};
const SDL_Color COLOR_RED = {255, 0, 0, 255};         // WAITING survivor
const SDL_Color COLOR_YELLOW = {255, 255, 0, 255};    // ASSIGNED survivor
const SDL_Color COLOR_BLUE = {0, 100, 255, 255};      // IDLE drone
const SDL_Color COLOR_GREEN = {0, 200, 0, 255};       // ON_MISSION drone
const SDL_Color COLOR_WHITE = {200, 200, 200, 255};   // Grid lines
const SDL_Color COLOR_DARK_GREY = {50, 50, 50, 255};  // Background


int init_sdl_window() {
    if (map.width <= 0 || map.height <= 0) {
        fprintf(stderr, "SDL_Init Error: Map dimensions not set or invalid before SDL init.\n");
        return 1;
    }
    window_render_width = map.width * CELL_SIZE;
    window_render_height = map.height * CELL_SIZE;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    sdl_window = SDL_CreateWindow("Emergency Drone Simulator - Phase 1", 
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 
                                window_render_width,
                                window_render_height, 
                                SDL_WINDOW_SHOWN);
    if (!sdl_window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) {
        SDL_DestroyWindow(sdl_window);
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    printf("SDL initialized successfully. Window: %dx%d\n", window_render_width, window_render_height);
    return 0;
}

/**
 * @brief Draws a single cell on the map with a given color.
 * @param map_x Harita satır indeksi (0'dan başlar)
 * @param map_y Harita sütun indeksi (0'dan başlar)
 * @param color Hücrenin rengi
 */
void draw_cell(int map_x, int map_y, SDL_Color color) {
    if (map_x < 0 || map_x >= map.height || map_y < 0 || map_y >= map.width) return; // Sınır kontrolü

    SDL_Rect cell_rect = {
        .x = map_y * CELL_SIZE,  // SDL x koordinatı haritanın sütununa bağlı
        .y = map_x * CELL_SIZE,  // SDL y koordinatı haritanın satırına bağlı
        .w = CELL_SIZE,
        .h = CELL_SIZE
    };
    SDL_SetRenderDrawColor(sdl_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(sdl_renderer, &cell_rect);
}

void draw_drones() {
    if (!drone_fleet) return; // drone_fleet henüz initialize edilmemişse çizme

    for (int i = 0; i < num_drones; i++) {
        Drone *d = &drone_fleet[i];
        pthread_mutex_lock(&d->lock);

        // Sadece aktif ve görünür droneları çiz
        if (d->status != ON_MISSION || (d->visible == 0)) {
            pthread_mutex_unlock(&d->lock);
            continue;
        }

        SDL_Color drone_color = COLOR_GREEN;
        draw_cell(d->coord.x, d->coord.y, drone_color);

        // Eğer görevdeyse, hedefine doğru bir çizgi çiz
        if (d->status == ON_MISSION) {
            SDL_SetRenderDrawColor(sdl_renderer, COLOR_GREEN.r, COLOR_GREEN.g, COLOR_GREEN.b, COLOR_GREEN.a);
            
            // Çizgi drone'un merkezinden hedefin merkezine
            int start_render_x = d->coord.y * CELL_SIZE + CELL_SIZE / 2;
            int start_render_y = d->coord.x * CELL_SIZE + CELL_SIZE / 2;
            int end_render_x = d->target.y * CELL_SIZE + CELL_SIZE / 2;
            int end_render_y = d->target.x * CELL_SIZE + CELL_SIZE / 2;

            SDL_RenderDrawLine(sdl_renderer, start_render_x, start_render_y, end_render_x, end_render_y);
        }
        pthread_mutex_unlock(&d->lock);
    }
}

void draw_survivors() {
    if (!map.cells) return; // map initialize edilmemişse çizme

    for (int r = 0; r < map.height; r++) {
        for (int c = 0; c < map.width; c++) {
            // Her harita hücresindeki survivor listesini kontrol et
            List *cell_survivor_list = map.cells[r][c].survivors;
            if (!cell_survivor_list) continue;

            pthread_mutex_lock(&cell_survivor_list->lock); // Hücre listesini kilitle
            
            if (cell_survivor_list->number_of_elements > 0) {
                // Bu hücrede en az bir survivor var. Durumuna göre renklendir.
                // Birden fazla survivor varsa, en "acil" olanın rengini gösterebiliriz
                // veya sadece bir işaret koyabiliriz. Şimdilik, HELPED olmayan ilk survivor'ın durumuna göre renklendirelim.
                Node *current_node = cell_survivor_list->head;
                SDL_Color cell_color = COLOR_DARK_GREY; // Varsayılan hücre rengi (arkaplan)
                int survivor_drawn_for_cell = 0;

                while (current_node != NULL) {
                    Survivor *s = *(Survivor**)current_node->data; // node->data Survivor* içerir
                    if (s) { // Null check
                        if (s->status == WAITING) {
                            cell_color = COLOR_RED;
                            survivor_drawn_for_cell = 1;
                            break; // Kırmızı en öncelikli, bu hücreyi kırmızı yap
                        } else if (s->status == ASSIGNED) {
                            cell_color = COLOR_YELLOW; // Kırmızı yoksa sarı olabilir
                            survivor_drawn_for_cell = 1;
                            // Devam et, belki WAITING olan vardır.
                        }
                        // HELPED olanlar için özel bir renk çizmiyoruz, hücre boş gibi görünecek
                        // veya varsayılan arkaplan renginde olacak.
                    }
                    current_node = current_node->next;
                }
                
                if (survivor_drawn_for_cell) {
                     draw_cell(r, c, cell_color);
                }
            }
            pthread_mutex_unlock(&cell_survivor_list->lock);
        }
    }
}


void draw_grid() {
    SDL_SetRenderDrawColor(sdl_renderer, COLOR_WHITE.r, COLOR_WHITE.g, COLOR_WHITE.b, 50); // Grid çizgileri için soluk beyaz
    
    // Yatay çizgiler
    for (int i = 0; i <= map.height; i++) {
        SDL_RenderDrawLine(sdl_renderer, 0, i * CELL_SIZE, window_render_width, i * CELL_SIZE);
    }
    // Dikey çizgiler
    for (int j = 0; j <= map.width; j++) {
        SDL_RenderDrawLine(sdl_renderer, j * CELL_SIZE, 0, j * CELL_SIZE, window_render_height);
    }
}

int draw_map() {
    // Arka planı temizle (koyu gri)
    SDL_SetRenderDrawColor(sdl_renderer, COLOR_DARK_GREY.r, COLOR_DARK_GREY.g, COLOR_DARK_GREY.b, COLOR_DARK_GREY.a);
    SDL_RenderClear(sdl_renderer);

    draw_grid();       // Izgarayı çiz
    draw_survivors();  // Hayatta kalanları çiz
    draw_drones();     // Drone'ları çiz

    SDL_RenderPresent(sdl_renderer); // Çizilenleri ekrana bas
    return 0;
}

int check_events() {
    while (SDL_PollEvent(&sdl_event)) { // event yerine sdl_event kullanılıyor
        if (sdl_event.type == SDL_QUIT) {
            printf("SDL_QUIT event received.\n");
            return 1; // Programın sonlanması gerektiğini belirt
        }
        if (sdl_event.type == SDL_KEYDOWN) {
            if (sdl_event.key.keysym.sym == SDLK_ESCAPE) {
                printf("ESCAPE key pressed.\n");
                return 1; // Programın sonlanması gerektiğini belirt
            }
        }
    }
    return 0; // Devam et
}

void quit_all() {
    printf("Quitting SDL...\n");
    if (sdl_renderer) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }
    SDL_Quit();
    printf("SDL quit completed.\n");
}