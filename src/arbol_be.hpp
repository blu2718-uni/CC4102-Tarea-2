/**
 * @file arbol_be.hpp
 * @brief Árbol Bε en memoria externa: inserción con buffers (flush con parámetro α)
 *        sobre el disco simulado y búsqueda por rango desde el archivo binario.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "arbol_comun.hpp"
#include "archivo.hpp"
#include "disco.hpp"
#include "par.hpp"

/**
 * @brief Nodo del árbol Bε, con hasta C llaves divisorias y un buffer de hasta L pares.
 *
 * Ocupa 4 + 4 + 4 + 4C + 4(C+1) + 8L = 8(2 + C + L) bytes. Con C + L = 510 son
 * 4096 bytes: (C, L) = (4, 506) para ε ≈ 0.25 y (126, 384) para ε ≈ 0.75.
 * - Interno: llaves[0..k_llaves) separa los k_llaves+1 hijos. hijos[i] tiene
 *   llaves en (llaves[i-1], llaves[i]]. El buffer guarda inserciones que todavía
 *   no bajan a los hijos.
 * - Hoja: k_llaves = 0. El buffer guarda los pares de la hoja.
 * En ambos casos buffer[0..k_buffer) está ordenado por llave.
 *
 * @tparam C máximo de llaves divisorias.
 * @tparam L máximo de pares en el buffer (l_max).
 */
template <int C, int L>
struct NodoBe {
    int32_t es_interno;  ///< 1 si el nodo es interno, 0 si es hoja.
    int32_t k_llaves;    ///< Llaves divisorias activas (0 <= k_llaves <= C; 0 en hojas).
    int32_t k_buffer;    ///< Pares en el buffer (0 <= k_buffer <= L).
    int32_t llaves[C];   ///< Llaves divisorias ordenadas (sin valores asociados).
    int32_t hijos[C + 1];  ///< Posiciones (índices de bloque) de los k_llaves+1 hijos.
    Par buffer[L];       ///< Pares ordenados: log de inserciones pendientes (interno) o datos (hoja).
};
static_assert(sizeof(NodoBe<4, 506>) == 4096, "El nodo Bε con c=4, l_max=506 debe ocupar 4096 bytes");
static_assert(sizeof(NodoBe<126, 384>) == 4096, "El nodo Bε con c=126, l_max=384 debe ocupar 4096 bytes");

/**
 * @brief Árbol Bε construido sobre un DiscoSimulado. La raíz siempre está en el bloque 0.
 *
 * Cada inserción entra al buffer de la raíz (1 lectura + 1 escritura). Cuando un
 * buffer se llena, el flush baja a cada hijo que tenga al menos ceil(f·α)
 * pares pendientes, con f = ceil(k_buffer / (k_llaves+1)) (sección 3.4.3).
 *
 * Mientras se procesa un flush, un nodo interno se maneja como una copia de
 * trabajo en RAM (NodoTrabajo). Esa copia puede tener más de C llaves por un
 * momento si varios hijos se dividen en el mismo flush. Antes de escribirla de
 * vuelta al disco se divide las veces que haga falta, así que cada bloque
 * escrito cumple k_llaves <= C.
 *
 * @tparam C máximo de llaves divisorias por nodo interno.
 * @tparam L máximo de pares en el buffer (l_max).
 */
template <int C, int L>
class ArbolBe {
    static_assert(C >= 4 && C % 2 == 0, "c debe ser par y al menos 4");
    static_assert(L >= 2 && L % 2 == 0, "l_max debe ser par y al menos 2");

public:
    using Nodo = NodoBe<C, L>;              ///< Tipo de nodo (bloque) del árbol.
    static constexpr int kC = C;            ///< Máximo de llaves divisorias.
    static constexpr int kL = L;            ///< Capacidad del buffer.
    static constexpr int64_t kEscalaAlpha = 1000000;  ///< α se guarda como entero α·10^6.

