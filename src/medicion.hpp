/**
 * @file medicion.hpp
 * @brief Mediciones del proceso y de la máquina para detectar interferencia en los tiempos.
 *
 * El tiempo de reloj de una construcción puede crecer por causas ajenas al algoritmo:
 * otros procesos que usan la CPU, falta de memoria (swap) o, en una máquina virtual, el
 * hipervisor. Para detectarlo se registra, junto al tiempo de reloj:
 *  - el tiempo de CPU del proceso (si el reloj supera bastante a la CPU, el proceso
 *    estuvo esperando);
 *  - los cambios de contexto involuntarios (el kernel le quitó la CPU al proceso);
 *  - los fallos de página mayores (páginas que hubo que leer del disco, p. ej. swap);
 *  - la presión de CPU y de memoria del kernel (PSI, /proc/pressure): tiempo en que
 *    alguna tarea estuvo detenida esperando CPU o memoria;
 *  - el tiempo robado por el hipervisor (steal, /proc/stat).
 * Si algún dato no está disponible en el sistema, se registra -1.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/resource.h>
#include <unistd.h>

/// @brief Valores acumulados en un instante (se restan dos muestras para obtener un intervalo).
struct Muestra {
    double cpu = 0;           ///< Tiempo de CPU del proceso, usuario + sistema (s).
    int64_t involuntarios = 0;  ///< Cambios de contexto involuntarios del proceso.
    int64_t mayores = 0;      ///< Fallos de página mayores del proceso.
    int64_t psiCpuUs = -1;    ///< PSI de CPU, "some total" (µs); -1 si no hay PSI.
    int64_t psiMemUs = -1;    ///< PSI de memoria, "some total" (µs); -1 si no hay PSI.
    int64_t steal = -1;       ///< Tiempo robado por el hipervisor (ticks de reloj); -1 si no se pudo leer.
};

/// @brief Diferencia entre dos muestras: interferencia observada durante un intervalo.
struct Interferencia {
    double cpu = 0;            ///< Tiempo de CPU del proceso (s).
    int64_t involuntarios = 0;  ///< Cambios de contexto involuntarios.
    int64_t mayores = 0;       ///< Fallos de página mayores.
    double psiCpuMs = -1;      ///< Tiempo con tareas esperando CPU (ms), o -1.
    double psiMemMs = -1;      ///< Tiempo con tareas esperando memoria (ms), o -1.
    double stealMs = -1;       ///< Tiempo robado por el hipervisor (ms, sumado sobre las CPU), o -1.
};

/**
 * @brief Lee el "some total" de un archivo PSI.
 * @param ruta /proc/pressure/cpu o /proc/pressure/memory (entrada).
 * @return microsegundos acumulados, o -1 si el archivo no existe o no tiene el formato esperado.
 */
inline int64_t leerPresion(const char* ruta) {
    std::FILE* f = std::fopen(ruta, "r");
    if (f == nullptr) return -1;
    char linea[256];
    int64_t total = -1;
    if (std::fgets(linea, sizeof linea, f) != nullptr && std::strncmp(linea, "some", 4) == 0) {
        const char* p = std::strstr(linea, "total=");
        if (p != nullptr) total = std::strtoll(p + 6, nullptr, 10);
    }
    std::fclose(f);
    return total;
}

/**
 * @brief Lee el tiempo robado por el hipervisor (columna steal de la línea "cpu" de /proc/stat).
 * @return ticks acumulados de todas las CPU, o -1 si no se pudo leer.
 */
inline int64_t leerSteal() {
    std::FILE* f = std::fopen("/proc/stat", "r");
    if (f == nullptr) return -1;
    unsigned long long v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int n = std::fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4],
                              &v[5], &v[6], &v[7]);
    std::fclose(f);
    return n == 8 ? static_cast<int64_t>(v[7]) : -1;
}

/**
 * @brief Tiempo de CPU del proceso (usuario + sistema).
 * @return segundos.
 */
inline double tiempoCpu() {
    struct rusage u;
    ::getrusage(RUSAGE_SELF, &u);
    return static_cast<double>(u.ru_utime.tv_sec + u.ru_stime.tv_sec) +
           static_cast<double>(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

/**
 * @brief Toma una muestra de todos los contadores.
 * @return valores acumulados en este instante.
 */
inline Muestra tomarMuestra() {
    Muestra m;
    struct rusage u;
    ::getrusage(RUSAGE_SELF, &u);
    m.cpu = static_cast<double>(u.ru_utime.tv_sec + u.ru_stime.tv_sec) +
            static_cast<double>(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
    m.involuntarios = u.ru_nivcsw;
    m.mayores = u.ru_majflt;
    m.psiCpuUs = leerPresion("/proc/pressure/cpu");
    m.psiMemUs = leerPresion("/proc/pressure/memory");
    m.steal = leerSteal();
    return m;
}

/**
 * @brief Calcula la interferencia entre dos muestras.
 * @param a muestra al inicio del intervalo (entrada).
 * @param b muestra al final del intervalo (entrada).
 * @return diferencias b - a (los campos sin datos quedan en -1).
 */
inline Interferencia diferencia(const Muestra& a, const Muestra& b) {
    Interferencia d;
    d.cpu = b.cpu - a.cpu;
    d.involuntarios = b.involuntarios - a.involuntarios;
    d.mayores = b.mayores - a.mayores;
    if (a.psiCpuUs >= 0 && b.psiCpuUs >= 0) d.psiCpuMs = static_cast<double>(b.psiCpuUs - a.psiCpuUs) / 1000.0;
    if (a.psiMemUs >= 0 && b.psiMemUs >= 0) d.psiMemMs = static_cast<double>(b.psiMemUs - a.psiMemUs) / 1000.0;
    if (a.steal >= 0 && b.steal >= 0) {
        const long hz = ::sysconf(_SC_CLK_TCK);
        d.stealMs = static_cast<double>(b.steal - a.steal) * 1000.0 / static_cast<double>(hz > 0 ? hz : 100);
    }
    return d;
}

/**
 * @brief Memoria disponible según el kernel (MemAvailable de /proc/meminfo).
 * @return MiB disponibles, o -1 si no se pudo leer.
 */
inline int64_t memoriaDisponibleMiB() {
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (f == nullptr) return -1;
    char linea[256];
    int64_t kib = -1;
    while (std::fgets(linea, sizeof linea, f) != nullptr) {
        if (std::strncmp(linea, "MemAvailable:", 13) == 0) {
            kib = std::strtoll(linea + 13, nullptr, 10);
            break;
        }
    }
    std::fclose(f);
    return kib < 0 ? -1 : kib / 1024;
}
