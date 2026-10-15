/**
 * @file pruebas.cpp
 * @brief Pruebas de correctitud de los árboles B y Bε:
 *        - conteo exacto de I/Os en casos pequeños hechos a mano,
 *        - invariantes de la estructura (orden, rangos, ocupación, altura y
 *          multiconjunto de pares) con parámetros chicos, para tener árboles
 *          profundos, y con los parámetros reales,
 *        - búsquedas por rango desde el archivo comparadas con fuerza bruta,
 *        - datos aleatorios, ordenados, con llaves repetidas y extremas, y una
 *          muestra de datos.bin si está disponible.
 */

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "arbol_b.hpp"
#include "arbol_be.hpp"
#include "datos.hpp"

namespace {

int gFallas = 0;          ///< Verificaciones fallidas.
int gVerificaciones = 0;  ///< Verificaciones realizadas.

/**
 * @brief Registra una verificación y muestra el mensaje si falló.
 * @param ok      resultado de la verificación (entrada).
 * @param mensaje descripción del caso (entrada).
 */
void verificar(bool ok, const std::string& mensaje) {
    ++gVerificaciones;
    if (!ok) {
        ++gFallas;
        std::fprintf(stderr, "  FALLA: %s\n", mensaje.c_str());
    }
}

/**
 * @brief Clave de orden total de un par (llave y bits del valor), para comparar multiconjuntos.
 * @param p par (entrada).
 * @return entero de 64 bits que respeta el orden por llave.
 */
uint64_t claveOrden(const Par& p) {
    uint32_t bits;
    std::memcpy(&bits, &p.valor, sizeof bits);
    return (static_cast<uint64_t>(static_cast<uint32_t>(p.llave) ^ 0x80000000u) << 32) | bits;
}

/**
 * @brief Compara dos colecciones de pares como multiconjuntos.
 * @param a primera colección (entrada, se copia).
 * @param b segunda colección (entrada, se copia).
 * @return true si tienen exactamente los mismos pares con las mismas multiplicidades.
 */
bool mismoMulticonjunto(std::vector<Par> a, std::vector<Par> b) {
    if (a.size() != b.size()) return false;
    // Orden total por (llave, bits del valor), para que pares iguales queden en la misma posición.
    auto cmp = [](const Par& x, const Par& y) { return claveOrden(x) < claveOrden(y); };
    std::sort(a.begin(), a.end(), cmp);
    std::sort(b.begin(), b.end(), cmp);
    for (size_t i = 0; i < a.size(); ++i) {
        if (claveOrden(a[i]) != claveOrden(b[i])) return false;
    }
    return true;
}

/**
 * @brief Búsqueda por fuerza bruta.
 * @param datos pares insertados (entrada).
 * @param l     límite inferior inclusive (entrada).
 * @param u     límite superior inclusive (entrada).
 * @return pares con llave en [l, u].
 */
std::vector<Par> fuerzaBruta(const std::vector<Par>& datos, int32_t l, int32_t u) {
    std::vector<Par> r;
    for (const Par& p : datos) {
        if (p.llave >= l && p.llave <= u) r.push_back(p);
    }
    return r;
}

/**
 * @brief Genera datos de prueba.
 * @param tipo    aleatorio | ascendente | descendente | duplicados | constante | extremos (entrada).
 * @param n       cantidad de pares (entrada).
 * @param semilla semilla (entrada).
 * @return pares generados.
 */
std::vector<Par> generar(const std::string& tipo, size_t n, uint64_t semilla) {
    std::mt19937_64 rng(semilla);
    std::vector<Par> v(n);
    for (size_t i = 0; i < n; ++i) {
        int32_t t = 0;
        if (tipo == "aleatorio") t = static_cast<int32_t>(rng() % 1000000000ULL);
        else if (tipo == "ascendente") t = static_cast<int32_t>(i * 3);
        else if (tipo == "descendente") t = static_cast<int32_t>((n - i) * 3);
        else if (tipo == "duplicados") t = static_cast<int32_t>(rng() % 23);
        else if (tipo == "constante") t = 7;
        else if (tipo == "extremos") {
            t = (rng() & 1) ? INT32_MIN + static_cast<int32_t>(rng() % 50) : INT32_MAX - static_cast<int32_t>(rng() % 50);
        }
        v[i] = {t, static_cast<float>(static_cast<double>(rng() % 100000) / 1000.0 - 20.0)};
    }
    return v;
}

/**
 * @brief Revisa todas las invariantes de un árbol B construido.
 * @tparam B capacidad del nodo.
 * @param arbol      árbol a revisar (entrada).
 * @param insertados pares insertados (entrada).
 * @param error      descripción del primer problema encontrado (salida).
 * @return true si el árbol es válido.
 */
template <int B>
bool validar(const ArbolB<B>& arbol, const std::vector<Par>& insertados, std::string& error) {
    const auto& d = arbol.disco();
    std::vector<char> visto(static_cast<size_t>(d.cantidad()), 0);
    std::vector<Par> todos;
    int profHoja = -1;
    // Revisa el subárbol del bloque idx, cuyas llaves deben estar en [lo, hi], a profundidad prof.
    // Junta sus pares en `todos`, marca los nodos en `visto` y deja el problema en `error`.
    // Retorna false en el primer problema encontrado.
    std::function<bool(int32_t, int64_t, int64_t, int, bool)> rec = [&](int32_t idx, int64_t lo, int64_t hi, int prof,
                                                                        bool raiz) -> bool {
        if (idx < 0 || idx >= d.cantidad()) return error = "índice de hijo fuera de rango", false;
        if (visto[static_cast<size_t>(idx)]) return error = "nodo alcanzable por dos caminos", false;
        visto[static_cast<size_t>(idx)] = 1;
        const NodoB<B>& n = d.inspeccionar(idx);
        if (n.es_interno != 0 && n.es_interno != 1) return error = "es_interno inválido", false;
        if (n.k < 0 || n.k > B) return error = "k fuera de [0, b]", false;
        if (!raiz && n.k < B / 2 - 1) return error = "nodo no raíz con menos de b/2-1 pares", false;
        if (raiz && n.es_interno && n.k < 1) return error = "raíz interna sin pares", false;
        if (n.padding != 0) return error = "padding distinto de 0", false;
        for (int i = 0; i < n.k; ++i) {
            const int64_t t = n.llaves_valores[i].llave;
            if (t < lo || t > hi) return error = "llave fuera del rango de su subárbol", false;
            if (i > 0 && n.llaves_valores[i - 1].llave > t) return error = "pares desordenados", false;
            todos.push_back(n.llaves_valores[i]);
        }
        if (!n.es_interno) {
            if (profHoja == -1) profHoja = prof;
            if (profHoja != prof) return error = "hojas a distinta profundidad", false;
            return true;
        }
        for (int i = 0; i <= n.k; ++i) {
            const int64_t a = (i == 0) ? lo : n.llaves_valores[i - 1].llave;
            const int64_t b = (i == n.k) ? hi : n.llaves_valores[i].llave;
            if (!rec(n.hijos[i], a, b, prof + 1, false)) return false;
        }
        return true;
    };
    if (!rec(0, INT32_MIN, INT32_MAX, 0, true)) return false;
    if (std::count(visto.begin(), visto.end(), 1) != d.cantidad()) return error = "hay nodos inalcanzables", false;
    if (profHoja + 1 != arbol.altura()) return error = "altura registrada incorrecta", false;
    if (!mismoMulticonjunto(todos, insertados)) return error = "los pares del árbol no son los insertados", false;
    const EstadisticasArbol e = arbol.estadisticas();
    if (e.elementosEnHojas + e.elementosEnInternos != insertados.size()) return error = "estadísticas incoherentes", false;
    return true;
}

/**
 * @brief Revisa todas las invariantes de un árbol Bε construido.
 * @tparam C máximo de llaves divisorias.
 * @tparam L capacidad del buffer.
 * @param arbol      árbol a revisar (entrada).
 * @param insertados pares insertados (entrada).
 * @param error      descripción del primer problema encontrado (salida).
 * @return true si el árbol es válido.
 */
template <int C, int L>
bool validar(const ArbolBe<C, L>& arbol, const std::vector<Par>& insertados, std::string& error) {
    const auto& d = arbol.disco();
    std::vector<char> visto(static_cast<size_t>(d.cantidad()), 0);
    std::vector<Par> todos;
    int profHoja = -1;
    // Revisa el subárbol del bloque idx, cuyas llaves deben estar en [lo, hi], a profundidad prof.
    // Junta sus pares en `todos`, marca los nodos en `visto` y deja el problema en `error`.
    // Retorna false en el primer problema encontrado.
    std::function<bool(int32_t, int64_t, int64_t, int, bool)> rec = [&](int32_t idx, int64_t lo, int64_t hi, int prof,
                                                                        bool raiz) -> bool {
        if (idx < 0 || idx >= d.cantidad()) return error = "índice de hijo fuera de rango", false;
        if (visto[static_cast<size_t>(idx)]) return error = "nodo alcanzable por dos caminos", false;
        visto[static_cast<size_t>(idx)] = 1;
        const NodoBe<C, L>& n = d.inspeccionar(idx);
        if (n.es_interno != 0 && n.es_interno != 1) return error = "es_interno inválido", false;
        if (n.k_buffer < 0 || n.k_buffer > L) return error = "k_buffer fuera de [0, l_max]", false;
        for (int i = 0; i < n.k_buffer; ++i) {
            const int64_t t = n.buffer[i].llave;
            if (t < lo || t > hi) return error = "par del buffer fuera del rango de su nodo", false;
            if (i > 0 && n.buffer[i - 1].llave > t) return error = "buffer desordenado", false;
            todos.push_back(n.buffer[i]);
        }
        if (!n.es_interno) {
            if (n.k_llaves != 0) return error = "hoja con k_llaves distinto de 0", false;
            if (!raiz && n.k_buffer < L / 2) return error = "hoja no raíz con menos de l_max/2 pares", false;
            if (profHoja == -1) profHoja = prof;
            if (profHoja != prof) return error = "hojas a distinta profundidad", false;
            return true;
        }
        if (n.k_llaves < 1 || n.k_llaves > C) return error = "k_llaves fuera de [1, c]", false;
        if (raiz && n.k_llaves >= C) return error = "raíz con c llaves (debió dividirse)", false;
        if (!raiz && n.k_llaves < C / 2 - 1) return error = "nodo interno con menos de c/2-1 llaves", false;
        for (int i = 0; i < n.k_llaves; ++i) {
            if (n.llaves[i] < lo || n.llaves[i] > hi) return error = "llave divisoria fuera de rango", false;
            if (i > 0 && n.llaves[i - 1] > n.llaves[i]) return error = "llaves divisorias desordenadas", false;
        }
        for (int i = 0; i <= n.k_llaves; ++i) {
            const int64_t a = (i == 0) ? lo : n.llaves[i - 1];
            const int64_t b = (i == n.k_llaves) ? hi : n.llaves[i];
            if (!rec(n.hijos[i], a, b, prof + 1, false)) return false;
        }
        return true;
    };
    if (!rec(0, INT32_MIN, INT32_MAX, 0, true)) return false;
    if (std::count(visto.begin(), visto.end(), 1) != d.cantidad()) return error = "hay nodos inalcanzables", false;
    if (profHoja + 1 != arbol.altura()) return error = "altura registrada incorrecta", false;
    if (!mismoMulticonjunto(todos, insertados)) return error = "los pares del árbol no son los insertados", false;
    const EstadisticasArbol e = arbol.estadisticas();
    if (e.elementosEnHojas + e.elementosEnInternos != insertados.size()) return error = "estadísticas incoherentes", false;
    return true;
}

/**
 * @brief Guarda el árbol en un archivo temporal y compara búsquedas con fuerza bruta.
 *
 * Revisa el rango completo, rangos vacíos, rangos fuera de los datos, consultas
 * puntuales sobre llaves existentes y rangos aleatorios de distintos anchos.
 *
 * @tparam Arbol tipo de árbol.
 * @param arbol    árbol construido (entrada).
 * @param datos    pares insertados (entrada).
 * @param nombre   nombre del caso, para los mensajes (entrada).
 * @param directo  usar O_DIRECT al leer (entrada).
 * @param consultas cantidad de rangos aleatorios (entrada).
 */
template <class Arbol>
void probarBusquedas(const Arbol& arbol, const std::vector<Par>& datos, const std::string& nombre, bool directo,
                     int consultas) {
    const std::string ruta = "build/prueba_" + std::to_string(::getpid()) + ".bin";
    const uint64_t bytes = arbol.disco().guardar(ruta);
    verificar(bytes == static_cast<uint64_t>(arbol.disco().cantidad()) * sizeof(typename Arbol::Nodo),
              nombre + ": tamaño del archivo");
    ArchivoArbol<typename Arbol::Nodo> archivo(ruta, directo);
    verificar(archivo.bloques() == arbol.disco().cantidad(), nombre + ": bloques del archivo");
    if (directo && sizeof(typename Arbol::Nodo) == 4096) verificar(archivo.directo(), nombre + ": se usa O_DIRECT");
    int64_t mn = INT32_MAX, mx = INT32_MIN;
    for (const Par& p : datos) {
        mn = std::min<int64_t>(mn, p.llave);
        mx = std::max<int64_t>(mx, p.llave);
    }
    if (datos.empty()) mn = mx = 0;
    std::vector<std::pair<int64_t, int64_t>> rangos = {
        {INT32_MIN, INT32_MAX}, {mn, mx}, {mn, mn}, {mx, mx}, {mx + 1, INT32_MAX}, {INT32_MIN, mn - 1}, {mx, mn}};
    std::mt19937_64 rng(12345);
    const int64_t ancho = std::max<int64_t>(1, mx - mn);
    for (int i = 0; i < consultas; ++i) {
        const int64_t l = mn - ancho / 20 + static_cast<int64_t>(rng() % static_cast<uint64_t>(ancho + ancho / 10 + 1));
        const int64_t w = static_cast<int64_t>(rng() % static_cast<uint64_t>(ancho / ((i % 3 == 0) ? 2 : 50) + 1));
        rangos.push_back({l, l + w});
    }
    for (int i = 0; i < 20 && !datos.empty(); ++i) {
        const int32_t t = datos[rng() % datos.size()].llave;
        rangos.push_back({t, t});
    }
    std::vector<Par> res;
    for (const auto& r : rangos) {
        const int32_t l = static_cast<int32_t>(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, r.first)));
        const int32_t u = static_cast<int32_t>(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, r.second)));
        res.clear();
        archivo.reiniciarLecturas();
        Arbol::buscarRango(archivo, l, u, res);
        const bool ok = mismoMulticonjunto(res, fuerzaBruta(datos, l, u));
        verificar(ok, nombre + ": búsqueda [" + std::to_string(l) + ", " + std::to_string(u) + "] devolvió " +
                          std::to_string(res.size()) + " pares, se esperaban " +
                          std::to_string(fuerzaBruta(datos, l, u).size()));
        if (l <= u) {
            verificar(archivo.lecturas() >= 1 && static_cast<int64_t>(archivo.lecturas()) <= arbol.disco().cantidad(),
                      nombre + ": I/Os de búsqueda dentro de [1, nodos]");
        } else {
            verificar(archivo.lecturas() == 0, nombre + ": rango vacío no lee bloques");
        }
    }
    std::remove(ruta.c_str());
}

