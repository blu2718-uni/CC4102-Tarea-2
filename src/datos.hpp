/**
 * @file datos.hpp
 * @brief Lectura secuencial, por bloques, de los pares de datos.bin.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "par.hpp"

/**
 * @brief Calcula cuántos pares completos tiene un archivo de datos.
 * @param ruta ruta del archivo binario de pares (entrada).
 * @return tamaño del archivo dividido en 8 bytes.
 */
inline uint64_t paresEnArchivo(const std::string& ruta) {
    struct stat st;
    if (::stat(ruta.c_str(), &st) != 0) throw std::runtime_error("no se encontró el archivo de datos " + ruta);
    return static_cast<uint64_t>(st.st_size) / sizeof(Par);
}

/**
 * @brief Lector secuencial de los primeros N pares de datos.bin.
 *
 * Lee por bloques (por ejemplo, de 65536 pares) para no cargar el archivo
 * completo en RAM. Los pares se insertan después uno a uno en el árbol.
 */
class LectorDatos {
public:
    /**
     * @brief Abre el archivo y comprueba que tenga al menos maxPares pares.
     * @param ruta     ruta de datos.bin (entrada).
     * @param maxPares cantidad de pares que se van a leer desde el inicio (entrada).
     */
    LectorDatos(const std::string& ruta, uint64_t maxPares) : restantes_(maxPares) {
        const uint64_t disponibles = paresEnArchivo(ruta);
        if (disponibles < maxPares) {
            throw std::runtime_error(ruta + " tiene " + std::to_string(disponibles) + " pares, se pidieron " +
                                     std::to_string(maxPares));
        }
        f_ = std::fopen(ruta.c_str(), "rb");
        if (f_ == nullptr) throw std::runtime_error("no se pudo abrir " + ruta);
    }

    LectorDatos(const LectorDatos&) = delete;
    LectorDatos& operator=(const LectorDatos&) = delete;

    /// @brief Cierra el archivo.
    ~LectorDatos() {
        if (f_ != nullptr) std::fclose(f_);
    }

    /**
     * @brief Lee el siguiente bloque de pares.
     * @param bloque    vector que recibe los pares; se redimensiona a lo leído (salida).
     * @param capacidad cantidad máxima de pares a leer en esta llamada (entrada).
     * @return cantidad de pares leídos (0 cuando ya se leyeron los maxPares).
     */
    size_t siguiente(std::vector<Par>& bloque, size_t capacidad) {
        const size_t pedir = static_cast<size_t>(std::min<uint64_t>(capacidad, restantes_));
        bloque.resize(pedir);
        if (pedir == 0) return 0;
        const size_t n = std::fread(bloque.data(), sizeof(Par), pedir, f_);
        if (n != pedir) throw std::runtime_error("lectura incompleta del archivo de datos");
        restantes_ -= n;
        leidos_ += n;
        return n;
    }

    /// @brief Pares leídos hasta ahora. @return cantidad acumulada.
    uint64_t leidos() const { return leidos_; }

private:
    std::FILE* f_ = nullptr;  ///< Archivo de datos.
    uint64_t restantes_ = 0;  ///< Pares que faltan por leer.
    uint64_t leidos_ = 0;     ///< Pares leídos.
};
