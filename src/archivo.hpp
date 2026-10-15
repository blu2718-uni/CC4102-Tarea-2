/**
 * @file archivo.hpp
 * @brief Escritura y lectura por bloques de archivos en disco: el archivo binario donde
 *        se guarda un árbol y el archivo de prueba de la medición de latencia.
 *
 * Las búsquedas deben leer de verdad del disco. Para eso se usa O_DIRECT, que salta la
 * caché de páginas del sistema operativo, y se revisan dos casos en que O_DIRECT se
 * acepta pero no lee del dispositivo:
 *  - tmpfs/ramfs: el archivo está en RAM.
 *  - Btrfs con compresión (Fedora monta / con compress=zstd:1): una lectura O_DIRECT
 *    de un extent comprimido vuelve a la lectura normal, con caché. Para evitarlo, los
 *    archivos se crean con el atributo NOCOW (como `chattr +C`), que en Btrfs desactiva
 *    la compresión, y después se verifica con FIEMAP que ningún extent quedó comprimido.
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
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#endif

/**
 * @brief Intenta marcar un archivo vacío como NOCOW (sin copy-on-write).
 *
 * En Btrfs esto desactiva la compresión del archivo, que de otro modo impediría leerlo
 * con O_DIRECT. Solo tiene efecto si el archivo todavía no tiene datos. En otros
 * sistemas de archivos (ext4, xfs) la operación falla o no hace nada, y no importa.
 *
 * @param fd descriptor de un archivo recién creado (entrada).
 * @return true si el archivo quedó con el atributo NOCOW.
 */
inline bool marcarSinCopiaEnEscritura(int fd) {
#if defined(__linux__) && defined(FS_IOC_GETFLAGS) && defined(FS_IOC_SETFLAGS) && defined(FS_NOCOW_FL)
    int atributos = 0;
    if (::ioctl(fd, FS_IOC_GETFLAGS, &atributos) != 0) return false;
    if (atributos & FS_NOCOW_FL) return true;
    atributos |= FS_NOCOW_FL;
    return ::ioctl(fd, FS_IOC_SETFLAGS, &atributos) == 0;
#else
    (void)fd;
    return false;
#endif
}

/**
 * @brief Crea un archivo vacío para escribir bloques: borra el anterior (si existe), lo
 *        crea de nuevo y lo marca NOCOW antes de escribir nada.
 * @param ruta ruta del archivo (entrada).
 * @return descriptor abierto para escritura.
 */
inline int crearArchivoBloques(const std::string& ruta) {
    if (::unlink(ruta.c_str()) != 0 && errno != ENOENT) {
        throw std::system_error(errno, std::generic_category(), "no se pudo borrar " + ruta);
    }
    const int fd = ::open(ruta.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "no se pudo crear " + ruta);
    marcarSinCopiaEnEscritura(fd);
    return fd;
}

/**
 * @brief Escribe n bytes completos (write puede escribir menos de lo pedido).
 * @param fd   descriptor abierto para escritura (entrada).
 * @param p    datos a escribir (entrada).
 * @param n    cantidad de bytes (entrada).
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
 * @brief Termina un archivo de bloques: fsync (los datos quedan en el disco), le pide al
 *        sistema operativo que lo saque de su caché de páginas y lo cierra.
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
 * @brief Revisa si un archivo abierto se puede leer de verdad desde el dispositivo con O_DIRECT.
 * @param fd descriptor del archivo (entrada).
 * @return cadena vacía si se puede; si no, el motivo: "tmpfs" (el archivo está en RAM) o
 *         "comprimido" (algún extent está comprimido o guardado dentro de los metadatos,
 *         y Btrfs lo leería con caché).
 */
