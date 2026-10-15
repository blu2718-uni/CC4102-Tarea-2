/**
 * @file arbol_b.hpp
 * @brief Árbol B en memoria externa: inserción sobre el disco simulado y búsqueda por
 *        rango leyendo nodos desde el archivo binario.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "arbol_comun.hpp"
#include "archivo.hpp"
#include "disco.hpp"
#include "par.hpp"

/**
 * @brief Nodo del árbol B con capacidad para B pares llave-valor.
 *
 * Con B = 340 ocupa exactamente 4 + 4 + 340*8 + 341*4 + 4 = 4096 bytes (un bloque).
 * Invariante: llaves_valores[0..k) está ordenado ascendentemente y el hijo
 * hijos[i] solo tiene llaves mayores que llaves_valores[i-1].llave y menores o
 * iguales que llaves_valores[i].llave.
 *
 * @tparam B capacidad máxima de pares. Debe ser par, porque el split deja B/2-1 y B/2.
 */
template <int B>
struct NodoB {
    int32_t es_interno;     ///< 1 si el nodo es interno, 0 si es hoja.
    int32_t k;              ///< Cantidad de pares válidos (B/2-1 <= k <= B, salvo en la raíz).
    Par llaves_valores[B];  ///< Pares ordenados por llave; solo valen los primeros k.
    int32_t hijos[B + 1];   ///< Posiciones (índices de bloque) de los k+1 hijos; en hojas se ignora.
    int32_t padding;        ///< Relleno sin uso (siempre 0) para completar 4096 bytes.
};
static_assert(sizeof(NodoB<340>) == 4096, "El nodo del árbol B con b=340 debe ocupar 4096 bytes");

/**
 * @brief Árbol B construido sobre un DiscoSimulado. La raíz siempre está en el bloque 0.
 *
 * La inserción sigue la sección 3.4.2 del enunciado: los nodos llenos se dividen
 * antes de entrar en ellos, así que el padre siempre tiene espacio para la mediana.
 * La búsqueda por rango (sección 3.4.5) es estática y trabaja sobre el archivo
 * binario donde quedó guardado el árbol.
 *
 * @tparam B capacidad máxima de pares por nodo (b = 340 en la tarea).
 */
template <int B>
class ArbolB {
    static_assert(B >= 4 && B % 2 == 0, "b debe ser par y al menos 4");

public:
    using Nodo = NodoB<B>;     ///< Tipo de nodo (bloque) del árbol.
    static constexpr int kB = B;  ///< Capacidad de pares por nodo.

    /**
     * @brief Crea un árbol vacío: escribe una raíz hoja sin pares en el bloque 0
     *        (cuenta 1 escritura).
     */
    ArbolB() {
        Nodo raiz;
        std::memset(&raiz, 0, sizeof(Nodo));
        disco_.agregar(raiz);
    }

    /**
     * @brief Inserta un par llave-valor.
     *
     * Si la raíz está llena, se divide: R_izq y R_der se escriben al final del
     * arreglo y una raíz nueva con el par mediano se escribe en el bloque 0.
     * Después el par se inserta bajando por el árbol con insertarEnNodo().
     *
     * @param p par a insertar (entrada).
     */
    void insertar(const Par& p) {
        Nodo raiz;
        disco_.leer(0, raiz);
        if (raiz.k < B) {
            insertarEnNodo(0, raiz, p);
            return;
        }
        Nodo izq, der;
        Par mediana;
        dividir(raiz, izq, der, mediana);
        contarDivision(raiz);
        const int32_t iIzq = disco_.agregar(izq);
        const int32_t iDer = disco_.agregar(der);
        Nodo nueva;
        std::memset(&nueva, 0, sizeof(Nodo));
        nueva.es_interno = 1;
        nueva.k = 1;
        nueva.llaves_valores[0] = mediana;
        nueva.hijos[0] = iIzq;
        nueva.hijos[1] = iDer;
        disco_.escribir(0, nueva);
        ++altura_;
        if (p.llave <= mediana.llave) {
            insertarEnNodo(iIzq, izq, p);
        } else {
            insertarEnNodo(iDer, der, p);
        }
    }