    /**
     * @brief Crea un árbol vacío: escribe una raíz hoja vacía en el bloque 0 (1 escritura).
     * @param alpha parámetro del flush, con 0 < alpha < 1 (entrada).
     */
    explicit ArbolBe(double alpha) : alpha_(alpha) {
        if (!(alpha > 0.0 && alpha < 1.0)) throw std::invalid_argument("alpha debe cumplir 0 < alpha < 1");
        // Se guarda como entero para que ceil(f·α) sea exacto: en doble precisión, por ejemplo,
        // 100·0.07 = 7.000000000000001 y el techo daría 8 en vez de 7.
        alphaEscalado_ = std::llround(alpha * static_cast<double>(kEscalaAlpha));
        alphaEscalado_ = std::min<int64_t>(std::max<int64_t>(alphaEscalado_, 1), kEscalaAlpha - 1);
        std::memset(&raiz_, 0, sizeof(Nodo));
        disco_.agregar(raiz_);
    }

    /**
     * @brief Inserta un par (sección 3.4.4).
     *
     * - Raíz hoja con espacio: inserta el par ordenado y escribe la raíz.
     * - Raíz hoja llena: split de hoja. R_izq y R_der van al final del arreglo,
     *   y una raíz interna nueva, con la llave divisoria y el par en su buffer,
     *   se escribe en el bloque 0.
     * - Raíz interna: si su buffer está lleno, se hace flush. Si después la raíz
     *   tiene c (o más) llaves, se divide y se crea una raíz nueva. Al final el
     *   par se inserta en el buffer de la raíz y esta se escribe.
     *
     * @param p par a insertar (entrada).
     */
    void insertar(const Par& p) {
        disco_.leer(0, raiz_);
        if (!raiz_.es_interno) {
            if (raiz_.k_buffer < L) {
                insertarOrdenado(raiz_.buffer, raiz_.k_buffer, p);
                disco_.escribir(0, raiz_);
            } else {
                dividirRaizHoja(p);
            }
            return;
        }
        if (raiz_.k_buffer == L) {
            NodoTrabajo r;
            aTrabajo(raiz_, r);
            vaciar(r);
            if (r.llaves.size() >= static_cast<size_t>(C)) dividirRaizInterna(r);
            aNodo(r, raiz_);
        }
        insertarOrdenado(raiz_.buffer, raiz_.k_buffer, p);
        disco_.escribir(0, raiz_);
    }

    /**
     * @brief Busca todos los pares con llave en [l, u] leyendo nodos desde el archivo
     *        (sección 3.4.6). Revisa el buffer de cada nodo visitado y baja a los
     *        hijos cuyo rango toca [l, u]. Cada nodo visitado cuenta 1 lectura.
     *
     * @param archivo archivo binario del árbol, con la raíz en el bloque 0 (entrada/salida: suma lecturas).
     * @param l       límite inferior del rango, inclusive (entrada).
     * @param u       límite superior del rango, inclusive (entrada).
     * @param salida  vector al que se agregan los pares encontrados, sin un orden particular (salida).
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

    /// @brief Parámetro α del flush. @return valor recibido en el constructor.
    double alpha() const { return alpha_; }

    /// @brief Contadores de flush, lotes y splits. @return contadores acumulados.
    const ContadoresArbol& contadores() const { return contadores_; }

    /**
     * @brief Calcula cuántos pares debe tener un hijo para recibirlos en el flush: ceil(f·α).
     *        Es público para poder probarlo directamente.
     * @param kBuffer pares en el buffer del nodo (entrada).
     * @param kLlaves llaves divisorias del nodo (entrada).
     * @return umbral mínimo (al menos 1), con f = ceil(kBuffer / (kLlaves + 1)).
     */
    int64_t umbral(size_t kBuffer, size_t kLlaves) const {
        const int64_t hijos = static_cast<int64_t>(kLlaves) + 1;
        const int64_t f = (static_cast<int64_t>(kBuffer) + hijos - 1) / hijos;
        const int64_t u = (f * alphaEscalado_ + kEscalaAlpha - 1) / kEscalaAlpha;
        return std::max<int64_t>(u, 1);
    }