inline std::string motivoSinLecturaDirecta(int fd) {
#ifdef __linux__
    struct statfs sf;
    if (::fstatfs(fd, &sf) == 0) {
        const unsigned long tipo = static_cast<unsigned long>(sf.f_type);
        if (tipo == 0x01021994UL || tipo == 0x858458f6UL) return "tmpfs";  // TMPFS_MAGIC, RAMFS_MAGIC
    }
#if defined(FS_IOC_FIEMAP)
    constexpr unsigned kExtents = 256;
    std::vector<char> memoria(sizeof(struct fiemap) + kExtents * sizeof(struct fiemap_extent));
    struct fiemap* fm = reinterpret_cast<struct fiemap*>(memoria.data());
    uint64_t inicio = 0;
    while (true) {
        std::memset(memoria.data(), 0, memoria.size());
        fm->fm_start = inicio;
        fm->fm_length = FIEMAP_MAX_OFFSET - inicio;
        fm->fm_flags = FIEMAP_FLAG_SYNC;
        fm->fm_extent_count = kExtents;
        if (::ioctl(fd, FS_IOC_FIEMAP, fm) != 0) return "";  // sin FIEMAP no se puede revisar: se asume que sí
        if (fm->fm_mapped_extents == 0) return "";
        for (unsigned i = 0; i < fm->fm_mapped_extents; ++i) {
            const struct fiemap_extent& e = fm->fm_extents[i];
            if (e.fe_flags & (FIEMAP_EXTENT_ENCODED | FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_NOT_ALIGNED)) {
                return "comprimido";
            }
            if (e.fe_flags & FIEMAP_EXTENT_LAST) return "";
        }
        const struct fiemap_extent& ultimo = fm->fm_extents[fm->fm_mapped_extents - 1];
        inicio = ultimo.fe_logical + ultimo.fe_length;
    }
#endif
#else
    (void)fd;
#endif
    return "";
}

/**
 * @brief Árbol guardado en un archivo binario, donde el nodo i está en el byte
 *        i*sizeof(Nodo). Lee nodos sueltos y cuenta las lecturas.
 *
 * Si se pide modo directo, el archivo se abre con O_DIRECT (en Linux) o F_NOCACHE (en
 * macOS), para que cada lectura vaya al dispositivo y no a la caché de páginas. directo()
 * es true solo si además motivoSinLecturaDirecta() no encontró problemas; si no, motivo()
 * dice por qué las lecturas no van de verdad al disco.
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
        bool abiertoDirecto = false;
#ifdef O_DIRECT
        if (puedeDirecto) {
            fd_ = ::open(ruta.c_str(), O_RDONLY | O_DIRECT);
            abiertoDirecto = fd_ >= 0;
        }
#endif
        if (fd_ < 0) {
            fd_ = ::open(ruta.c_str(), O_RDONLY);
            if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "no se pudo abrir " + ruta);
#if defined(__APPLE__) && defined(F_NOCACHE)
            if (puedeDirecto && ::fcntl(fd_, F_NOCACHE, 1) == 0) abiertoDirecto = true;
#endif
        }
        alinear_ = abiertoDirecto;
        if (!directo) {
            motivo_ = "no_pedido";
        } else if (!abiertoDirecto) {
            motivo_ = "sin_soporte";
        } else {
            motivo_ = motivoSinLecturaDirecta(fd_);
        }
        directo_ = abiertoDirecto && motivo_.empty();
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
     * Con O_DIRECT el buffer debe estar alineado a 4096 bytes. Si destino no lo está, se
     * lee a un buffer alineado y después se copia.
     *
     * @param i       posición del bloque, 0 <= i < bloques() (entrada).
     * @param destino nodo donde se dejan los bytes leídos (salida).
     */
    void leer(int32_t i, Nodo& destino) {
        if (i < 0 || static_cast<int64_t>(i) >= bloques_) {
            throw std::out_of_range("bloque fuera del archivo: " + std::to_string(i));
        }
        char* buf = reinterpret_cast<char*>(&destino);
        if (alinear_ && reinterpret_cast<uintptr_t>(buf) % kAlineacion != 0) buf = static_cast<char*>(intermedio_);
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

    /// @brief Indica si las lecturas van de verdad al disco. @return true si se usa O_DIRECT/F_NOCACHE sin problemas.
    bool directo() const { return directo_; }

    /**
     * @brief Motivo por el que las lecturas no van directo al disco.
     * @return "" si directo() es true; si no, "no_pedido", "sin_soporte", "tmpfs" o "comprimido".
     */
    const std::string& motivo() const { return motivo_; }

private:
    int fd_ = -1;                 ///< Descriptor del archivo.
    bool alinear_ = false;        ///< true si el archivo se abrió con O_DIRECT (los buffers deben estar alineados).
    bool directo_ = false;        ///< true si las lecturas van de verdad al disco.
    std::string motivo_;          ///< Motivo por el que directo_ es false.
    int64_t bloques_ = 0;         ///< Cantidad de bloques del archivo.
    uint64_t lecturas_ = 0;       ///< Lecturas acumuladas.
    void* intermedio_ = nullptr;  ///< Buffer alineado para O_DIRECT.
};