/**
 * @brief Caso completo: construye el árbol, valida invariantes y prueba búsquedas.
 * @tparam Arbol   tipo de árbol.
 * @tparam Fabrica función sin argumentos que devuelve std::unique_ptr<Arbol>.
 * @param nombre        nombre del caso (entrada).
 * @param crear         fábrica (entrada).
 * @param datos         pares a insertar (entrada).
 * @param directo       usar O_DIRECT en las búsquedas (entrada).
 * @param validarCadaPaso validar después de cada inserción (entrada; solo para casos chicos).
 * @param consultas     cantidad de rangos aleatorios (entrada).
 */
template <class Arbol, class Fabrica>
void caso(const std::string& nombre, Fabrica crear, const std::vector<Par>& datos, bool directo = false,
          bool validarCadaPaso = false, int consultas = 60) {
    std::unique_ptr<Arbol> arbol = crear();
    std::string error;
    if (validarCadaPaso) {
        std::vector<Par> prefijo;
        for (const Par& p : datos) {
            arbol->insertar(p);
            prefijo.push_back(p);
            if (!validar(*arbol, prefijo, error)) {
                verificar(false, nombre + " (tras " + std::to_string(prefijo.size()) + " inserciones): " + error);
                return;
            }
        }
    } else {
        for (const Par& p : datos) arbol->insertar(p);
    }
    const bool ok = validar(*arbol, datos, error);
    verificar(ok, nombre + ": " + error);
    if (ok) probarBusquedas(*arbol, datos, nombre, directo, consultas);
}