    /**
     * @brief Calcula estadísticas de la estructura recorriendo el arreglo, sin contar I/Os.
     * @return cantidad de nodos, internos, hojas y pares en buffers internos y en hojas.
     */
    EstadisticasArbol estadisticas() const {
        EstadisticasArbol e;
        e.nodos = disco_.cantidad();
        e.altura = altura_;
        for (int64_t i = 0; i < e.nodos; ++i) {
            const Nodo& n = disco_.inspeccionar(static_cast<int32_t>(i));
            if (n.es_interno) {
                ++e.internos;
                e.elementosEnInternos += static_cast<uint64_t>(n.k_buffer);
            } else {
                ++e.hojas;
                e.elementosEnHojas += static_cast<uint64_t>(n.k_buffer);
            }
        }
        return e;
    }

private:
    /**
     * @brief Copia de trabajo en RAM de un nodo interno mientras se procesa un flush.
     *
     * Se guarda en vectores para que pueda tener más de C llaves por un momento,
     * si varios hijos se dividen en el mismo flush. Nunca se escribe así al
     * disco: antes se divide (particionar).
     */
    struct NodoTrabajo {
        std::vector<int32_t> llaves;  ///< Llaves divisorias ordenadas.
        std::vector<int32_t> hijos;   ///< Índices de bloque de los hijos (llaves.size()+1).
        std::vector<Par> buffer;      ///< Pares pendientes, ordenados por llave.
    };

    /// @brief Hijo nuevo que aparece a la derecha de otro tras un split.
    struct NuevoHijo {
        int32_t llave;   ///< Llave divisoria entre el hijo anterior y este.
        int32_t indice;  ///< Bloque del hijo nuevo.
    };

    /**
     * @brief Pasa un bloque de nodo interno a su copia de trabajo.
     * @param n nodo interno leído del disco (entrada).
     * @param t copia de trabajo (salida).
     */
    static void aTrabajo(const Nodo& n, NodoTrabajo& t) {
        t.llaves.assign(n.llaves, n.llaves + n.k_llaves);
        t.hijos.assign(n.hijos, n.hijos + n.k_llaves + 1);
        t.buffer.reserve(L);
        t.buffer.assign(n.buffer, n.buffer + n.k_buffer);
    }

    /**
     * @brief Arma un bloque de nodo interno (relleno con ceros) desde una copia de trabajo.
     * @param t copia de trabajo con a lo más C llaves y L pares (entrada).
     * @param n bloque resultante (salida).
     */
    static void aNodo(const NodoTrabajo& t, Nodo& n) {
        if (t.llaves.size() > static_cast<size_t>(C) || t.buffer.size() > static_cast<size_t>(L) ||
            t.hijos.size() != t.llaves.size() + 1) {
            throw std::logic_error("nodo interno con tamaño inválido al escribir");
        }
        std::memset(&n, 0, sizeof(Nodo));
        n.es_interno = 1;
        n.k_llaves = static_cast<int32_t>(t.llaves.size());
        n.k_buffer = static_cast<int32_t>(t.buffer.size());
        std::copy(t.llaves.begin(), t.llaves.end(), n.llaves);
        std::copy(t.hijos.begin(), t.hijos.end(), n.hijos);
        std::copy(t.buffer.begin(), t.buffer.end(), n.buffer);
    }

    /**
     * @brief Flush de un nodo interno (sección 3.4.3).
     *
     * Agrupa el buffer según el hijo de cada par. Como el buffer está ordenado,
     * cada grupo es un tramo contiguo. Elige los hijos con al menos ceil(f·α)
     * pares, que son todos los que tienen el máximo o casi, y los procesa de
     * derecha a izquierda con empujar(). Si un hijo se divide, sus hermanos
     * nuevos y sus llaves divisorias se insertan en v justo a su derecha; como
     * se avanza hacia la izquierda, las posiciones que faltan no se mueven.
     * Los pares de los hijos no elegidos se quedan en el buffer de v. El
     * nodo v no se escribe aquí: eso lo hace quien llama.
     *
     * @param v copia de trabajo del nodo interno; su buffer pierde los grupos bajados
     *          y puede ganar llaves e hijos (entrada/salida).
     */
    void vaciar(NodoTrabajo& v) {
        ++contadores_.vaciados;
        std::vector<Par> original;
        original.swap(v.buffer);
        const size_t nb = original.size();
        const size_t K = v.llaves.size();
        // fin[i] = fin (exclusivo) del grupo del hijo i dentro de original.
        std::vector<size_t> fin(K + 1);
        size_t pos = 0;
        for (size_t i = 0; i < K; ++i) {
            pos = static_cast<size_t>(
                std::upper_bound(original.begin() + static_cast<std::ptrdiff_t>(pos), original.end(), v.llaves[i],
                                 CompararLlave{}) -
                original.begin());
            fin[i] = pos;
        }
        fin[K] = nb;
        const int64_t minimo = umbral(nb, K);
        std::vector<char> elegido(K + 1, 0);
        v.buffer.reserve(L);
        size_t inicio = 0;
        for (size_t i = 0; i <= K; ++i) {
            const size_t m = fin[i] - inicio;
            if (static_cast<int64_t>(m) >= minimo) {
                elegido[i] = 1;
            } else {
                v.buffer.insert(v.buffer.end(), original.begin() + static_cast<std::ptrdiff_t>(inicio),
                                original.begin() + static_cast<std::ptrdiff_t>(fin[i]));
            }
            inicio = fin[i];
        }
        std::vector<NuevoHijo> nuevos;
        for (size_t j = K + 1; j-- > 0;) {
            if (!elegido[j]) continue;
            const size_t ini = (j == 0) ? 0 : fin[j - 1];
            empujar(v.hijos[j], original.data() + ini, fin[j] - ini, nuevos);
            for (size_t q = 0; q < nuevos.size(); ++q) {
                v.llaves.insert(v.llaves.begin() + static_cast<std::ptrdiff_t>(j + q), nuevos[q].llave);
                v.hijos.insert(v.hijos.begin() + static_cast<std::ptrdiff_t>(j + 1 + q), nuevos[q].indice);
            }
        }
    }

