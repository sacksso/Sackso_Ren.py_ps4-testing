#include <Python.h>

#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

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

int main() {
    mkdir("/data/renpy", 0777);
    g_log = fopen("/data/renpy/python-smoke.log", "w");
    if (!g_log) g_log = fopen("/data/renpy_python_smoke.log", "w");
    if (g_log && dup2(fileno(g_log), STDERR_FILENO) < 0) {
        log_message("No se pudo redirigir stderr al log");
    }

    log_message("== CPython PS4 smoke test ==");

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
        status = PyConfig_SetString(&config, &config.base_executable, L"/app0/eboot.bin");
    }
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.prefix, L"/app0");
    }
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.base_prefix, L"/app0");
    }
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.exec_prefix, L"/app0");
    }
    if (!PyStatus_Exception(status)) {
        status = PyConfig_SetString(&config, &config.base_exec_prefix, L"/app0");
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
    log_message("PYTHON_SMOKE_OK result=42");
    Py_DECREF(answer);
    Py_DECREF(version);
    const int finalize_status = Py_FinalizeEx();
    log_message("Py_FinalizeEx = %d", finalize_status);
    if (g_log) fclose(g_log);
    return finalize_status < 0 ? 1 : 0;
}