/**
 * @brief Conteo exacto de I/Os del árbol B en un caso pequeño (b = 4).
 */
void probarConteoIOsB() {
    ArbolB<4> a;
    verificar(a.disco().lecturas() == 0 && a.disco().escrituras() == 1, "B: crear raíz = 1 escritura");
    for (int i = 0; i < 4; ++i) a.insertar({(i + 1) * 10, 0.0f});
    verificar(a.disco().lecturas() == 4 && a.disco().escrituras() == 5, "B: 4 inserciones en raíz hoja");
    a.insertar({25, 0.0f});  // raíz llena: split (2 al final + raíz) e inserción en hoja
    verificar(a.disco().lecturas() == 5 && a.disco().escrituras() == 9, "B: split de raíz");
    verificar(a.altura() == 2 && a.disco().cantidad() == 3, "B: altura 2 y 3 nodos tras split de raíz");
    const auto& r = a.disco().inspeccionar(0);
    verificar(r.es_interno == 1 && r.k == 1 && r.llaves_valores[0].llave == 20, "B: mediana en posición b/2");
    const auto& izq = a.disco().inspeccionar(r.hijos[0]);
    const auto& der = a.disco().inspeccionar(r.hijos[1]);
    verificar(izq.k == 1 && izq.llaves_valores[0].llave == 10, "B: hoja izquierda con b/2-1 pares");
    verificar(der.k == 3 && der.llaves_valores[0].llave == 25 && der.llaves_valores[2].llave == 40,
              "B: hoja derecha con b/2 pares + el insertado");
    a.insertar({5, 0.0f});  // raíz interna no llena: leer raíz + leer hoja + escribir hoja
    verificar(a.disco().lecturas() == 7 && a.disco().escrituras() == 10, "B: inserción en árbol de altura 2");
}

