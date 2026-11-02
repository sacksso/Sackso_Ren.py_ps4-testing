// Ren'Py PS4 - paso 2: hello world de SDL2 con diagnostico.
//
// Objetivo: saber que ve realmente SDL en la PS4 (video, audio, teclado, raton, mando) sin adivinar.
//  - Escribe un log en /data/renpy/log.txt (si no se puede, en /data/renpy_log.txt) y tambien por stdout.
//  - Pantalla: tres cuadrados que se iluminan ~1 s cuando llega un evento de teclado / raton / mando.
//    Un cuarto cuadrado parpadea al ritmo del pitido de audio.
//  - Audio: pitido de 440 Hz durante 0,5 s cada 2 s, por SDL_OpenAudioDevice.
//  - Cada 5 s escribe un latido con contadores de eventos.
//  - Teclado: SDL (2.0.9 de la PS4) solo ve el mando, asi que el teclado se lee con la API nativa (libSceKeyboard)
//    reutilizando la clase Keyboard del sample 'keyboard' de OpenOrbis (el workflow copia keyboard.h/.cpp).
//    Esto es temporal: despues se convertira en eventos de SDL para pygame_sdl2.

#include <SDL2/SDL.h>

#include <algorithm>
#include <sstream>
#include <vector>

#include "keyboard.h"  // copiado por el workflow desde samples/keyboard/keyboard/
#include "mouse.h"     // raton nativo (libSceMouse), ver app/mouse.cpp

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

// Lo piden los .h/.cpp de los samples de OpenOrbis (log.h): ellos lo declaran y el programa lo define.
std::stringstream debugLogStream;

static FILE* g_log = nullptr;