    /**
     * @brief Pasa un lote ordenado de m pares al hijo guardado en el bloque indice.
     *
     * - Hoja: ver empujarAHoja().
     * - Interno: mientras k_buffer(U) + m > l_max, hace flush de U. Después mezcla
     *   el lote en el buffer de U. Si U quedó con más de C llaves, lo divide:
     *   la primera parte queda en la posición de U y las demás van al final del
     *   arreglo. Si no, solo escribe U.
     *
     * @param indice bloque del hijo (entrada).
     * @param lote   pares ordenados que bajan; todos pertenecen al rango del hijo (entrada).
     * @param m      tamaño del lote, 1 <= m <= L (entrada).
     * @param nuevos hermanos nuevos que aparecen a la derecha del hijo, con sus llaves divisorias (salida).
     */
    void empujar(int32_t indice, const Par* lote, size_t m, std::vector<NuevoHijo>& nuevos) {
        nuevos.clear();
        if (m == 0 || m > static_cast<size_t>(L)) throw std::logic_error("tamaño de lote inválido");
        ++contadores_.lotes;
        contadores_.elementosEmpujados += m;
        Nodo u;
        disco_.leer(indice, u);
        if (!u.es_interno) {
            empujarAHoja(indice, u, lote, m, nuevos);
            return;
        }
        NodoTrabajo t;
        aTrabajo(u, t);
        while (t.buffer.size() + m > static_cast<size_t>(L)) vaciar(t);
        const size_t k = t.buffer.size();
        t.buffer.resize(k + m);
        mezclarAlFinal(t.buffer.data(), k, lote, m);
        if (t.llaves.size() <= static_cast<size_t>(C)) {
            aNodo(t, u);
            disco_.escribir(indice, u);
            return;
        }
        std::vector<NodoTrabajo> piezas;
        std::vector<int32_t> separadores;
        particionar(t, piezas, separadores);
        contadores_.divisionesInternas += piezas.size() - 1;
        aNodo(piezas[0], u);
        disco_.escribir(indice, u);
        for (size_t q = 1; q < piezas.size(); ++q) {
            aNodo(piezas[q], u);
            nuevos.push_back({separadores[q - 1], disco_.agregar(u)});
        }
    }