/**
 * @brief Conteo exacto de I/Os y contenido del árbol Bε en un caso pequeño (c = 4, l_max = 6, α = 0.5).
 */
void probarConteoIOsBe() {
    ArbolBe<4, 6> a(0.5);
    verificar(a.disco().lecturas() == 0 && a.disco().escrituras() == 1, "Bε: crear raíz = 1 escritura");
    for (int i = 1; i <= 6; ++i) a.insertar({i * 10, 0.0f});
    verificar(a.disco().lecturas() == 6 && a.disco().escrituras() == 7, "Bε: 6 inserciones en raíz hoja");
    a.insertar({70, 0.0f});  // split de raíz hoja: 2 al final + raíz nueva con 70 en su buffer
    verificar(a.disco().lecturas() == 7 && a.disco().escrituras() == 10, "Bε: split de raíz hoja");
    {
        const auto& r = a.disco().inspeccionar(0);
        verificar(r.es_interno == 1 && r.k_llaves == 1 && r.llaves[0] == 30 && r.k_buffer == 1 &&
                      r.buffer[0].llave == 70,
                  "Bε: raíz nueva con llave l_max/2 y el par en el buffer");
    }
    for (int t = 1; t <= 5; ++t) a.insertar({t, 0.0f});  // buffer de la raíz: 1,2,3,4,5,70 (lleno)
    verificar(a.disco().lecturas() == 12 && a.disco().escrituras() == 15, "Bε: inserciones en buffer de la raíz");
    // Flush: f = ceil(6/2) = 3, umbral = ceil(1.5) = 2. Solo el hijo 0 (5 pares) baja;
    // la hoja [10,20,30] recibe 1..5, desborda (8 > 6) y se divide en [1..4] y [5,10,20,30].
    a.insertar({6, 0.0f});
    verificar(a.disco().lecturas() == 14 && a.disco().escrituras() == 18, "Bε: flush con split de hoja");
    const auto& r = a.disco().inspeccionar(0);
    verificar(r.k_llaves == 2 && r.llaves[0] == 4 && r.llaves[1] == 30, "Bε: llave divisoria subida a la raíz");
    verificar(r.k_buffer == 2 && r.buffer[0].llave == 6 && r.buffer[1].llave == 70,
              "Bε: el grupo bajo el umbral se queda en el buffer");
    const auto& h0 = a.disco().inspeccionar(r.hijos[0]);
    const auto& h1 = a.disco().inspeccionar(r.hijos[1]);
    const auto& h2 = a.disco().inspeccionar(r.hijos[2]);
    verificar(h0.k_buffer == 4 && h0.buffer[0].llave == 1 && h0.buffer[3].llave == 4, "Bε: hoja izquierda");
    verificar(h1.k_buffer == 4 && h1.buffer[0].llave == 5 && h1.buffer[3].llave == 30, "Bε: hoja nueva");
    verificar(h2.k_buffer == 3 && h2.buffer[0].llave == 40, "Bε: hoja derecha intacta");
    verificar(a.contadores().vaciados == 1 && a.contadores().lotes == 1 && a.contadores().elementosEmpujados == 5,
              "Bε: contadores de flush");
}