static void logf_(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[renpy-ps4] %s\n", buf);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

static void mouseLog(const char* line) {
    logf_("%s", line);
}

static void openLog() {
    mkdir("/data/renpy", 0777);
    g_log = fopen("/data/renpy/log.txt", "w");
    if (!g_log) g_log = fopen("/data/renpy_log.txt", "w");
}

// ---- Audio: pitido periodico ----
struct Tone {
    double phase;
    uint64_t n;
    int freq;
    volatile int beepOn;
};

static void audioCb(void* ud, Uint8* stream, int len) {
    Tone* t = static_cast<Tone*>(ud);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    const int frames = len / static_cast<int>(sizeof(int16_t)) / 2;
    const double step = 2.0 * M_PI * 440.0 / static_cast<double>(t->freq);
    for (int i = 0; i < frames; i++) {
        const uint64_t pos = t->n % (2ULL * static_cast<uint64_t>(t->freq));
        const bool on = pos < static_cast<uint64_t>(t->freq / 2);
        int16_t v = 0;
        if (on) v = static_cast<int16_t>(std::sin(t->phase) * 0.2 * 32767.0);
        t->phase += step;
        if (t->phase > 2.0 * M_PI) t->phase -= 2.0 * M_PI;
        out[2 * i] = v;
        out[2 * i + 1] = v;
        t->n++;
    }
    t->beepOn = ((t->n % (2ULL * static_cast<uint64_t>(t->freq))) < static_cast<uint64_t>(t->freq / 2)) ? 1 : 0;
}

static void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 R, Uint8 G, Uint8 B) {
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    openLog();
    logf_("== renpy-ps4 hello (paso 2) ==");
    {
        SDL_version v;
        SDL_GetVersion(&v);
        logf_("SDL en ejecucion: %d.%d.%d", v.major, v.minor, v.patch);
    }

    // Subsistemas por separado: asi sabemos cual falla.
    SDL_Init(0);
    const struct { Uint32 f; const char* n; } subs[] = {
        {SDL_INIT_VIDEO, "VIDEO"},
        {SDL_INIT_AUDIO, "AUDIO"},
        {SDL_INIT_JOYSTICK, "JOYSTICK"},
        {SDL_INIT_EVENTS, "EVENTS"},
    };
    for (const auto& s : subs) {
        const int rc = SDL_InitSubSystem(s.f);
        logf_("SDL_InitSubSystem(%s) = %d %s", s.n, rc, rc ? SDL_GetError() : "ok");
    }

    // Drivers disponibles.
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++) logf_("driver video %d: %s", i, SDL_GetVideoDriver(i));
    logf_("driver video actual: %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(ninguno)");
    for (int i = 0; i < SDL_GetNumAudioDrivers(); i++) logf_("driver audio %d: %s", i, SDL_GetAudioDriver(i));
    logf_("driver audio actual: %s", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(ninguno)");
    logf_("dispositivos de audio de salida: %d", SDL_GetNumAudioDevices(0));

    // Ventana y renderer (si el acelerado falla, se prueba por software).
    SDL_Window* win = SDL_CreateWindow("RenPy PS4", 0, 0, 1920, 1080, 0);
    if (!win) logf_("SDL_CreateWindow fallo: %s", SDL_GetError());
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, 0) : nullptr;
    if (!ren && win) {
        logf_("SDL_CreateRenderer(0) fallo: %s; pruebo software", SDL_GetError());
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        logf_("Sin renderer: %s", SDL_GetError());
    } else {
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(ren, &ri) == 0) logf_("renderer: %s (flags 0x%x)", ri.name, ri.flags);
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        logf_("salida del renderer: %dx%d", w, h);
    }

    // Mandos.
    logf_("joysticks: %d", SDL_NumJoysticks());
    SDL_Joystick* joy = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick* j = SDL_JoystickOpen(i);
        logf_("joystick %d: %s (%s)", i, SDL_JoystickName(j) ? SDL_JoystickName(j) : "?", j ? "abierto" : "no abre");
        if (!joy) joy = j;
    }

    // Teclado nativo (libSceKeyboard).
    Keyboard* kb = new Keyboard();
    const bool kbOk = kb->Init(-1);
    logf_("Keyboard::Init(-1) = %s", kbOk ? "ok" : "FALLO");
    std::vector<uint16_t> prevKeys;

    // Raton nativo (libSceMouse).
    const bool mouseOk = psmouse_init(mouseLog);
    logf_("psmouse_init = %s", mouseOk ? "ok" : "FALLO");
    uint32_t prevMouseButtons = 0;

    // Audio.
    Tone tone;
    std::memset(&tone, 0, sizeof(tone));
    tone.freq = 48000;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = tone.freq;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCb;
    want.userdata = &tone;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (adev == 0) {
        logf_("SDL_OpenAudioDevice fallo: %s", SDL_GetError());
    } else {
        logf_("audio abierto: id=%u freq=%d canales=%d formato=0x%x muestras=%d", adev, have.freq, have.channels,
              have.format, have.samples);
        tone.freq = have.freq;
        SDL_PauseAudioDevice(adev, 0);
    }

    // Bucle principal.
    bool running = true;
    Uint32 lastKb = 0, lastMouse = 0, lastPad = 0, lastBeat = SDL_GetTicks();
    unsigned long cKb = 0, cMouse = 0, cPad = 0, cOther = 0, frames = 0;
    int mx = 960, my = 540;
    bool seenType[0x10000];
    std::memset(seenType, 0, sizeof(seenType));

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const Uint32 now = SDL_GetTicks();
            switch (e.type) {
                case SDL_QUIT:
                    logf_("SDL_QUIT");
                    running = false;
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                    cKb++;
                    lastKb = now;
                    logf_("tecla %s: scancode=%d nombre=%s", e.type == SDL_KEYDOWN ? "abajo" : "arriba",
                          static_cast<int>(e.key.keysym.scancode), SDL_GetKeyName(e.key.keysym.sym));
                    break;
                case SDL_TEXTINPUT:
                    cKb++;
                    lastKb = now;
                    logf_("texto: %s", e.text.text);
                    break;
                case SDL_MOUSEMOTION:
                    cMouse++;
                    lastMouse = now;
                    mx = e.motion.x;
                    my = e.motion.y;
                    if (cMouse <= 5 || cMouse % 200 == 0) logf_("raton mueve: x=%d y=%d (n=%lu)", mx, my, cMouse);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    cMouse++;
                    lastMouse = now;
                    logf_("raton boton %d %s", e.button.button, e.type == SDL_MOUSEBUTTONDOWN ? "abajo" : "arriba");
                    break;
                case SDL_MOUSEWHEEL:
                    cMouse++;
                    lastMouse = now;
                    logf_("raton rueda: %d", e.wheel.y);
                    break;
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                    cPad++;
                    lastPad = now;
                    logf_("mando boton %d %s", e.jbutton.button, e.type == SDL_JOYBUTTONDOWN ? "abajo" : "arriba");
                    break;
                case SDL_JOYHATMOTION:
                    cPad++;
                    lastPad = now;
                    logf_("mando cruceta: 0x%x", e.jhat.value);
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.value > 16000 || e.jaxis.value < -16000) {
                        cPad++;
                        lastPad = now;
                        if (cPad % 20 == 0) logf_("mando eje %d = %d", e.jaxis.axis, e.jaxis.value);
                    }
                    break;
                default:
                    cOther++;
                    if (e.type < 0x10000 && !seenType[e.type]) {
                        seenType[e.type] = true;
                        logf_("evento de otro tipo visto por primera vez: 0x%x", e.type);
                    }
                    break;
            }
        }

        const Uint32 now = SDL_GetTicks();

        // Teclado nativo: se compara el conjunto de teclas pulsadas con el del frame anterior.
        if (kbOk) {
            std::vector<uint16_t> cur;
            for (uint16_t k : kb->Keys()) cur.push_back(k);
            for (uint16_t k : cur) {
                if (std::find(prevKeys.begin(), prevKeys.end(), k) == prevKeys.end()) {
                    cKb++;
                    lastKb = now;
                    logf_("tecla nativa abajo: codigo=%u char=%d", static_cast<unsigned>(k), kb->Key2Char(k));
                }
            }
            for (uint16_t k : prevKeys) {
                if (std::find(cur.begin(), cur.end(), k) == cur.end()) {
                    cKb++;
                    lastKb = now;
                    logf_("tecla nativa arriba: codigo=%u", static_cast<unsigned>(k));
                }
            }
            prevKeys.swap(cur);
        }

        // Raton nativo: se convierte en eventos SDL para que el resto (y luego pygame_sdl2) lo vea como un raton normal.
        if (mouseOk) {
            PsMouseState ms;
            if (psmouse_poll(&ms)) {
                if (ms.dx || ms.dy) {
                    mx = std::max(0, std::min(1919, mx + ms.dx));
                    my = std::max(0, std::min(1079, my + ms.dy));
                    SDL_Event ev;
                    SDL_zero(ev);
                    ev.type = SDL_MOUSEMOTION;
                    ev.motion.x = mx;
                    ev.motion.y = my;
                    ev.motion.xrel = ms.dx;
                    ev.motion.yrel = ms.dy;
                    SDL_PushEvent(&ev);
                }
                const struct { uint32_t bit; Uint8 sdl; } map[] = {
                    {1, SDL_BUTTON_LEFT}, {2, SDL_BUTTON_RIGHT}, {4, SDL_BUTTON_MIDDLE}};
                for (const auto& m : map) {
                    if ((ms.buttons ^ prevMouseButtons) & m.bit) {
                        const bool down = (ms.buttons & m.bit) != 0;
                        SDL_Event ev;
                        SDL_zero(ev);
                        ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                        ev.button.button = m.sdl;
                        ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
                        ev.button.x = mx;
                        ev.button.y = my;
                        SDL_PushEvent(&ev);
                    }
                }
                prevMouseButtons = ms.buttons;
                if (ms.wheel) {
                    SDL_Event ev;
                    SDL_zero(ev);
                    ev.type = SDL_MOUSEWHEEL;
                    ev.wheel.y = ms.wheel;
                    SDL_PushEvent(&ev);
                }
            }
        }

        if (now - lastBeat >= 5000) {
            lastBeat = now;
            logf_("latido: frames=%lu teclado=%lu raton=%lu mando=%lu otros=%lu", frames, cKb, cMouse, cPad, cOther);
        }

        if (ren) {
            SDL_SetRenderDrawColor(ren, 20, 20, 30, 255);
            SDL_RenderClear(ren);
            const bool kbOn = lastKb && now - lastKb < 1000;
            const bool msOn = lastMouse && now - lastMouse < 1000;
            const bool padOn = lastPad && now - lastPad < 1000;
            fillRect(ren, 160, 400, 240, 240, kbOn ? 255 : 60, kbOn ? 80 : 20, kbOn ? 80 : 20);          // teclado: rojo
            fillRect(ren, 560, 400, 240, 240, msOn ? 80 : 20, msOn ? 255 : 60, msOn ? 80 : 20);          // raton: verde
            fillRect(ren, 960, 400, 240, 240, padOn ? 80 : 20, padOn ? 120 : 20, padOn ? 255 : 60);      // mando: azul
            fillRect(ren, 1360, 400, 240, 240, tone.beepOn ? 255 : 60, tone.beepOn ? 220 : 60, 0);       // audio: amarillo
            if (cMouse) fillRect(ren, mx - 8, my - 8, 16, 16, 255, 255, 255);                             // cursor
            SDL_RenderPresent(ren);
        }
        frames++;
        SDL_Delay(16);
    }

    logf_("saliendo");
    if (adev) SDL_CloseAudioDevice(adev);
    if (joy) SDL_JoystickClose(joy);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    if (g_log) fclose(g_log);
    return 0;
}
