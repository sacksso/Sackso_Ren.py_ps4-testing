// Lectura del raton con libSceMouse. VERSION DE DIAGNOSTICO: los valores de userId/type y el layout de
// SceMouseData vienen de memoria, no de los logs; por eso se vuelca el contenido crudo al log.
#include "mouse.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
int sceUserServiceGetInitialUser(int* userId);
int sceMouseInit(void);
int sceMouseOpen(int userId, int type, int index, void* param);
int sceMouseRead(int handle, void* data, int num);
}

// Layout esperado de SceMouseData (40 bytes).
struct RawMouse {
    uint64_t timestamp;
    uint8_t connected;
    uint8_t pad_[3];
    uint32_t buttons;
    int32_t x;
    int32_t y;
    int32_t wheel;
    int32_t tilt;
    uint8_t reserve[8];
};

static PsMouseLogFn g_log = nullptr;
static int g_handle = -1;
static uint32_t g_prevButtons = 0;
static int g_rawLogs = 0;
static unsigned long g_polls = 0;
static int g_lastRet = 12345;

static void L(const char* fmt, ...) {
    if (!g_log) return;
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log(buf);
}

static void dumpRaw(const uint8_t* b, int n) {
    char hex[3 * 64 + 1];
    int p = 0;
    for (int i = 0; i < n && i < 64; i++) p += snprintf(hex + p, sizeof(hex) - p, "%02x ", b[i]);
    L("raton crudo: %s", hex);
}

bool psmouse_init(PsMouseLogFn logger) {
    g_log = logger;
    int rc = sceMouseInit();
    L("sceMouseInit = 0x%x", static_cast<unsigned>(rc));

    int initial = -1;
    int urc = sceUserServiceGetInitialUser(&initial);
    L("sceUserServiceGetInitialUser = 0x%x (userId=%d)", static_cast<unsigned>(urc), initial);

    const int users[] = {initial, 0xFE, -1, 0};  // 0xFE = usuario "system" en el SDK de Sony
    for (int u : users) {
        int h = sceMouseOpen(u, 0, 0, nullptr);  // type 0 = puerto estandar, index 0
        L("sceMouseOpen(user=%d, type=0, index=0) = 0x%x", u, static_cast<unsigned>(h));
        if (h >= 0) {
            g_handle = h;
            L("raton abierto: handle=%d", h);
            return true;
        }
    }
    L("raton: ningun userId funciono");
    return false;
}

bool psmouse_poll(PsMouseState* out) {
    std::memset(out, 0, sizeof(*out));
    if (g_handle < 0) return false;
    g_polls++;
    bool got = false;
    for (int i = 0; i < 16; i++) {
        uint8_t buf[64];
        std::memset(buf, 0, sizeof(buf));
        int n = sceMouseRead(g_handle, buf, 1);
        if (n != g_lastRet) {  // solo se loguea cuando cambia el codigo de retorno
            g_lastRet = n;
            L("sceMouseRead devuelve 0x%x", static_cast<unsigned>(n));
        }
        if (n <= 0) break;
        const RawMouse* r = reinterpret_cast<const RawMouse*>(buf);
        if (g_rawLogs < 10 || r->buttons != g_prevButtons) {
            g_rawLogs++;
            dumpRaw(buf, 40);
            L("raton parseado: conectado=%d botones=0x%x x=%d y=%d rueda=%d", r->connected, r->buttons, r->x, r->y,
              r->wheel);
        }
        g_prevButtons = r->buttons;
        out->dx += r->x;
        out->dy += r->y;
        out->wheel += r->wheel;
        out->buttons = r->buttons;
        out->connected = r->connected != 0;
        got = true;
    }
    return got;
}