/**
 * @brief Revisa que el umbral ceil(f·α), con f = ceil(k_buffer / (k_llaves + 1)), sea exacto
 *        aunque f·α no sea representable en punto flotante, y que un árbol con ese α sea válido.
 */
void probarUmbralExacto() {
    struct Caso {
        double alpha;      // α del árbol
        size_t kBuffer;    // pares en el buffer
        size_t kLlaves;    // llaves divisorias
        int64_t esperado;  // ceil(f·α) calculado a mano
    };
    const Caso casos[] = {
        {0.07, 100, 0, 7},   // en double, 100·0.07 = 7.000000000000001 y ceil daría 8
        {0.3, 10, 0, 3},     // f·α exacto
        {0.1, 3, 0, 1},      // 0.30000000000000004: ceil = 1
        {0.9, 384, 81, 5},   // f = ceil(384/82) = 5, ceil(4.5) = 5
        {0.99, 506, 4, 101}, // f = ceil(506/5) = 102, ceil(100.98) = 101
        {0.5, 161, 0, 81},   // ceil(80.5) = 81
        {0.01, 5, 4, 1},     // f = 1, ceil(0.01) = 1 (mínimo 1)
    };
    for (const Caso& c : casos) {
        ArbolBe<4, 6> a(c.alpha);
        const int64_t u = a.umbral(c.kBuffer, c.kLlaves);
        verificar(u == c.esperado, "umbral con α=" + std::to_string(c.alpha) + ", k_buffer=" + std::to_string(c.kBuffer) +
                                       ", k_llaves=" + std::to_string(c.kLlaves) + ": " + std::to_string(u) +
                                       ", se esperaba " + std::to_string(c.esperado));
    }
    ArbolBe<4, 6> a(0.07);
    std::vector<Par> v = generar("ascendente", 500, 1);
    for (const Par& p : v) a.insertar(p);
    std::string error;
    verificar(validar(a, v, error), "Bε α=0.07: " + error);
}

}  // namespace

