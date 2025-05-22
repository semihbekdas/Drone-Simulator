CC      := gcc
CFLAGS  := -g -Wall -pthread

# Kaynak dosyalar
COMMON_SRCS_FOR_SERVER := list.c map.c survivor.c ai.c globals.c drone.c
SERVER_SRCS := server.c $(COMMON_SRCS_FOR_SERVER)
DRONE_CLIENT_SRCS := drone_client/drone_client.c
VIEWER_CLIENT_SRCS := viewer_client.c

# Hedefler
SERVER_TARGET  := server
DRONE_CLIENT_TARGET := drone_client_exec
VIEWER_CLIENT_TARGET := viewer_client_exec

UNAME_S := $(shell uname -s)

LINKER_FLAGS := -lm
JSONC_LDFLAGS := -L/opt/homebrew/Cellar/json-c/0.18/lib -ljson-c
JSONC_CFLAGS := -I/opt/homebrew/Cellar/json-c/0.18/include/json-c

# SDL2 flags (manuel yazıldı çünkü which sdl2-config boş dönüyor)
SDLCFLAGS  := -I/opt/homebrew/Cellar/sdl2/2.32.6/include/SDL2
SDLLDFLAGS := -L/opt/homebrew/Cellar/sdl2/2.32.6/lib -lSDL2


.PHONY: all server_target client_target viewer_target clean run_server run_client run_viewer

all: server_target client_target viewer_target

server_target: $(SERVER_TARGET)
$(SERVER_TARGET): $(SERVER_SRCS)
	@echo "Compiling Server ($(SERVER_TARGET))..."
	$(CC) $(CFLAGS) $(JSONC_CFLAGS) $^ $(SDLCFLAGS) -o $@ $(SDLLDFLAGS) $(JSONC_LDFLAGS) $(LINKER_FLAGS)
	@echo "Server compiled successfully."

client_target: $(DRONE_CLIENT_TARGET)
$(DRONE_CLIENT_TARGET): $(DRONE_CLIENT_SRCS)
	@echo "Compiling Drone Client ($(DRONE_CLIENT_TARGET))..."
	$(CC) $(CFLAGS) $(JSONC_CFLAGS) $^ -o $@ $(JSONC_LDFLAGS) $(LINKER_FLAGS)
	@echo "Drone Client compiled successfully."

viewer_target: $(VIEWER_CLIENT_TARGET)
$(VIEWER_CLIENT_TARGET): $(VIEWER_CLIENT_SRCS)
	@echo "Compiling Viewer Client ($(VIEWER_CLIENT_TARGET))..."
	$(CC) $(CFLAGS) -I./headers $(JSONC_CFLAGS) $(SDLCFLAGS) $^ -o $@ $(SDLLDFLAGS) $(JSONC_LDFLAGS) $(LINKER_FLAGS)
	@echo "Viewer Client compiled successfully."

run_server: server_target
	@echo "Running Server..."
	./$(SERVER_TARGET)

run_client: client_target
	@echo "Running Drone Client..."
	./$(DRONE_CLIENT_TARGET) 1

run_viewer: viewer_target
	@echo "Running Viewer Client..."
	./$(VIEWER_CLIENT_TARGET)

clean:
	@echo "Cleaning up..."
	rm -f $(SERVER_TARGET) $(DRONE_CLIENT_TARGET) $(VIEWER_CLIENT_TARGET) *.o
	@echo "Cleanup complete."