    /**
     * @brief Busca todos los pares con llave en [l, u] leyendo nodos desde el archivo
     *        (sección 3.4.5). Cada nodo visitado cuenta 1 lectura en el archivo.
     *
     * Como cada nodo se recorre en orden (hijo, par, hijo, ...), los pares
     * resultantes quedan ordenados por llave.
     *
     * @param archivo archivo binario del árbol, con la raíz en el bloque 0 (entrada/salida: suma lecturas).
     * @param l       límite inferior del rango, inclusive (entrada).
     * @param u       límite superior del rango, inclusive (entrada).
     * @param salida  vector al que se agregan los pares encontrados (salida).
     */
    static void buscarRango(ArchivoArbol<Nodo>& archivo, int32_t l, int32_t u, std::vector<Par>& salida) {
        if (l > u) return;
        buscarEnNodo(archivo, 0, l, u, salida);
    }

    /// @brief Disco simulado del árbol. @return referencia al arreglo de nodos.
    DiscoSimulado<Nodo>& disco() { return disco_; }

    /// @brief Disco simulado del árbol (solo lectura). @return referencia constante al arreglo de nodos.
    const DiscoSimulado<Nodo>& disco() const { return disco_; }

    /// @brief Altura actual. @return cantidad de niveles (1 si la raíz es hoja).
    int altura() const { return altura_; }

    /// @brief Contadores de splits. @return contadores (los de flush quedan en 0).
    const ContadoresArbol& contadores() const { return contadores_; }

    /**
     * @brief Calcula estadísticas de la estructura recorriendo el arreglo, sin contar I/Os.
     * @return cantidad de nodos, internos, hojas y pares en cada tipo de nodo.
     */
    EstadisticasArbol estadisticas() const {
        EstadisticasArbol e;
        e.nodos = disco_.cantidad();
        e.altura = altura_;
        for (int64_t i = 0; i < e.nodos; ++i) {
            const Nodo& n = disco_.inspeccionar(static_cast<int32_t>(i));
            if (n.es_interno) {
                ++e.internos;
                e.elementosEnInternos += static_cast<uint64_t>(n.k);
            } else {
                ++e.hojas;
                e.elementosEnHojas += static_cast<uint64_t>(n.k);
            }
        }
        return e;
    }

private:
    /**
     * @brief Elige el hijo donde debe ir una llave: el primer i con llaves_valores[i].llave >= t.
     * @param n nodo interno (entrada).
     * @param t llave buscada (entrada).
     * @return índice del hijo, entre 0 y n.k.
     */
    static int posicionHijo(const Nodo& n, int32_t t) {
        return static_cast<int>(std::lower_bound(n.llaves_valores, n.llaves_valores + n.k, t, CompararLlave{}) -
                                n.llaves_valores);
    }

    /**
     * @brief Divide un nodo lleno (sección 3.4.1).
     *
     * El par en la posición B/2 (contando desde 1) es la mediana. Los B/2-1 pares
     * a su izquierda van a izq y los B/2 a su derecha van a der. Si el nodo es
     * interno, los primeros B/2 hijos van a izq y los B/2+1 restantes a der.
     *
     * @param lleno   nodo con k == B (entrada).
     * @param izq     nuevo nodo izquierdo, inicializado en cero (salida).
     * @param der     nuevo nodo derecho, inicializado en cero (salida).
     * @param mediana par mediano que sube al padre (salida).
     */
    static void dividir(const Nodo& lleno, Nodo& izq, Nodo& der, Par& mediana) {
        constexpr int kIzq = B / 2 - 1;
        constexpr int kDer = B / 2;
        std::memset(&izq, 0, sizeof(Nodo));
        std::memset(&der, 0, sizeof(Nodo));
        izq.es_interno = lleno.es_interno;
        der.es_interno = lleno.es_interno;
        izq.k = kIzq;
        der.k = kDer;
        mediana = lleno.llaves_valores[kIzq];
        std::memcpy(izq.llaves_valores, lleno.llaves_valores, kIzq * sizeof(Par));
        std::memcpy(der.llaves_valores, lleno.llaves_valores + kIzq + 1, kDer * sizeof(Par));
        if (lleno.es_interno) {
            std::memcpy(izq.hijos, lleno.hijos, (kIzq + 1) * sizeof(int32_t));
            std::memcpy(der.hijos, lleno.hijos + kIzq + 1, (kDer + 1) * sizeof(int32_t));
        }
    }