/**
 * @brief Ejecuta todas las pruebas.
 * @return 0 si todas pasan, 1 si alguna falla.
 */
int main() {
    ::mkdir("build", 0755);
    std::printf("Conteo de I/Os en casos pequeños...\n");
    probarConteoIOsB();
    probarConteoIOsBe();
    probarUmbralExacto();

    const std::vector<std::string> tipos = {"aleatorio", "ascendente", "descendente", "duplicados", "constante",
                                            "extremos"};
    const std::vector<double> alphas = {0.01, 0.3, 0.5, 0.7, 0.99};

    std::printf("Árboles con parámetros pequeños (validación tras cada inserción)...\n");
    for (const std::string& t : tipos) {
        const std::vector<Par> v = generar(t, 400, 7);
        caso<ArbolB<4>>("B<4> " + t + " paso a paso", [] { return std::make_unique<ArbolB<4>>(); }, v, false, true, 10);
        for (double a : {0.01, 0.99}) {
            const std::string s = " a=" + std::to_string(a) + " " + t + " paso a paso";
            caso<ArbolBe<4, 4>>("Be<4,4>" + s, [a] { return std::make_unique<ArbolBe<4, 4>>(a); }, v, false, true, 10);
            caso<ArbolBe<4, 6>>("Be<4,6>" + s, [a] { return std::make_unique<ArbolBe<4, 6>>(a); }, v, false, true, 10);
        }
    }

    std::printf("Árboles con parámetros pequeños (3000 pares)...\n");
    for (const std::string& t : tipos) {
        const std::vector<Par> v = generar(t, 3000, 11);
        caso<ArbolB<4>>("B<4> " + t, [] { return std::make_unique<ArbolB<4>>(); }, v);
        caso<ArbolB<6>>("B<6> " + t, [] { return std::make_unique<ArbolB<6>>(); }, v);
        caso<ArbolB<340>>("B<340> " + t, [] { return std::make_unique<ArbolB<340>>(); }, v);
        for (double a : alphas) {
            const std::string s = " a=" + std::to_string(a) + " " + t;
            caso<ArbolBe<4, 4>>("Be<4,4>" + s, [a] { return std::make_unique<ArbolBe<4, 4>>(a); }, v);
            caso<ArbolBe<4, 6>>("Be<4,6>" + s, [a] { return std::make_unique<ArbolBe<4, 6>>(a); }, v);
            caso<ArbolBe<6, 10>>("Be<6,10>" + s, [a] { return std::make_unique<ArbolBe<6, 10>>(a); }, v);
            caso<ArbolBe<8, 4>>("Be<8,4>" + s, [a] { return std::make_unique<ArbolBe<8, 4>>(a); }, v);
        }
    }

    std::printf("Árbol vacío...\n");
    caso<ArbolB<340>>("B vacío", [] { return std::make_unique<ArbolB<340>>(); }, {}, true);
    caso<ArbolBe<4, 506>>("Be4 vacío", [] { return std::make_unique<ArbolBe<4, 506>>(0.5); }, {}, true);

    std::printf("Parámetros reales con 200000 pares (búsquedas con O_DIRECT)...\n");
    for (const std::string& t : {std::string("aleatorio"), std::string("ascendente"), std::string("duplicados")}) {
        const std::vector<Par> v = generar(t, 200000, 3);
        caso<ArbolB<340>>("B " + t, [] { return std::make_unique<ArbolB<340>>(); }, v, true, false, 40);
        for (double a : {0.1, 0.5, 0.9}) {
            const std::string s = " a=" + std::to_string(a) + " " + t;
            caso<ArbolBe<4, 506>>("Be4" + s, [a] { return std::make_unique<ArbolBe<4, 506>>(a); }, v, true, false, 40);
            caso<ArbolBe<126, 384>>("Be126" + s, [a] { return std::make_unique<ArbolBe<126, 384>>(a); }, v, true, false,
                                    40);
        }
    }

    const std::string rutaDatos = "data/datos.bin";
    struct stat st;
    if (::stat(rutaDatos.c_str(), &st) == 0) {
        std::printf("Muestra de datos.bin (2^17 pares, rangos de una semana)...\n");
        std::vector<Par> v;
        LectorDatos lector(rutaDatos, 1 << 17);
        lector.siguiente(v, 1 << 17);
        caso<ArbolB<340>>("B datos.bin", [] { return std::make_unique<ArbolB<340>>(); }, v, true, false, 100);
        caso<ArbolBe<4, 506>>("Be4 datos.bin", [] { return std::make_unique<ArbolBe<4, 506>>(0.5); }, v, true, false,
                              100);
        caso<ArbolBe<126, 384>>("Be126 datos.bin", [] { return std::make_unique<ArbolBe<126, 384>>(0.5); }, v, true,
                                false, 100);
    } else {
        std::printf("(sin data/datos.bin: se omite la prueba con datos reales)\n");
    }

    std::printf("%d verificaciones, %d fallas\n", gVerificaciones, gFallas);
    return gFallas == 0 ? 0 : 1;
}
