/**
 * @file archivo.hpp
 * @brief Lectura de nodos (bloques) desde el archivo binario donde se guardó un árbol.
 */
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Árbol guardado en un archivo binario, donde el nodo i está en el byte
 *        i*sizeof(Nodo). Lee nodos sueltos y cuenta las lecturas.
 *
 * Si se pide modo directo, el archivo se abre con O_DIRECT (en Linux) o
 * F_NOCACHE (en macOS). Así cada lectura va al dispositivo y no a la caché de
 * páginas del sistema operativo, y el tiempo de búsqueda incluye el acceso
 * real a disco. Si el modo directo no se puede usar (por ejemplo, en tmpfs o
 * con nodos que no miden un múltiplo de 4096 bytes), se lee de forma normal.
 *
 * @tparam Nodo tipo de nodo serializado en el archivo.
 */
template <class Nodo>
class ArchivoArbol {
public:
    /// Alineación de memoria que pide O_DIRECT.
    static constexpr size_t kAlineacion = 4096;

    /**
     * @brief Abre el archivo de un árbol.
     * @param ruta    ruta del archivo binario (entrada).
     * @param directo true para intentar leer sin la caché del sistema operativo (entrada).
     */
    ArchivoArbol(const std::string& ruta, bool directo) {
        const bool puedeDirecto = directo && (sizeof(Nodo) % kAlineacion == 0);
#ifdef O_DIRECT
        if (puedeDirecto) {
            fd_ = ::open(ruta.c_str(), O_RDONLY | O_DIRECT);
            if (fd_ >= 0) directo_ = true;
        }
#endif
        if (fd_ < 0) {
            fd_ = ::open(ruta.c_str(), O_RDONLY);
            if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "no se pudo abrir " + ruta);
#if defined(__APPLE__) && defined(F_NOCACHE)
            if (puedeDirecto && ::fcntl(fd_, F_NOCACHE, 1) == 0) directo_ = true;
#endif
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
        if (::posix_memalign(&intermedio_, kAlineacion, sizeof(Nodo)) != 0) {
            ::close(fd_);
            throw std::bad_alloc();
        }
    }

    ArchivoArbol(const ArchivoArbol&) = delete;
    ArchivoArbol& operator=(const ArchivoArbol&) = delete;

    /// @brief Cierra el archivo y libera el buffer intermedio.
    ~ArchivoArbol() {
        if (fd_ >= 0) ::close(fd_);
        std::free(intermedio_);
    }

    /**
     * @brief Lee el nodo i del archivo (1 lectura de un bloque completo).
     *
     * En modo directo conviene que destino esté alineado a 4096 bytes. Si no lo
     * está, se lee a un buffer alineado y después se copia.
     *
     * @param i       posición del bloque, 0 <= i < bloques() (entrada).
     * @param destino nodo donde se dejan los bytes leídos (salida).
     */
    void leer(int32_t i, Nodo& destino) {
        if (i < 0 || static_cast<int64_t>(i) >= bloques_) {
            throw std::out_of_range("bloque fuera del archivo: " + std::to_string(i));
        }
        char* buf = reinterpret_cast<char*>(&destino);
        if (directo_ && reinterpret_cast<uintptr_t>(buf) % kAlineacion != 0) buf = static_cast<char*>(intermedio_);
        size_t restante = sizeof(Nodo);
        off_t offset = static_cast<off_t>(i) * static_cast<off_t>(sizeof(Nodo));
        char* p = buf;
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
        if (buf != reinterpret_cast<char*>(&destino)) std::memcpy(&destino, buf, sizeof(Nodo));
        ++lecturas_;
    }

    /// @brief Lecturas hechas desde el último reinicio. @return cantidad de bloques leídos.
    uint64_t lecturas() const { return lecturas_; }

    /// @brief Pone en cero el contador de lecturas.
    void reiniciarLecturas() { lecturas_ = 0; }

    /// @brief Bloques del archivo. @return cantidad de nodos guardados.
    int64_t bloques() const { return bloques_; }

    /// @brief Indica si se lee sin caché del SO. @return true si se usa O_DIRECT/F_NOCACHE.
    bool directo() const { return directo_; }

private:
    int fd_ = -1;                 ///< Descriptor del archivo.
    bool directo_ = false;        ///< true si se lee sin caché del sistema operativo.
    int64_t bloques_ = 0;         ///< Cantidad de bloques del archivo.
    uint64_t lecturas_ = 0;       ///< Lecturas acumuladas.
    void* intermedio_ = nullptr;  ///< Buffer alineado para O_DIRECT.
};
