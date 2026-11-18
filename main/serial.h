#pragma once

#include <stdbool.h>

/* The QL's serial ports as TCP ports 2301 (SER1) and 2302 (SER2) */
void serial_start(void);
bool serial_connected(int port);
int serial_tcp_port(int port);
