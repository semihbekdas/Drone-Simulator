#ifndef VIEWER_SDL_H // İsim değişikliği, view.h global bir isim olabilir
#define VIEWER_SDL_H
#include <SDL.h>
#include "coord.h" // Coord gibi temel tipler gerekebilir

// Renk Tanımları (viewer_client.c içine de alınabilir)
extern const SDL_Color VC_COLOR_BLACK;
extern const SDL_Color VC_COLOR_RED;    
extern const SDL_Color VC_COLOR_YELLOW; 
extern const SDL_Color VC_COLOR_BLUE;   
extern const SDL_Color VC_COLOR_GREEN;  
extern const SDL_Color VC_COLOR_WHITE;  
extern const SDL_Color VC_COLOR_DARK_GREY;


// Viewer Client'ın kullanacağı SDL fonksiyonları
int vc_init_sdl_window(int map_width, int map_height, int cell_size_pixels);
void vc_draw_cell(int map_x, int map_y, int cell_size_pixels, SDL_Color color);
// Çizim fonksiyonları artık doğrudan global listelere erişmeyecek,
// sunucudan gelen parse edilmiş veriyi alacak.
// void vc_draw_drones_from_data(SDL_Renderer *renderer, DroneData *drones_data, int num_drones, int cell_size);
// void vc_draw_survivors_from_data(SDL_Renderer *renderer, SurvivorData *survivors_data, int num_survivors, int cell_size);
void vc_draw_grid(SDL_Renderer *renderer, int map_width, int map_height, int cell_size_pixels);
void vc_render_all(/* Gerekli veri yapıları buraya */);
int vc_check_events(); 
void vc_quit_sdl();    

#endif