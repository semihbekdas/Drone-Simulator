#ifndef CONNECTION_HANDLING_H
#define CONNECTION_HANDLING_H

#include <pthread.h>

struct handler_args {
    int client_fd;
    char initial_msg[1024]; // BUFFER_SIZE ile uyumlu olmalı
};

void* handle_drone_connection(void* arg);
void* handle_viewer_connection(void* arg);

#endif // CONNECTION_HANDLING_H
