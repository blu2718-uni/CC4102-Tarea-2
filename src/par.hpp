/**
 * @file par.hpp
 * @brief Par llave-valor (tiempo Unix, temperatura) y comparador por llave.
 */
#pragma once

#include <cstdint>
#include <limits>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "datos.bin está en little-endian: este código requiere una arquitectura little-endian"
#endif
static_assert(std::numeric_limits<float>::is_iec559, "Se requiere float IEEE-754 de 4 bytes");

/**
 * @brief Par llave-valor de 8 bytes con el mismo formato que datos.bin.
 *
 * El archivo de datos guarda pares consecutivos (int32 little-endian, float32
 * little-endian), así que un arreglo de Par se puede leer directamente con fread.
 * También es el formato de los pares dentro de los nodos de ambos árboles.
 */
struct Par {
    int32_t llave;  ///< Tiempo Unix (segundos) en que se tomó la medición.
    float valor;    ///< Temperatura medida (°C).
};
static_assert(sizeof(Par) == 8, "Par debe ocupar exactamente 8 bytes");

/**
 * @brief Comparador por llave para std::lower_bound, std::upper_bound y std::merge.
 *
 * Permite comparar Par con Par, Par con llave y llave con Par.
 */
struct CompararLlave {
    /**
     * @brief Compara dos pares por llave.
     * @param a primer par (entrada).
     * @param b segundo par (entrada).
     * @return true si a.llave < b.llave.
     */
    bool operator()(const Par& a, const Par& b) const { return a.llave < b.llave; }

    /**
     * @brief Compara un par con una llave.
     * @param a par (entrada).
     * @param t llave (entrada).
     * @return true si a.llave < t.
     */
    bool operator()(const Par& a, int32_t t) const { return a.llave < t; }

    /**
     * @brief Compara una llave con un par.
     * @param t llave (entrada).
     * @param b par (entrada).
     * @return true si t < b.llave.
     */
    bool operator()(int32_t t, const Par& b) const { return t < b.llave; }
};
