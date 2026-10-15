/**
 * @file disco.hpp
 * @brief Disco simulado en RAM: arreglo secuencial de bloques (nodos) que cuenta I/Os.
 */
#pragma once

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "archivo.hpp"

/**
 * @brief Arreglo de nodos en RAM que hace de disco mientras se construye un árbol.
 *
 * - El bloque i guarda el i-ésimo nodo. El bloque 0 siempre es la raíz.
 * - leer() copia el bloque completo al buffer de quien llama y cuenta 1 lectura.
 * - escribir() (sobrescribe una posición) y agregar() (escribe al final) copian
 *   el bloque completo al arreglo y cuentan 1 escritura cada una.
 *
 * Los bloques se guardan en segmentos de kBloquesPorSegmento nodos reservados
 * con mmap. Así el arreglo crece sin copiarse entero (un std::vector, al
 * realocar, necesita por un momento el doble de memoria, y un árbol de más de
 * 1 GB no cabría), y al liberar la memoria vuelve al sistema operativo. Las
 * páginas llegan en cero y solo ocupan RAM cuando se escriben. Para los
 * algoritmos sigue siendo un arreglo indexado por posición de bloque.
 *
 * @tparam Nodo tipo trivialmente copiable que ocupa un bloque.
 */
template <class Nodo>
class DiscoSimulado {
    static_assert(std::is_trivially_copyable<Nodo>::value, "El nodo debe ser trivialmente copiable");

public:
    /// Cantidad de bloques por segmento de memoria (4 MiB para nodos de 4096 bytes).
    static constexpr int64_t kBloquesPorSegmento = 1024;

    /// @brief Crea un disco vacío (sin bloques).
    DiscoSimulado() = default;
    DiscoSimulado(const DiscoSimulado&) = delete;
    DiscoSimulado& operator=(const DiscoSimulado&) = delete;

    /// @brief Libera toda la memoria del disco.
    ~DiscoSimulado() { liberar(); }

    /**
     * @brief Lee un bloque completo (1 lectura).
     * @param i       posición del bloque, 0 <= i < cantidad() (entrada).
     * @param destino buffer donde se copia el nodo (salida).
     */
    void leer(int32_t i, Nodo& destino) {
        std::memcpy(&destino, direccion(i), sizeof(Nodo));
        ++lecturas_;
    }

    /**
     * @brief Sobrescribe un bloque existente con un nodo completo (1 escritura).
     * @param i      posición del bloque, 0 <= i < cantidad() (entrada).
     * @param origen nodo que se escribe (entrada).
     */
    void escribir(int32_t i, const Nodo& origen) {
        std::memcpy(direccion(i), &origen, sizeof(Nodo));
        ++escrituras_;
    }

    /**
     * @brief Escribe un nodo nuevo al final del arreglo (1 escritura).
     * @param origen nodo que se escribe (entrada).
     * @return posición (índice de bloque) donde quedó el nodo.
     */
    int32_t agregar(const Nodo& origen) {
        if (cantidad_ >= static_cast<int64_t>(INT32_MAX)) throw std::length_error("disco simulado lleno");
        if (cantidad_ == static_cast<int64_t>(segmentos_.size()) * kBloquesPorSegmento) nuevoSegmento();
        const int32_t i = static_cast<int32_t>(cantidad_++);
        std::memcpy(direccion(i), &origen, sizeof(Nodo));
        ++escrituras_;
        return i;
    }

    /**
     * @brief Accede a un bloque sin contar I/O. Solo para estadísticas y pruebas,
     *        nunca se usa dentro de los algoritmos de inserción.
     * @param i posición del bloque (entrada).
     * @return referencia constante al nodo guardado.
     */
    const Nodo& inspeccionar(int32_t i) const { return *direccion(i); }

    /// @brief Cantidad de bloques escritos. @return número de nodos del árbol.
    int64_t cantidad() const { return cantidad_; }

    /// @brief Lecturas acumuladas. @return cantidad de llamadas a leer().
    uint64_t lecturas() const { return lecturas_; }

    /// @brief Escrituras acumuladas. @return cantidad de llamadas a escribir() y agregar().
    uint64_t escrituras() const { return escrituras_; }

    /**
     * @brief Guarda el árbol en un archivo binario: escribe los nodos 0, 1, 2, ... en orden,
     *        con una escritura de un bloque (sizeof(Nodo) bytes) por nodo, así que el
     *        nodo i queda en el byte i*sizeof(Nodo).
     *
     * El archivo se crea con crearArchivoBloques() (en Btrfs queda sin compresión, para
     * poder leerlo con O_DIRECT) y se cierra con terminarArchivo(): fsync, para que los
     * datos estén realmente en el disco antes de buscar, y posix_fadvise, para sacarlo de
     * la caché de páginas y que las búsquedas lean de verdad del disco.
     *
     * @param ruta ruta del archivo de salida; se sobrescribe si existe (entrada).
     * @return cantidad de bytes escritos (= cantidad() * sizeof(Nodo)).
     */
    uint64_t guardar(const std::string& ruta) const {
        const int fd = crearArchivoBloques(ruta);
        uint64_t total = 0;
        for (int64_t i = 0; i < cantidad_; ++i) {
            escribirTodo(fd, direccion(static_cast<int32_t>(i)), sizeof(Nodo), ruta);
            total += sizeof(Nodo);
        }
        terminarArchivo(fd, ruta);
        return total;
    }

    /**
     * @brief Libera la memoria de todos los bloques y deja el disco vacío.
     *        Los contadores de I/O se conservan.
     */
    void liberar() {
        for (Nodo* s : segmentos_) ::munmap(s, bytesSegmento());
        segmentos_.clear();
        segmentos_.shrink_to_fit();
        cantidad_ = 0;
    }

private:
    /// @brief Bytes de un segmento. @return kBloquesPorSegmento * sizeof(Nodo).
    static constexpr size_t bytesSegmento() { return static_cast<size_t>(kBloquesPorSegmento) * sizeof(Nodo); }

    /**
     * @brief Calcula la dirección en RAM de un bloque y valida el índice.
     * @param i posición del bloque (entrada).
     * @return puntero al nodo i.
     */
    Nodo* direccion(int32_t i) const {
        if (i < 0 || static_cast<int64_t>(i) >= cantidad_) {
            throw std::out_of_range("bloque fuera del disco simulado: " + std::to_string(i));
        }
        const uint32_t u = static_cast<uint32_t>(i);
        return segmentos_[u / kBloquesPorSegmento] + (u % kBloquesPorSegmento);
    }

    /// @brief Reserva un segmento nuevo (en cero) con mmap.
    void nuevoSegmento() {
        void* p = ::mmap(nullptr, bytesSegmento(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) throw std::bad_alloc();
        segmentos_.push_back(static_cast<Nodo*>(p));
    }

    std::vector<Nodo*> segmentos_;  ///< Segmentos de memoria con los bloques.
    int64_t cantidad_ = 0;          ///< Cantidad de bloques en uso.
    uint64_t lecturas_ = 0;         ///< Lecturas acumuladas.
    uint64_t escrituras_ = 0;       ///< Escrituras acumuladas.
};
