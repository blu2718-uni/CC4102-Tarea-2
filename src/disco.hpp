/**
 * @file disco.hpp
 * @brief Disco simulado en RAM: vector secuencial de nodos que contabiliza lecturas y escrituras.
 */
#pragma once

#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "archivo.hpp"

/**
 * @brief Vector de nodos en RAM que simula el disco durante la construcción del árbol.
 *
 * - El elemento i guarda el i-ésimo nodo; el índice 0 siempre es la raíz.
 * - leer() copia el nodo al buffer de quien llama y suma 1 lectura.
 * - escribir() (modifica una posición existente) y agregar() (inserta al final)
 *   copian el nodo al arreglo y suman 1 escritura cada una.
 *
 * @tparam Nodo tipo trivialmente copiable que ocupa exactamente un bloque.
 */
template <class Nodo>
class DiscoSimulado {
    static_assert(std::is_trivially_copyable<Nodo>::value, "El nodo debe ser trivialmente copiable");

public:
    /// @brief Crea un disco simulado vacío.
    DiscoSimulado() = default;

    /**
     * @brief Lee un nodo desde la posición i del arreglo (cuenta 1 lectura).
     * @param i       índice del bloque, 0 <= i < cantidad() (entrada).
     * @param destino estructura donde se copia el nodo leído (salida).
     */
    void leer(int32_t i, Nodo& destino) {
        validarIndice(i);
        std::memcpy(&destino, &bloques_[static_cast<size_t>(i)], sizeof(Nodo));
        ++lecturas_;
    }

    /**
     * @brief Sobrescribe un nodo existente en la posición i del arreglo (cuenta 1 escritura).
     * @param i      índice del bloque, 0 <= i < cantidad() (entrada).
     * @param origen nodo que se escribirá en esa posición (entrada).
     */
    void escribir(int32_t i, const Nodo& origen) {
        validarIndice(i);
        std::memcpy(&bloques_[static_cast<size_t>(i)], &origen, sizeof(Nodo));
        ++escrituras_;
    }

    /**
     * @brief Agrega un nuevo nodo al final del arreglo (cuenta 1 escritura).
     * @param origen nodo a agregar al final (entrada).
     * @return índice (posición) donde quedó almacenado el nuevo nodo.
     */
    int32_t agregar(const Nodo& origen) {
        if (bloques_.size() >= static_cast<size_t>(INT32_MAX)) {
            throw std::length_error("disco simulado lleno");
        }
        const int32_t i = static_cast<int32_t>(bloques_.size());
        bloques_.push_back(origen);
        ++escrituras_;
        return i;
    }

    /**
     * @brief Accede a un nodo sin sumar I/Os (solo para estadísticas finales y pruebas).
     * @param i índice del bloque (entrada).
     * @return referencia constante al nodo almacenado.
     */
    const Nodo& inspeccionar(int32_t i) const {
        validarIndice(i);
        return bloques_[static_cast<size_t>(i)];
    }

    /// @brief Cantidad de nodos almacenados en el arreglo. @return número de bloques.
    int64_t cantidad() const { return static_cast<int64_t>(bloques_.size()); }

    /// @brief Lecturas simuladas acumuladas. @return total de llamadas a leer().
    uint64_t lecturas() const { return lecturas_; }

    /// @brief Escrituras simuladas acumuladas. @return total de llamadas a escribir() y agregar().
    uint64_t escrituras() const { return escrituras_; }

    /**
     * @brief Escribe todos los nodos de forma secuencial (0, 1, ..., cantidad()-1) a un archivo binario.
     * @param ruta ruta del archivo binario de salida (entrada).
     * @return cantidad total de bytes escritos (cantidad() * sizeof(Nodo)).
     */
    uint64_t guardar(const std::string& ruta) const {
        const int fd = crearArchivoBloques(ruta);
        uint64_t total = 0;
        for (size_t i = 0; i < bloques_.size(); ++i) {
            escribirTodo(fd, &bloques_[i], sizeof(Nodo), ruta);
            total += sizeof(Nodo);
        }
        terminarArchivo(fd, ruta);
        return total;
    }

private:
    /**
     * @brief Verifica que el índice de bloque esté dentro del rango válido.
     * @param i índice de bloque a validar (entrada).
     */
    void validarIndice(int32_t i) const {
        if (i < 0 || static_cast<size_t>(i) >= bloques_.size()) {
            throw std::out_of_range("bloque fuera del disco simulado: " + std::to_string(i));
        }
    }

    std::vector<Nodo> bloques_;  ///< Arreglo secuencial de nodos en RAM.
    uint64_t lecturas_ = 0;      ///< Contador de lecturas simuladas.
    uint64_t escrituras_ = 0;    ///< Contador de escrituras simuladas.
};
