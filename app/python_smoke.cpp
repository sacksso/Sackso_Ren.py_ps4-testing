#include <Python.h>

#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

static FILE* g_log = nullptr;

static void log_message(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("%s\n", buffer);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "%s\n", buffer);
        fflush(g_log);
    }
}

static void probe_module(const char* name) {
    PyObject* module = PyImport_ImportModule(name);
    if (module) {
        log_message("MODULE_OK %s", name);
        Py_DECREF(module);
        return;
    }

    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyObject* message = value ? PyObject_Str(value) : nullptr;
    const char* message_text = message ? PyUnicode_AsUTF8(message) : nullptr;
    log_message("MODULE_FAIL %s: %s", name,
                message_text ? message_text : "sin detalle");
    Py_XDECREF(message);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    PyErr_Clear();
}

static void log_object_repr(const char* label, PyObject* object) {
    PyObject* repr = object ? PyObject_Repr(object) : nullptr;
    const char* text = repr ? PyUnicode_AsUTF8(repr) : nullptr;
    log_message("%s=%s", label, text ? text : "unavailable");
    Py_XDECREF(repr);
    PyErr_Clear();
}

static void probe_expression(const char* label, const char* expression, PyObject* globals) {
    PyObject* result = PyRun_String(expression, Py_eval_input, globals, globals);
    if (result) {
        log_object_repr(label, result);
        Py_DECREF(result);
        return;
    }

    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyObject* message = value ? PyObject_Str(value) : nullptr;
    const char* message_text = message ? PyUnicode_AsUTF8(message) : nullptr;
    log_message("%s failed: %s", label,
                message_text ? message_text : "sin detalle");
    Py_XDECREF(message);
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    PyErr_Clear();
}

int main() {
    mkdir("/data/renpy", 0777);
    std::remove("/data/renpy/getpath-error.log");
    std::remove("/data/renpy/import-error.log");
    std::remove("/data/renpy/rlock-error.log");
    std::remove("/data/renpy/fs-codec-error.log");
    g_log = fopen("/data/renpy/python-smoke.log", "w");
    if (!g_log) g_log = fopen("/data/renpy_python_smoke.log", "w");

    log_message("== CPython PS4 smoke test ==");
    FILE* stdlib = fopen("/app0/assets/stdlib.zip", "rb");
    if (!stdlib) {
        log_message("STDLIB_OPEN_FAIL errno=%d (%s)", errno, strerror(errno));
    } else {
        unsigned char signature[4] = {};
        const size_t signature_size = fread(signature, 1, sizeof(signature), stdlib);
        fseek(stdlib, 0, SEEK_END);
        const long stdlib_size = ftell(stdlib);
        log_message("STDLIB_OPEN_OK size=%ld signature=%02x%02x%02x%02x bytes=%zu",
                    stdlib_size, signature[0], signature[1], signature[2], signature[3], signature_size);
        fclose(stdlib);
    }

    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.module_search_paths_set = 1;
    config.use_hash_seed = 1;
    config.hash_seed = 0;

    PyStatus status = PyConfig_SetString(&config, &config.program_name, L"/app0/eboot.bin");
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.executable, L"/app0/eboot.bin");
    }
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.home, L"/app0");
    }
    if (!PyStatus_Exception(status)) {
        status = PyWideStringList_Append(
            &config.module_search_paths, L"/app0/assets/stdlib.zip");
    }
    if (!PyStatus_Exception(status)) {
        status = Py_InitializeFromConfig(&config);
    }

    if (PyStatus_Exception(status)) {
        log_message("Py_InitializeFromConfig fallo: %s (funcion=%s)",
                    status.err_msg ? status.err_msg : "sin detalle",
                    status.func ? status.func : "desconocida");
        PyConfig_Clear(&config);
        if (g_log) fclose(g_log);
        return 1;
    }
    PyConfig_Clear(&config);

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = main_module ? PyModule_GetDict(main_module) : nullptr;
    PyObject* encodings = PyImport_ImportModule("encodings");
    PyObject* version = globals && encodings
        ? PyRun_String("__import__('sys').version", Py_eval_input, globals, globals)
        : nullptr;
    Py_XDECREF(encodings);
    if (!version) {
        log_message("Fallo al importar encodings o sys.version");
        Py_FinalizeEx();
        if (g_log) fclose(g_log);
        return 1;
    }

    const char* version_text = PyUnicode_AsUTF8(version);
    PyObject* answer = PyRun_String("40 + 2", Py_eval_input, globals, globals);
    if (!version_text || !answer || PyLong_AsLong(answer) != 42) {
        log_message("La ejecucion Python devolvio un resultado inesperado");
        Py_XDECREF(answer);
        Py_DECREF(version);
        Py_FinalizeEx();
        if (g_log) fclose(g_log);
        return 1;
    }

    log_message("Python version: %s", version_text);
    Py_DECREF(answer);
    Py_DECREF(version);

    log_object_repr("sys.path", PySys_GetObject("path"));
    log_object_repr("sys.path_hooks", PySys_GetObject("path_hooks"));
    probe_module("zipimport");
    probe_expression("python os.stat size",
                     "__import__('os').stat('/app0/assets/stdlib.zip').st_size", globals);
    probe_expression("python open read",
                     "open('/app0/assets/stdlib.zip', 'rb').read(4)", globals);
    probe_expression("python open seek",
                     "(lambda f: (f.seek(0, 2), f.close()))(open('/app0/assets/stdlib.zip', 'rb'))",
                     globals);
    probe_expression("python os.open readonly",
                     "(lambda os: (lambda fd: (os.close(fd), fd)[1])(os.open('/app0/assets/stdlib.zip', os.O_RDONLY)))(__import__('os'))",
                     globals);
    probe_expression("python os.open cloexec",
                     "(lambda os: (lambda fd: (os.close(fd), fd)[1])(os.open('/app0/assets/stdlib.zip', os.O_RDONLY | getattr(os, 'O_CLOEXEC', 0))))(__import__('os'))",
                     globals);
    PyObject* zip_spec = PyRun_String(
        "__import__('zipimport').zipimporter('/app0/assets/stdlib.zip').find_spec('json')",
        Py_eval_input, globals, globals);
    if (zip_spec) {
        log_object_repr("zipimport json spec", zip_spec);
        Py_DECREF(zip_spec);
    } else {
        PyObject* type = nullptr;
        PyObject* value = nullptr;
        PyObject* traceback = nullptr;
        PyErr_Fetch(&type, &value, &traceback);
        PyObject* message = value ? PyObject_Str(value) : nullptr;
        const char* message_text = message ? PyUnicode_AsUTF8(message) : nullptr;
        log_message("zipimport json spec failed: %s",
                    message_text ? message_text : "sin detalle");
        Py_XDECREF(message);
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        PyErr_Clear();
    }

    static const char* const modules[] = {
        "os", "json", "collections", "copyreg", "math", "_struct", "binascii", "zlib", "select",
        "_socket", "_random", "_datetime", "_json", "array", "unicodedata",
        "_pickle", "_hashlib", "_md5", "_sha1", "_sha2", "hashlib",
    };
    for (const char* module : modules) probe_module(module);

    log_message("PYTHON_SMOKE_OK result=42");
    const int finalize_status = Py_FinalizeEx();
    log_message("Py_FinalizeEx = %d", finalize_status);
    if (g_log) fclose(g_log);
    return finalize_status < 0 ? 1 : 0;
}