    /**
     * @brief Pasa un lote a una hoja; si no cabe, hace split de la hoja.
     *
     * Si k_buffer + m <= l_max, mezcla el lote en el buffer y escribe la hoja.
     * Si no, junta los s = k_buffer + m pares en orden y divide en la posición
     * ceil(s/2): esos primeros pares quedan en H_izq (en la posición de la hoja)
     * y el resto en H_der (al final del arreglo). La llave del último par de
     * H_izq sube como divisoria. Es lo mismo que dividir la hoja y repartir el
     * lote con <= k_u / > k_u, pero usa la mediana del conjunto completo, así
     * que ninguna mitad pasa de l_max aunque el lote sea muy desbalanceado
     * (s <= 2·l_max).
     *
     * @param indice bloque de la hoja (entrada).
     * @param hoja   copia en RAM de la hoja (entrada/salida).
     * @param lote   pares ordenados que llegan (entrada).
     * @param m      tamaño del lote (entrada).
     * @param nuevos recibe la hoja nueva y su llave divisoria si hubo split (salida).
     */
    void empujarAHoja(int32_t indice, Nodo& hoja, const Par* lote, size_t m, std::vector<NuevoHijo>& nuevos) {
        const size_t k = static_cast<size_t>(hoja.k_buffer);
        const size_t s = k + m;
        if (s <= static_cast<size_t>(L)) {
            mezclarAlFinal(hoja.buffer, k, lote, m);
            hoja.k_buffer = static_cast<int32_t>(s);
            disco_.escribir(indice, hoja);
            return;
        }
        Par mezcla[2 * L];
        std::merge(hoja.buffer, hoja.buffer + k, lote, lote + m, mezcla, CompararLlave{});
        const size_t nIzq = (s + 1) / 2;
        const size_t nDer = s - nIzq;
        Nodo izq, der;
        std::memset(&izq, 0, sizeof(Nodo));
        std::memset(&der, 0, sizeof(Nodo));
        izq.k_buffer = static_cast<int32_t>(nIzq);
        der.k_buffer = static_cast<int32_t>(nDer);
        std::memcpy(izq.buffer, mezcla, nIzq * sizeof(Par));
        std::memcpy(der.buffer, mezcla + nIzq, nDer * sizeof(Par));
        disco_.escribir(indice, izq);
        nuevos.push_back({mezcla[nIzq - 1].llave, disco_.agregar(der)});
        ++contadores_.divisionesHoja;
    }

    /**
     * @brief Split de un nodo interno con K >= C llaves (sección 3.4.3).
     *
     * La llave mediana es la de índice (K-1)/2 (base 0). Con K = C es la
     * posición C/2 contando desde 1: quedan C/2-1 llaves a la izquierda y C/2 a
     * la derecha, y C/2 y C/2+1 hijos. El buffer se reparte: llave <= k_m va a
     * izq y llave > k_m va a der.
     *
     * @param u   copia de trabajo a dividir (entrada).
     * @param izq mitad izquierda (salida).
     * @param der mitad derecha (salida).
     * @param km  llave mediana que sube al padre (salida).
     */
    static void dividirInterno(const NodoTrabajo& u, NodoTrabajo& izq, NodoTrabajo& der, int32_t& km) {
        const size_t K = u.llaves.size();
        const size_t mid = (K - 1) / 2;
        km = u.llaves[mid];
        izq.llaves.assign(u.llaves.begin(), u.llaves.begin() + static_cast<std::ptrdiff_t>(mid));
        izq.hijos.assign(u.hijos.begin(), u.hijos.begin() + static_cast<std::ptrdiff_t>(mid + 1));
        der.llaves.assign(u.llaves.begin() + static_cast<std::ptrdiff_t>(mid + 1), u.llaves.end());
        der.hijos.assign(u.hijos.begin() + static_cast<std::ptrdiff_t>(mid + 1), u.hijos.end());
        const auto corte = std::upper_bound(u.buffer.begin(), u.buffer.end(), km, CompararLlave{});
        izq.buffer.assign(u.buffer.begin(), corte);
        der.buffer.assign(corte, u.buffer.end());
    }

    /**
     * @brief Divide recursivamente una copia de trabajo hasta que cada parte tenga <= C llaves.
     * @param u           nodo a particionar; queda en un estado no especificado (entrada).
     * @param piezas      partes resultantes, de izquierda a derecha (salida, se agregan al final).
     * @param separadores llaves divisorias entre partes consecutivas (salida, se agregan al final).
     */
    static void particionar(NodoTrabajo& u, std::vector<NodoTrabajo>& piezas, std::vector<int32_t>& separadores) {
        if (u.llaves.size() <= static_cast<size_t>(C)) {
            piezas.push_back(std::move(u));
            return;
        }
        NodoTrabajo izq, der;
        int32_t km;
        dividirInterno(u, izq, der, km);
        particionar(izq, piezas, separadores);
        separadores.push_back(km);
        particionar(der, piezas, separadores);
    }