    /**
     * @brief Suma un split al contador de hojas o al de nodos internos.
     * @param n nodo que se dividió (entrada).
     */
    void contarDivision(const Nodo& n) {
        if (n.es_interno) {
            ++contadores_.divisionesInternas;
        } else {
            ++contadores_.divisionesHoja;
        }
    }

    /**
     * @brief Inserta p en el subárbol cuya raíz es nodo (sección 3.4.2).
     *
     * - Hoja: inserta p de forma ordenada y escribe la hoja.
     * - Interno: lee el hijo U que corresponde. Si U está lleno, lo divide
     *   (U_der al final del arreglo, U_izq en la posición de U), agrega la
     *   mediana y U_der a este nodo, lo escribe y sigue por U_izq o U_der según
     *   la llave. Si U no está lleno, sigue por U.
     *
     * @param indice posición del nodo en el disco (entrada).
     * @param nodo   copia en RAM del nodo, que no está lleno (entrada/salida).
     * @param p      par a insertar (entrada).
     */
    void insertarEnNodo(int32_t indice, Nodo& nodo, const Par& p) {
        if (!nodo.es_interno) {
            insertarOrdenado(nodo.llaves_valores, nodo.k, p);
            disco_.escribir(indice, nodo);
            return;
        }
        const int i = posicionHijo(nodo, p.llave);
        const int32_t iU = nodo.hijos[i];
        Nodo u;
        disco_.leer(iU, u);
        if (u.k < B) {
            insertarEnNodo(iU, u, p);
            return;
        }
        Nodo izq, der;
        Par mediana;
        dividir(u, izq, der, mediana);
        contarDivision(u);
        const int32_t iDer = disco_.agregar(der);
        disco_.escribir(iU, izq);
        std::memmove(&nodo.llaves_valores[i + 1], &nodo.llaves_valores[i],
                     static_cast<size_t>(nodo.k - i) * sizeof(Par));
        nodo.llaves_valores[i] = mediana;
        std::memmove(&nodo.hijos[i + 2], &nodo.hijos[i + 1], static_cast<size_t>(nodo.k - i) * sizeof(int32_t));
        nodo.hijos[i + 1] = iDer;
        ++nodo.k;
        disco_.escribir(indice, nodo);
        if (p.llave <= mediana.llave) {
            insertarEnNodo(iU, izq, p);
        } else {
            insertarEnNodo(iDer, der, p);
        }
    }

    /**
     * @brief Paso recursivo de la búsqueda por rango.
     *
     * Visita los hijos i con llaves_valores[i-1] <= u y llaves_valores[i] >= l.
     * Se usan comparaciones no estrictas para que la búsqueda siga siendo
     * correcta con llaves repetidas: un split puede dejar copias de la llave
     * mediana en ambos subárboles.
     *
     * @param archivo archivo del árbol (entrada/salida: suma lecturas).
     * @param indice  bloque del nodo actual (entrada).
     * @param l       límite inferior inclusive (entrada).
     * @param u       límite superior inclusive (entrada).
     * @param salida  pares encontrados (salida).
     */
    static void buscarEnNodo(ArchivoArbol<Nodo>& archivo, int32_t indice, int32_t l, int32_t u,
                             std::vector<Par>& salida) {
        alignas(4096) Nodo n;
        archivo.leer(indice, n);
        if (n.k < 0 || n.k > B) throw std::runtime_error("nodo corrupto en el bloque " + std::to_string(indice));
        const Par* ini = n.llaves_valores;
        const Par* fin = ini + n.k;
        const int a = static_cast<int>(std::lower_bound(ini, fin, l, CompararLlave{}) - ini);
        const int z = static_cast<int>(std::upper_bound(ini, fin, u, CompararLlave{}) - ini);
        if (!n.es_interno) {
            salida.insert(salida.end(), ini + a, ini + z);
            return;
        }
        for (int i = a; i <= z; ++i) {
            buscarEnNodo(archivo, n.hijos[i], l, u, salida);
            if (i < z) salida.push_back(n.llaves_valores[i]);
        }
    }

    DiscoSimulado<Nodo> disco_;  ///< Arreglo de nodos en RAM (disco simulado).
    int altura_ = 1;             ///< Cantidad de niveles.
    ContadoresArbol contadores_; ///< Contadores de splits.
};
