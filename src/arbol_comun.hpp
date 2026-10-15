/**
 * @file arbol_comun.hpp
 * @brief Utilidades compartidas por el árbol B y el árbol Bε.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "par.hpp"

/**
 * @brief Estadísticas estructurales de un árbol ya construido.
 *
 * Se calculan recorriendo el arreglo de nodos sin contar I/Os: no son parte
 * de los algoritmos, solo sirven para reportar resultados.
 */
struct EstadisticasArbol {
    int64_t nodos = 0;                 ///< Cantidad total de nodos (= bloques en disco).
    int64_t internos = 0;              ///< Cantidad de nodos internos.
    int64_t hojas = 0;                 ///< Cantidad de hojas.
    uint64_t elementosEnInternos = 0;  ///< Pares en nodos internos (pares del árbol B o buffers del Bε).
    uint64_t elementosEnHojas = 0;     ///< Pares en hojas.
    int altura = 0;                    ///< Cantidad de niveles (una raíz hoja tiene altura 1).
};

/**
 * @brief Contadores de operaciones internas de la inserción (para el análisis).
 */
struct ContadoresArbol {
    uint64_t vaciados = 0;            ///< Llamadas a flush (solo Bε).
    uint64_t lotes = 0;               ///< Transferencias de un grupo de pares a un hijo (solo Bε).
    uint64_t elementosEmpujados = 0;  ///< Pares bajados un nivel en total (solo Bε).
    uint64_t divisionesHoja = 0;      ///< Splits de hojas.
    uint64_t divisionesInternas = 0;  ///< Splits de nodos internos (incluye la raíz).
};

/**
 * @brief Inserta un par en un arreglo ordenado por llave y lo mantiene ordenado.
 *
 * Si ya hay pares con la misma llave, el nuevo queda después de ellos.
 *
 * @param a arreglo con a[0..k) ordenado y espacio para al menos k+1 pares (entrada/salida).
 * @param k cantidad de pares válidos en a; se incrementa en 1 (entrada/salida).
 * @param p par a insertar (entrada).
 */
inline void insertarOrdenado(Par* a, int32_t& k, const Par& p) {
    Par* pos = std::upper_bound(a, a + k, p.llave, CompararLlave{});
    std::memmove(pos + 1, pos, static_cast<size_t>((a + k) - pos) * sizeof(Par));
    *pos = p;
    ++k;
}

/**
 * @brief Mezcla un lote ordenado dentro de un arreglo ordenado, sin memoria auxiliar.
 *
 * Recorre ambos arreglos desde el final. Si hay llaves iguales, los pares del
 * lote quedan después de los que ya estaban.
 *
 * @param a    arreglo con a[0..k) ordenado y espacio para k+m pares (entrada/salida).
 * @param k    cantidad de pares válidos en a (entrada).
 * @param lote arreglo ordenado de m pares que no se solapa con a (entrada).
 * @param m    cantidad de pares del lote (entrada).
 */
inline void mezclarAlFinal(Par* a, size_t k, const Par* lote, size_t m) {
    size_t i = k, j = m, w = k + m;
    while (j > 0) {
        if (i > 0 && a[i - 1].llave > lote[j - 1].llave) {
            a[--w] = a[--i];
        } else {
            a[--w] = lote[--j];
        }
    }
}
