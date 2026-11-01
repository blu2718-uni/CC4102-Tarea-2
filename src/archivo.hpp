/**
 * @file archivo.hpp
 * @brief Escritura y lectura por bloques de archivos binarios de árboles en disco.
 */
#pragma once

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Crea un archivo binario vacío para escribir bloques, truncándolo si ya existe.
 * @param ruta ruta del archivo (entrada).
 * @return descriptor abierto para escritura.
 */
inline int crearArchivoBloques(const std::string& ruta) {
    const int fd = ::open(ruta.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "no se pudo crear " + ruta);
    return fd;
}

/**
 * @brief Escribe n bytes completos en un archivo abierto.
 * @param fd   descriptor abierto para escritura (entrada).
 * @param p    puntero a los datos a escribir (entrada).
 * @param n    cantidad de bytes a escribir (entrada).
 * @param ruta ruta del archivo, para los mensajes de error (entrada).
 */
inline void escribirTodo(int fd, const void* p, size_t n, const std::string& ruta) {
    const char* q = static_cast<const char*>(p);
    while (n > 0) {
        const ssize_t w = ::write(fd, q, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            const int e = errno;
            ::close(fd);
            throw std::system_error(e, std::generic_category(), "error escribiendo " + ruta);
        }
        q += w;
        n -= static_cast<size_t>(w);
    }
}

/**
 * @brief Sincroniza los bloques en disco (fsync), descarta la caché de páginas y cierra el archivo.
 * @param fd   descriptor abierto para escritura; queda cerrado (entrada).
 * @param ruta ruta del archivo, para los mensajes de error (entrada).
 */
inline void terminarArchivo(int fd, const std::string& ruta) {
    if (::fsync(fd) != 0) {
        const int e = errno;
        ::close(fd);
        throw std::system_error(e, std::generic_category(), "error en fsync de " + ruta);
    }
#ifdef POSIX_FADV_DONTNEED
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
    ::close(fd);
}

/**
 * @brief Archivo binario de un árbol en disco, donde el nodo i reside en el byte i * sizeof(Nodo).
 *        Permite leer bloques individuales con pread y contabiliza cada lectura como una I/O.
 *
 * @tparam Nodo tipo de nodo serializado en el archivo.
 */
template <class Nodo>
class ArchivoArbol {
public:
    /// Alineación de bloque para lectura directa desde disco.
    static constexpr size_t kAlineacion = 4096;

    /**
     * @brief Abre el archivo binario del árbol (con O_DIRECT si el nodo mide un múltiplo de 4096 B).
     * @param ruta ruta del archivo binario (entrada).
     */
    explicit ArchivoArbol(const std::string& ruta) {
#ifdef O_DIRECT
        if (sizeof(Nodo) % kAlineacion == 0) {
            fd_ = ::open(ruta.c_str(), O_RDONLY | O_DIRECT);
        }
#endif
        if (fd_ < 0) {
            fd_ = ::open(ruta.c_str(), O_RDONLY);
            if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "no se pudo abrir " + ruta);
        }
        struct stat st;
        if (::fstat(fd_, &st) != 0) {
            const int e = errno;
            ::close(fd_);
            throw std::system_error(e, std::generic_category(), "no se pudo consultar " + ruta);
        }
        if (st.st_size % static_cast<off_t>(sizeof(Nodo)) != 0) {
            ::close(fd_);
            throw std::runtime_error("el tamaño de " + ruta + " no es múltiplo del tamaño de nodo");
        }
        bloques_ = static_cast<int64_t>(st.st_size / static_cast<off_t>(sizeof(Nodo)));
    }

    ArchivoArbol(const ArchivoArbol&) = delete;
    ArchivoArbol& operator=(const ArchivoArbol&) = delete;

    /// @brief Cierra el descriptor del archivo.
    ~ArchivoArbol() {
        if (fd_ >= 0) ::close(fd_);
    }

    /**
     * @brief Lee el nodo en la posición i del archivo (1 lectura de bloque).
     * @param i       índice del bloque a leer, 0 <= i < bloques() (entrada).
     * @param destino buffer alineado a 4096 bytes donde se copia el nodo leído (salida).
     */
    void leer(int32_t i, Nodo& destino) {
        if (i < 0 || static_cast<int64_t>(i) >= bloques_) {
            throw std::out_of_range("bloque fuera del archivo: " + std::to_string(i));
        }
        size_t restante = sizeof(Nodo);
        off_t offset = static_cast<off_t>(i) * static_cast<off_t>(sizeof(Nodo));
        char* p = reinterpret_cast<char*>(&destino);
        while (restante > 0) {
            const ssize_t r = ::pread(fd_, p, restante, offset);
            if (r < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "error leyendo bloque " + std::to_string(i));
            }
            if (r == 0) throw std::runtime_error("fin de archivo inesperado en bloque " + std::to_string(i));
            p += r;
            restante -= static_cast<size_t>(r);
            offset += r;
        }
        ++lecturas_;
    }

    /// @brief Cantidad de lecturas realizadas desde el último reinicio. @return bloques leídos.
    uint64_t lecturas() const { return lecturas_; }

    /// @brief Reinicia en cero el contador de lecturas.
    void reiniciarLecturas() { lecturas_ = 0; }

    /// @brief Cantidad total de bloques (nodos) en el archivo. @return número de bloques.
    int64_t bloques() const { return bloques_; }

private:
    int fd_ = -1;            ///< Descriptor del archivo abierto.
    int64_t bloques_ = 0;    ///< Cantidad de bloques en el archivo.
    uint64_t lecturas_ = 0;  ///< Contador de bloques leídos.
};
