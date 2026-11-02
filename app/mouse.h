// Raton nativo de la PS4 (libSceMouse). SDL 2.0.9 de la PS4 solo ve el mando, asi que el raton se lee
// aparte y main.cpp lo convierte en eventos SDL (igual que se hizo con el teclado).
// Los prototipos reales de sceMouse* estan en mouse.cpp; el orbis/Mouse.h del toolchain son stubs vacios.
#pragma once
#include <cstdint>

struct PsMouseState {
    int dx;            // desplazamiento desde la ultima lectura
    int dy;
    int wheel;         // rueda acumulada desde la ultima lectura
    uint32_t buttons;  // bit 0 = izquierdo, bit 1 = derecho, bit 2 = central
    bool connected;
};

typedef void (*PsMouseLogFn)(const char* line);

// Abre el raton probando varios userId. Devuelve false si ninguno funciona (el motivo queda en el log).
bool psmouse_init(PsMouseLogFn logger);

// Lee todo lo pendiente y lo acumula en 'out'. Devuelve true si hubo datos nuevos.
bool psmouse_poll(PsMouseState* out);