    /**
     * @brief Split de una raíz hoja llena al insertar p (sección 3.4.4 a).
     *
     * La llave en la posición l_max/2 (contando desde 1) es la divisoria. Los
     * primeros l_max/2 pares van a R_izq y el resto a R_der, ambos al final del
     * arreglo. La raíz nueva (interna, con p en su buffer) se escribe en el bloque 0.
     *
     * @param p par que se estaba insertando (entrada).
     */
    void dividirRaizHoja(const Par& p) {
        constexpr int kMitad = L / 2;
        Nodo izq, der, nueva;
        std::memset(&izq, 0, sizeof(Nodo));
        std::memset(&der, 0, sizeof(Nodo));
        std::memset(&nueva, 0, sizeof(Nodo));
        izq.k_buffer = kMitad;
        der.k_buffer = L - kMitad;
        std::memcpy(izq.buffer, raiz_.buffer, kMitad * sizeof(Par));
        std::memcpy(der.buffer, raiz_.buffer + kMitad, (L - kMitad) * sizeof(Par));
        nueva.es_interno = 1;
        nueva.k_llaves = 1;
        nueva.llaves[0] = raiz_.buffer[kMitad - 1].llave;
        nueva.hijos[0] = disco_.agregar(izq);
        nueva.hijos[1] = disco_.agregar(der);
        nueva.k_buffer = 1;
        nueva.buffer[0] = p;
        disco_.escribir(0, nueva);
        ++altura_;
        ++contadores_.divisionesHoja;
    }

    /**
     * @brief Split de la raíz interna cuando llega a C (o más) llaves.
     *
     * Divide la raíz con dividirInterno(). Si alguna mitad sigue con más de C
     * llaves, la vuelve a dividir. Las partes van al final del arreglo y r pasa
     * a ser la raíz nueva (sin pares en el buffer), con las llaves divisorias y
     * los índices de esas partes. Quien llama la escribe en el bloque 0. En el
     * caso, casi imposible, de que la raíz nueva también llegue a C llaves, se
     * repite el proceso.
     *
     * @param r copia de trabajo de la raíz; queda convertida en la raíz nueva (entrada/salida).
     */
    void dividirRaizInterna(NodoTrabajo& r) {
        NodoTrabajo izq, der;
        int32_t km;
        dividirInterno(r, izq, der, km);
        std::vector<NodoTrabajo> piezas;
        std::vector<int32_t> separadores;
        particionar(izq, piezas, separadores);
        separadores.push_back(km);
        particionar(der, piezas, separadores);
        contadores_.divisionesInternas += piezas.size() - 1;
        NodoTrabajo nueva;
        Nodo bloque;
        for (const NodoTrabajo& pieza : piezas) {
            aNodo(pieza, bloque);
            nueva.hijos.push_back(disco_.agregar(bloque));
        }
        nueva.llaves = std::move(separadores);
        r = std::move(nueva);
        ++altura_;
        if (r.llaves.size() >= static_cast<size_t>(C)) dividirRaizInterna(r);
    }

    /**
     * @brief Paso recursivo de la búsqueda por rango.
     *
     * Agrega los pares del buffer que están en [l, u] y baja a los hijos i con
     * llaves[i-1] <= u y llaves[i] >= l. Las comparaciones no estrictas
     * mantienen la búsqueda correcta con llaves repetidas.
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
        if (n.k_buffer < 0 || n.k_buffer > L || n.k_llaves < 0 || n.k_llaves > C) {
            throw std::runtime_error("nodo corrupto en el bloque " + std::to_string(indice));
        }
        const Par* b = n.buffer;
        const Par* e = b + n.k_buffer;
        const Par* desde = std::lower_bound(b, e, l, CompararLlave{});
        const Par* hasta = std::upper_bound(desde, e, u, CompararLlave{});
        salida.insert(salida.end(), desde, hasta);
        if (!n.es_interno) return;
        const int a = static_cast<int>(std::lower_bound(n.llaves, n.llaves + n.k_llaves, l) - n.llaves);
        const int z = static_cast<int>(std::upper_bound(n.llaves, n.llaves + n.k_llaves, u) - n.llaves);
        for (int i = a; i <= z; ++i) buscarEnNodo(archivo, n.hijos[i], l, u, salida);
    }

    DiscoSimulado<Nodo> disco_;     ///< Arreglo de nodos en RAM (disco simulado).
    double alpha_;                  ///< Parámetro α del flush.
    int64_t alphaEscalado_ = 0;     ///< α·10^6 redondeado (para calcular ceil(f·α) con enteros).
    int altura_ = 1;                ///< Cantidad de niveles.
    ContadoresArbol contadores_;    ///< Contadores de flush, lotes y splits.
    Nodo raiz_;                     ///< Buffer en RAM donde se lee la raíz en cada inserción.
};
