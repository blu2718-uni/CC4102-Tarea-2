/**
 * @file main.cpp
 * @brief Programa principal de la Tarea 2 (CC4102): construye árboles B y Bε con
 *        los datos de temperatura, los guarda en disco, hace búsquedas por rango y
 *        registra los resultados en archivos CSV.
 *
 * Comandos:
 *  - principal: experimento de la sección 4 (N = 2^15..2^26, 50 búsquedas).
 *  - alpha:     experimento bonus (N = 2^24, 8 valores de α, 20 búsquedas).
 *  - consulta:  construye un árbol, lo guarda y responde una búsqueda [l, u].
 *  - buscar:    responde una búsqueda [l, u] sobre un archivo de árbol ya guardado.
 */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "arbol_b.hpp"
#include "arbol_be.hpp"
#include "datos.hpp"
#include "medicion.hpp"

namespace {

using Reloj = std::chrono::steady_clock;
using ArbolB340 = ArbolB<340>;         ///< Árbol B de la tarea (b = 340).
using ArbolBe4 = ArbolBe<4, 506>;      ///< Árbol Bε con ε ≈ 0.25 (c = 4, l_max = 506).
using ArbolBe126 = ArbolBe<126, 384>;  ///< Árbol Bε con ε ≈ 0.75 (c = 126, l_max = 384).

constexpr int64_t kLlaveMin = 1546300800;   ///< Mínimo del rango de l (enunciado).
constexpr int64_t kLlaveMax = 1754006400;   ///< Máximo del rango de l (enunciado).
constexpr int64_t kAnchoRango = 604800;     ///< Ancho de cada búsqueda: una semana en segundos.
constexpr size_t kParesPorBloque = 1 << 16; ///< Pares que se leen de datos.bin en cada fread.

/// α que se usa en un árbol Bε si no se indica con --alpha-be4/--alpha-be126 y no existe
/// resultados/alpha_<tipo>_elegido.csv (el archivo que escribe el comando `alpha`).
constexpr double kAlphaPorDefecto = 0.9;

/// Regla para elegir α en el bonus: el menor α cuyas I/Os de carga estén a lo más a esta
/// fracción del mínimo entre los α probados...
constexpr double kToleranciaCarga = 0.005;
/// ... y cuyas I/Os promedio de búsqueda estén a lo más a esta fracción del mínimo.
constexpr double kToleranciaBusqueda = 0.03;

/// @brief Opciones de línea de comandos (ver ayuda()).
struct Opciones {
    std::string comando;                                  ///< principal | alpha | consulta | buscar.
    std::string datos = "data/datos.bin";                 ///< Archivo de datos.
    std::string resultados = "resultados";                ///< Carpeta de los CSV de salida.
    std::string arboles = "arboles";                      ///< Carpeta de los archivos de árboles.
    int nMin = 15;                                        ///< Exponente mínimo de N (principal).
    int nMax = 26;                                        ///< Exponente máximo de N (principal).
    int nAlpha = 24;                                      ///< Exponente de N (alpha) o N de consulta.
    int busquedas = -1;                                   ///< Búsquedas por árbol (-1: 50 o 20 según el comando).
    int repeticiones = 1;                                 ///< Veces que se repite cada construcción y sus búsquedas.
    uint64_t semilla = 20261015;                          ///< Semilla de los rangos aleatorios.
    double alphaBe4 = -1;                                 ///< α del árbol Bε c=4 (-1: elegido o por defecto).
    double alphaBe126 = -1;                               ///< α del árbol Bε c=126 (-1: elegido o por defecto).
    int mib = 512;                                        ///< MiB del archivo de prueba (latencia).
    int lecturasLatencia = 3000;                          ///< Lecturas por modo (latencia).
    std::vector<std::string> tipos = {"B", "BE4", "BE126"};  ///< Árboles del experimento principal.
    std::vector<int> guardarConsulta = {16, 20, 26};      ///< Exponentes para los que se guarda la búsqueda 0.
    std::string arbolAlpha = "BE126";                     ///< Árbol del experimento alpha.
    std::vector<double> alphas = {0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 0.99};  ///< Valores de α (bonus).
    bool directo = true;                                  ///< Búsquedas con O_DIRECT.
    bool conservar = false;                               ///< No borrar los archivos de árboles.
    std::string tipo = "B";                               ///< Tipo de árbol (consulta/buscar).
    std::string archivo;                                  ///< Archivo de árbol (buscar).
    double alpha = -1;                                    ///< α (consulta); -1 = por defecto del tipo.
    int64_t l = -1;                                       ///< Límite inferior (consulta/buscar).
    int64_t u = -1;                                       ///< Límite superior (consulta/buscar).
    std::string salida;                                   ///< CSV de salida (consulta/buscar); vacío = stdout.
};

/// @brief Un rango de búsqueda [l, u].
struct Consulta {
    int id;     ///< Número de la consulta.
    int32_t l;  ///< Límite inferior inclusive.
    int32_t u;  ///< Límite superior inclusive.
};

/**
 * @brief Resumen de un multiconjunto de pares, para comparar los resultados de una
 *        búsqueda con los de la fuerza bruta sin guardar todos los pares.
 */
struct Resumen {
    uint64_t cantidad = 0;   ///< Cantidad de pares.
    int64_t sumaLlaves = 0;  ///< Suma de las llaves.
    uint64_t sumaBits = 0;   ///< Suma de los bits de los valores (float como uint32).

    /**
     * @brief Suma un par al resumen.
     * @param p par a agregar (entrada).
     */
    void agregar(const Par& p) {
        ++cantidad;
        sumaLlaves += p.llave;
        uint32_t bits;
        std::memcpy(&bits, &p.valor, sizeof bits);
        sumaBits += bits;
    }

    /**
     * @brief Compara dos resúmenes.
     * @param o otro resumen (entrada).
     * @return true si los tres campos coinciden.
     */
    bool operator==(const Resumen& o) const {
        return cantidad == o.cantidad && sumaLlaves == o.sumaLlaves && sumaBits == o.sumaBits;
    }
};

/// @brief Medición de una búsqueda por rango.
struct MedicionBusqueda {
    int id;               ///< Número de la consulta.
    int32_t l;            ///< Límite inferior.
    int32_t u;            ///< Límite superior.
    uint64_t resultados;  ///< Pares encontrados.
    uint64_t ios;         ///< Bloques leídos del archivo.
    double ms;            ///< Tiempo de la búsqueda en milisegundos.
    bool correcta;        ///< Coincide con la fuerza bruta.
};

/// @brief Mediciones de un árbol construido con N pares.
struct MedicionArbol {
    std::string tipo;          ///< B, BE4 o BE126.
    double alpha = 0;          ///< α (0 si es árbol B).
    int exponente = 0;         ///< N = 2^exponente.
    uint64_t n = 0;            ///< Pares insertados.
    int repeticion = 0;        ///< Número de repetición (0, 1, ...).
    double tInsercion = 0;     ///< Segundos insertando (sin contar la lectura de datos.bin).
    double tLecturaDatos = 0;  ///< Segundos leyendo datos.bin.
    double tEscritura = 0;     ///< Segundos guardando el árbol en el archivo.
    uint64_t lecturas = 0;     ///< Lecturas simuladas durante la inserción.
    uint64_t escrituras = 0;   ///< Escrituras simuladas durante la inserción.
    EstadisticasArbol est;     ///< Estructura del árbol.
    ContadoresArbol cont;      ///< Contadores de flush/splits.
    bool directo = false;      ///< Las búsquedas leyeron de verdad del disco (O_DIRECT sin problemas).
    std::string motivo;        ///< Si directo es false, el motivo (ver ArchivoArbol::motivo()).
    double tCpuInsercion = 0;  ///< Tiempo de CPU del proceso durante las inserciones (s).
    Interferencia interIns;    ///< Interferencia durante la construcción (lectura de datos + inserciones).
    Interferencia interBus;    ///< Interferencia durante las búsquedas.
    int64_t memDisponible = -1;  ///< MiB disponibles en la máquina al empezar la construcción.
    std::vector<MedicionBusqueda> busquedas;  ///< Una entrada por búsqueda.
};

/**
 * @brief Escribe un mensaje de progreso en stderr con la hora local.
 * @param msg texto del mensaje (entrada).
 */
void registrar(const std::string& msg) {
    const std::time_t t = std::time(nullptr);
    char hora[16];
    std::strftime(hora, sizeof hora, "%H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "[%s] %s\n", hora, msg.c_str());
    std::fflush(stderr);
}

/**
 * @brief Crea una carpeta y sus padres (como mkdir -p).
 * @param ruta carpeta a crear (entrada).
 */
void crearCarpeta(const std::string& ruta) {
    std::string actual;
    std::stringstream ss(ruta);
    std::string parte;
    if (!ruta.empty() && ruta[0] == '/') actual = "/";
    while (std::getline(ss, parte, '/')) {
        if (parte.empty()) continue;
        actual += parte + "/";
        if (::mkdir(actual.c_str(), 0755) != 0 && errno != EEXIST) {
            throw std::runtime_error("no se pudo crear la carpeta " + actual);
        }
    }
}

/**
 * @brief Genera rangos [l, l + 604800] con l uniforme en [1546300800, 1754006400].
 *
 * Usa mt19937_64, que da la misma secuencia en cualquier plataforma. El sesgo
 * del módulo es menor que 10^-10.
 *
 * @param cantidad cantidad de rangos (entrada).
 * @param semilla  semilla del generador (entrada).
 * @return rangos generados, con id = 0..cantidad-1.
 */
std::vector<Consulta> generarConsultas(int cantidad, uint64_t semilla) {
    std::mt19937_64 rng(semilla);
    const uint64_t rango = static_cast<uint64_t>(kLlaveMax - kLlaveMin + 1);
    std::vector<Consulta> v;
    for (int i = 0; i < cantidad; ++i) {
        const int64_t l = kLlaveMin + static_cast<int64_t>(rng() % rango);
        v.push_back({i, static_cast<int32_t>(l), static_cast<int32_t>(l + kAnchoRango)});
    }
    return v;
}

/**
 * @brief Calcula por fuerza bruta (recorriendo los primeros n pares) el resumen
 *        esperado de cada consulta. Sirve para verificar las búsquedas.
 * @param ruta      ruta de datos.bin (entrada).
 * @param n         cantidad de pares desde el inicio (entrada).
 * @param consultas rangos a evaluar (entrada).
 * @return un resumen por consulta, en el mismo orden.
 */
std::vector<Resumen> fuerzaBruta(const std::string& ruta, uint64_t n, const std::vector<Consulta>& consultas) {
    std::vector<Resumen> r(consultas.size());
    LectorDatos lector(ruta, n);
    std::vector<Par> bloque;
    while (const size_t k = lector.siguiente(bloque, kParesPorBloque)) {
        for (size_t i = 0; i < k; ++i) {
            const Par& p = bloque[i];
            for (size_t q = 0; q < consultas.size(); ++q) {
                if (p.llave >= consultas[q].l && p.llave <= consultas[q].u) r[q].agregar(p);
            }
        }
    }
    return r;
}

/**
 * @brief Inserta uno a uno los primeros n pares de datos.bin en un árbol.
 *
 * El archivo se lee por bloques y solo se mide el tiempo de las inserciones.
 *
 * @tparam Arbol tipo de árbol (ArbolB o ArbolBe).
 * @param arbol      árbol donde se insertan los pares (entrada/salida).
 * @param ruta       ruta de datos.bin (entrada).
 * @param n          cantidad de pares (entrada).
 * @param tLectura   segundos gastados leyendo datos.bin (salida).
 * @param tCpu       segundos de CPU del proceso gastados en las inserciones (salida).
 * @return segundos de reloj gastados en las inserciones.
 */
template <class Arbol>
double construir(Arbol& arbol, const std::string& ruta, uint64_t n, double& tLectura, double& tCpu) {
    LectorDatos lector(ruta, n);
    std::vector<Par> bloque;
    double segundos = 0;
    tLectura = 0;
    tCpu = 0;
    while (true) {
        const auto t0 = Reloj::now();
        const size_t k = lector.siguiente(bloque, kParesPorBloque);
        const auto t1 = Reloj::now();
        tLectura += std::chrono::duration<double>(t1 - t0).count();
        if (k == 0) break;
        const double c0 = tiempoCpu();
        for (size_t i = 0; i < k; ++i) arbol.insertar(bloque[i]);
        tCpu += tiempoCpu() - c0;
        segundos += std::chrono::duration<double>(Reloj::now() - t1).count();
    }
    return segundos;
}

/**
 * @brief Calcula el resumen de los pares que devolvió una búsqueda.
 * @param v pares encontrados (entrada).
 * @return resumen de v.
 */
Resumen resumir(const std::vector<Par>& v) {
    Resumen r;
    for (const Par& p : v) r.agregar(p);
    return r;
}

/**
 * @brief Construye un árbol, lo guarda en disco, libera la RAM y mide las búsquedas
 *        leyendo desde el archivo.
 *
 * @tparam Arbol   tipo de árbol.
 * @tparam Fabrica función sin argumentos que devuelve std::unique_ptr<Arbol>.
 * @param tipo       nombre del árbol (B, BE4, BE126) (entrada).
 * @param alpha      α usado (0 para el árbol B) (entrada).
 * @param crear      fábrica del árbol (entrada).
 * @param op         opciones (rutas, O_DIRECT, etc.) (entrada).
 * @param exponente  N = 2^exponente (entrada).
 * @param consultas  rangos a buscar (entrada).
 * @param esperado   resumen esperado por consulta, o nullptr para no verificar (entrada).
 * @param consulta0  si no es nullptr, recibe los pares de la consulta 0, ordenados (salida).
 * @return mediciones de construcción, tamaño y búsquedas.
 */
template <class Arbol, class Fabrica>
MedicionArbol medirArbol(const std::string& tipo, double alpha, Fabrica crear, const Opciones& op, int exponente,
                         const std::vector<Consulta>& consultas, const std::vector<Resumen>* esperado,
                         std::vector<Par>* consulta0) {
    MedicionArbol m;
    m.tipo = tipo;
    m.alpha = alpha;
    m.exponente = exponente;
    m.n = 1ULL << exponente;
    std::ostringstream nombre;
    nombre << op.arboles << "/" << tipo;
    if (alpha > 0) nombre << "_a" << alpha;
    nombre << "_N" << exponente << ".bin";
    const std::string ruta = nombre.str();
    m.memDisponible = memoriaDisponibleMiB();
    {
        std::unique_ptr<Arbol> arbol = crear();
        const Muestra antes = tomarMuestra();
        m.tInsercion = construir(*arbol, op.datos, m.n, m.tLecturaDatos, m.tCpuInsercion);
        m.interIns = diferencia(antes, tomarMuestra());
        m.lecturas = arbol->disco().lecturas();
        m.escrituras = arbol->disco().escrituras();
        m.est = arbol->estadisticas();
        m.cont = arbol->contadores();
        const auto t0 = Reloj::now();
        arbol->disco().guardar(ruta);
        m.tEscritura = std::chrono::duration<double>(Reloj::now() - t0).count();
    }  // Aquí se destruye el árbol y se libera su RAM antes de buscar.
    ArchivoArbol<typename Arbol::Nodo> archivo(ruta, op.directo);
    m.directo = archivo.directo();
    m.motivo = archivo.motivo();
    std::vector<Par> resultado;
    const Muestra antesBusquedas = tomarMuestra();
    for (const Consulta& c : consultas) {
        resultado.clear();
        archivo.reiniciarLecturas();
        const auto t0 = Reloj::now();
        Arbol::buscarRango(archivo, c.l, c.u, resultado);
        const double ms = std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
        const bool ok = esperado == nullptr || resumir(resultado) == (*esperado)[static_cast<size_t>(c.id)];
        m.busquedas.push_back({c.id, c.l, c.u, resultado.size(), archivo.lecturas(), ms, ok});
        if (c.id == 0 && consulta0 != nullptr) {
            *consulta0 = resultado;
            std::sort(consulta0->begin(), consulta0->end(), CompararLlave{});
        }
    }
    m.interBus = diferencia(antesBusquedas, tomarMuestra());
    if (!op.conservar) std::remove(ruta.c_str());
    return m;
}

/**
 * @brief Construye y mide un árbol según su nombre de tipo.
 * @param tipo      B, BE4 o BE126 (entrada).
 * @param alpha     α a usar en los Bε (entrada).
 * @param op        opciones (entrada).
 * @param exponente N = 2^exponente (entrada).
 * @param consultas rangos a buscar (entrada).
 * @param esperado  resúmenes esperados o nullptr (entrada).
 * @param consulta0 pares de la consulta 0 o nullptr (salida).
 * @return mediciones del árbol.
 */
MedicionArbol medirTipo(const std::string& tipo, double alpha, const Opciones& op, int exponente,
                        const std::vector<Consulta>& consultas, const std::vector<Resumen>* esperado,
                        std::vector<Par>* consulta0) {
    // Cada rama pasa una fábrica (lambda sin argumentos) que crea un árbol vacío del tipo pedido.
    if (tipo == "B") {
        return medirArbol<ArbolB340>(tipo, 0.0, [] { return std::make_unique<ArbolB340>(); }, op, exponente,
                                     consultas, esperado, consulta0);
    }
    if (tipo == "BE4") {
        return medirArbol<ArbolBe4>(tipo, alpha, [alpha] { return std::make_unique<ArbolBe4>(alpha); }, op,
                                    exponente, consultas, esperado, consulta0);
    }
    if (tipo == "BE126") {
        return medirArbol<ArbolBe126>(tipo, alpha, [alpha] { return std::make_unique<ArbolBe126>(alpha); }, op,
                                      exponente, consultas, esperado, consulta0);
    }
    throw std::invalid_argument("tipo de árbol desconocido: " + tipo + " (use B, BE4 o BE126)");
}

/**
 * @brief α por defecto de un tipo de árbol.
 * @param tipo B, BE4 o BE126 (entrada).
 * @param op   opciones con los α configurados (entrada).
 * @return α del tipo (0 para B).
 */
double alphaDe(const std::string& tipo, const Opciones& op) {
    if (tipo == "BE4") return op.alphaBe4;
    if (tipo == "BE126") return op.alphaBe126;
    return 0.0;
}

/**
 * @brief Lee un CSV simple (sin comillas) completo.
 * @param ruta archivo CSV (entrada).
 * @param filas filas como mapas columna -> texto (salida).
 * @return false si el archivo no existe.
 */
bool leerCsv(const std::string& ruta, std::vector<std::vector<std::pair<std::string, std::string>>>& filas) {
    std::FILE* f = std::fopen(ruta.c_str(), "r");
    if (f == nullptr) return false;
    std::vector<std::string> encabezado;
    char buf[4096];
    bool primera = true;
    while (std::fgets(buf, sizeof buf, f) != nullptr) {
        std::string linea(buf);
        while (!linea.empty() && (linea.back() == '\n' || linea.back() == '\r')) linea.pop_back();
        if (linea.empty()) continue;
        std::vector<std::string> campos;
        std::stringstream ss(linea);
        std::string x;
        while (std::getline(ss, x, ',')) campos.push_back(x);
        if (!linea.empty() && linea.back() == ',') campos.push_back("");
        if (primera) {
            encabezado = campos;
            primera = false;
            continue;
        }
        std::vector<std::pair<std::string, std::string>> fila;
        for (size_t i = 0; i < encabezado.size(); ++i) fila.push_back({encabezado[i], i < campos.size() ? campos[i] : ""});
        filas.push_back(fila);
    }
    std::fclose(f);
    return true;
}

/**
 * @brief Busca el valor de una columna en una fila leída con leerCsv().
 * @param fila fila (entrada).
 * @param columna nombre de la columna (entrada).
 * @return texto del campo; lanza una excepción si la columna no existe.
 */
const std::string& campo(const std::vector<std::pair<std::string, std::string>>& fila, const std::string& columna) {
    for (const auto& c : fila) {
        if (c.first == columna) return c.second;
    }
    throw std::runtime_error("falta la columna " + columna);
}

/**
 * @brief Lee el α elegido por el experimento bonus para un árbol Bε.
 * @param dir carpeta de resultados (entrada).
 * @param tipo BE4 o BE126 (entrada).
 * @param alpha α elegido (salida).
 * @return true si existe resultados/alpha_<tipo>_elegido.csv.
 */
bool leerAlphaElegido(const std::string& dir, const std::string& tipo, double& alpha) {
    std::vector<std::vector<std::pair<std::string, std::string>>> filas;
    if (!leerCsv(dir + "/alpha_" + tipo + "_elegido.csv", filas) || filas.empty()) return false;
    alpha = std::stod(campo(filas[0], "alpha"));
    if (!(alpha > 0 && alpha < 1)) throw std::runtime_error("α inválido en alpha_" + tipo + "_elegido.csv");
    return true;
}

/**
 * @brief Completa los α de los Bε que no se dieron por línea de comandos: usa el α elegido
 *        por el experimento bonus (si ya se ejecutó) o kAlphaPorDefecto, y lo informa.
 * @param op opciones; se completan alphaBe4 y alphaBe126 (entrada/salida).
 */
void resolverAlphas(Opciones& op) {
    for (auto par : {std::make_pair(std::string("BE4"), &op.alphaBe4), std::make_pair(std::string("BE126"), &op.alphaBe126)}) {
        if (*par.second > 0) continue;
        double a;
        if (leerAlphaElegido(op.resultados, par.first, a)) {
            *par.second = a;
            registrar(par.first + ": se usa el α elegido por el experimento bonus, " + std::to_string(a));
        } else {
            *par.second = kAlphaPorDefecto;
            registrar(par.first + ": no existe " + op.resultados + "/alpha_" + par.first +
                      "_elegido.csv (ejecute primero `make alpha`); se usa α = " + std::to_string(kAlphaPorDefecto));
        }
    }
}

/**
 * @brief Elige α a partir de los resultados del bonus de un árbol Bε y lo guarda en
 *        resultados/alpha_<tipo>_elegido.csv.
 *
 * Regla: el menor α cuyas I/Os de carga (deterministas) estén a lo más kToleranciaCarga
 * sobre el mínimo y cuyas I/Os promedio de búsqueda estén a lo más kToleranciaBusqueda
 * sobre el mínimo. Se usan I/Os y no tiempos porque las I/Os no dependen de la máquina;
 * entre los α que cumplen, el menor hace menos vaciados (menos CPU).
 *
 * @param dir carpeta de resultados (entrada).
 * @param tipo BE4 o BE126 (entrada).
 * @return α elegido.
 */
double elegirAlpha(const std::string& dir, const std::string& tipo) {
    std::vector<std::vector<std::pair<std::string, std::string>>> arboles, busquedas;
    if (!leerCsv(dir + "/alpha_" + tipo + ".csv", arboles) || arboles.empty() ||
        !leerCsv(dir + "/alpha_" + tipo + "_busquedas.csv", busquedas) || busquedas.empty()) {
        throw std::runtime_error("faltan los resultados del experimento alpha para " + tipo);
    }
    std::vector<double> alphas;
    std::vector<double> carga, sumaBus, cuentaBus;
    auto indice = [&](double a) -> size_t {  // posición de α en los vectores (lo agrega si no está)
        for (size_t i = 0; i < alphas.size(); ++i) {
            if (std::fabs(alphas[i] - a) < 1e-12) return i;
        }
        alphas.push_back(a);
        carga.push_back(-1);
        sumaBus.push_back(0);
        cuentaBus.push_back(0);
        return alphas.size() - 1;
    };
    for (const auto& f : arboles) {
        const size_t i = indice(std::stod(campo(f, "alpha")));
        carga[i] = std::stod(campo(f, "ios_insercion")) / std::stod(campo(f, "N"));
    }
    for (const auto& f : busquedas) {
        const size_t i = indice(std::stod(campo(f, "alpha")));
        sumaBus[i] += std::stod(campo(f, "ios"));
        cuentaBus[i] += 1;
    }
    double minCarga = 1e300, minBus = 1e300;
    for (size_t i = 0; i < alphas.size(); ++i) {
        minCarga = std::min(minCarga, carga[i]);
        minBus = std::min(minBus, sumaBus[i] / cuentaBus[i]);
    }
    // Primero se exigen ambas tolerancias; si ningún α las cumple a la vez, manda la carga
    // (siempre hay al menos un α con la carga mínima).
    size_t elegido = alphas.size();
    std::string criterio = "carga_y_busqueda";
    for (int intento = 0; intento < 2 && elegido == alphas.size(); ++intento) {
        if (intento == 1) criterio = "solo_carga";
        for (size_t i = 0; i < alphas.size(); ++i) {
            const double bus = sumaBus[i] / cuentaBus[i];
            const bool cumple = carga[i] <= minCarga * (1 + kToleranciaCarga) &&
                                (intento == 1 || bus <= minBus * (1 + kToleranciaBusqueda));
            if (cumple && (elegido == alphas.size() || alphas[i] < alphas[elegido])) elegido = i;
        }
    }
    std::FILE* f = std::fopen((dir + "/alpha_" + tipo + "_elegido.csv").c_str(), "w");
    if (f == nullptr) throw std::runtime_error("no se pudo escribir el α elegido");
    std::fprintf(f, "tipo,alpha,ios_por_par,ios_busqueda,ios_por_par_min,ios_busqueda_min,tolerancia_carga,"
                    "tolerancia_busqueda,criterio\n");
    std::fprintf(f, "%s,%g,%.6f,%.3f,%.6f,%.3f,%g,%g,%s\n", tipo.c_str(), alphas[elegido], carga[elegido],
                 sumaBus[elegido] / cuentaBus[elegido], minCarga, minBus, kToleranciaCarga, kToleranciaBusqueda,
                 criterio.c_str());
    std::fclose(f);
    registrar(tipo + ": α elegido = " + std::to_string(alphas[elegido]));
    return alphas[elegido];
}

/**
 * @brief Calcula el promedio y la desviación estándar muestral.
 * @param v valores (entrada).
 * @param prom promedio (salida).
 * @param desv desviación estándar (salida; 0 si hay menos de 2 valores).
 */
void promedioDesv(const std::vector<double>& v, double& prom, double& desv) {
    prom = 0;
    desv = 0;
    if (v.empty()) return;
    for (double x : v) prom += x;
    prom /= static_cast<double>(v.size());
    if (v.size() < 2) return;
    double s = 0;
    for (double x : v) s += (x - prom) * (x - prom);
    desv = std::sqrt(s / static_cast<double>(v.size() - 1));
}

/// Encabezado de los CSV con una fila por árbol.
const char* kEncabezadoArbol =
    "tipo,alpha,exponente,N,repeticion,t_insercion_s,t_lectura_datos_s,lecturas_insercion,escrituras_insercion,"
    "ios_insercion,nodos,bytes,altura,nodos_internos,hojas,elementos_en_internos,elementos_en_hojas,"
    "vaciados,lotes,elementos_empujados,divisiones_hoja,divisiones_internas,t_escritura_s,busquedas,"
    "t_busqueda_prom_ms,t_busqueda_desv_ms,ios_busqueda_prom,ios_busqueda_desv,resultados_prom,odirect,"
    "verificado,t_cpu_insercion_s,cambios_invol_insercion,fallos_mayores_insercion,psi_cpu_insercion_ms,"
    "psi_mem_insercion_ms,steal_insercion_ms,cambios_invol_busqueda,fallos_mayores_busqueda,psi_cpu_busqueda_ms,"
    "psi_mem_busqueda_ms,steal_busqueda_ms,mem_disponible_mib,motivo_sin_odirect\n";

/// Encabezado de los CSV con una fila por búsqueda.
const char* kEncabezadoBusqueda = "tipo,alpha,exponente,N,repeticion,id,l,u,resultados,ios,tiempo_ms,correcta\n";

/**
 * @brief Escribe una fila con las mediciones de un árbol.
 * @param f archivo CSV abierto (entrada/salida).
 * @param m mediciones (entrada).
 * @return true si todas las búsquedas coincidieron con la fuerza bruta.
 */
bool escribirFilaArbol(std::FILE* f, const MedicionArbol& m) {
    std::vector<double> tiempos, ios, res;
    bool ok = true;
    for (const MedicionBusqueda& b : m.busquedas) {
        tiempos.push_back(b.ms);
        ios.push_back(static_cast<double>(b.ios));
        res.push_back(static_cast<double>(b.resultados));
        ok = ok && b.correcta;
    }
    double tProm, tDesv, ioProm, ioDesv, resProm, resDesv;
    promedioDesv(tiempos, tProm, tDesv);
    promedioDesv(ios, ioProm, ioDesv);
    promedioDesv(res, resProm, resDesv);
    std::fprintf(f,
                 "%s,%g,%d,%llu,%d,%.6f,%.6f,%llu,%llu,%llu,%lld,%lld,%d,%lld,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
                 "%.6f,%zu,%.6f,%.6f,%.3f,%.3f,%.3f,%d,%d,%.6f,%lld,%lld,%.3f,%.3f,%.3f,%lld,%lld,%.3f,%.3f,%.3f,%lld,%s\n",
                 m.tipo.c_str(), m.alpha, m.exponente, static_cast<unsigned long long>(m.n), m.repeticion,
                 m.tInsercion,
                 m.tLecturaDatos, static_cast<unsigned long long>(m.lecturas),
                 static_cast<unsigned long long>(m.escrituras),
                 static_cast<unsigned long long>(m.lecturas + m.escrituras), static_cast<long long>(m.est.nodos),
                 static_cast<long long>(m.est.nodos) * 4096LL, m.est.altura, static_cast<long long>(m.est.internos),
                 static_cast<long long>(m.est.hojas), static_cast<unsigned long long>(m.est.elementosEnInternos),
                 static_cast<unsigned long long>(m.est.elementosEnHojas),
                 static_cast<unsigned long long>(m.cont.vaciados), static_cast<unsigned long long>(m.cont.lotes),
                 static_cast<unsigned long long>(m.cont.elementosEmpujados),
                 static_cast<unsigned long long>(m.cont.divisionesHoja),
                 static_cast<unsigned long long>(m.cont.divisionesInternas), m.tEscritura, m.busquedas.size(), tProm,
                 tDesv, ioProm, ioDesv, resProm, m.directo ? 1 : 0, ok ? 1 : 0, m.tCpuInsercion,
                 static_cast<long long>(m.interIns.involuntarios), static_cast<long long>(m.interIns.mayores),
                 m.interIns.psiCpuMs, m.interIns.psiMemMs, m.interIns.stealMs,
                 static_cast<long long>(m.interBus.involuntarios), static_cast<long long>(m.interBus.mayores),
                 m.interBus.psiCpuMs, m.interBus.psiMemMs, m.interBus.stealMs, static_cast<long long>(m.memDisponible),
                 m.motivo.c_str());
    std::fflush(f);
    return ok;
}

/**
 * @brief Escribe una fila por búsqueda de un árbol.
 * @param f archivo CSV abierto (entrada/salida).
 * @param m mediciones (entrada).
 */
void escribirFilasBusquedas(std::FILE* f, const MedicionArbol& m) {
    for (const MedicionBusqueda& b : m.busquedas) {
        std::fprintf(f, "%s,%g,%d,%llu,%d,%d,%d,%d,%llu,%llu,%.6f,%d\n", m.tipo.c_str(), m.alpha, m.exponente,
                     static_cast<unsigned long long>(m.n), m.repeticion, b.id, b.l, b.u,
                     static_cast<unsigned long long>(b.resultados), static_cast<unsigned long long>(b.ios), b.ms,
                     b.correcta ? 1 : 0);
    }
    std::fflush(f);
}

/**
 * @brief Abre un CSV para escribir (lo trunca) y le pone el encabezado.
 * @param ruta       ruta del archivo (entrada).
 * @param encabezado primera línea (entrada).
 * @return archivo abierto.
 */
std::FILE* abrirCsv(const std::string& ruta, const char* encabezado) {
    std::FILE* f = std::fopen(ruta.c_str(), "w");
    if (f == nullptr) throw std::runtime_error("no se pudo crear " + ruta);
    std::fputs(encabezado, f);
    std::fflush(f);
    return f;
}

/**
 * @brief Guarda pares en un CSV con columnas llave,valor.
 * @param ruta  archivo de salida; vacío = salida estándar (entrada).
 * @param pares pares a escribir (entrada).
 */
void guardarPares(const std::string& ruta, const std::vector<Par>& pares) {
    std::FILE* f = ruta.empty() ? stdout : std::fopen(ruta.c_str(), "w");
    if (f == nullptr) throw std::runtime_error("no se pudo crear " + ruta);
    std::fputs("llave,valor\n", f);
    for (const Par& p : pares) std::fprintf(f, "%d,%.4f\n", p.llave, static_cast<double>(p.valor));
    if (f != stdout) std::fclose(f);
}

/**
 * @brief Guarda los rangos de búsqueda usados.
 * @param ruta      archivo CSV de salida (entrada).
 * @param consultas rangos (entrada).
 */
void guardarConsultas(const std::string& ruta, const std::vector<Consulta>& consultas) {
    std::FILE* f = abrirCsv(ruta, "id,l,u\n");
    for (const Consulta& c : consultas) std::fprintf(f, "%d,%d,%d\n", c.id, c.l, c.u);
    std::fclose(f);
}

/**
 * @brief Arma un mensaje de progreso con las mediciones de un árbol.
 * @param m mediciones (entrada).
 * @return texto de una línea.
 */
std::string describir(const MedicionArbol& m) {
    double ms = 0, ios = 0, res = 0;
    bool ok = true;
    for (const MedicionBusqueda& b : m.busquedas) {
        ms += b.ms;
        ios += static_cast<double>(b.ios);
        res += static_cast<double>(b.resultados);
        ok = ok && b.correcta;
    }
    const double k = m.busquedas.empty() ? 1.0 : static_cast<double>(m.busquedas.size());
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "N=2^%d rep %d %-5s a=%-5g insercion %.2fs, I/Os %llu (%.3f por par), %lld nodos (%.1f MiB), "
                  "altura %d, busqueda %.3f ms, %.1f I/Os, %.1f resultados, %s%s",
                  m.exponente, m.repeticion, m.tipo.c_str(), m.alpha, m.tInsercion,
                  static_cast<unsigned long long>(m.lecturas + m.escrituras),
                  static_cast<double>(m.lecturas + m.escrituras) / static_cast<double>(m.n),
                  static_cast<long long>(m.est.nodos), static_cast<double>(m.est.nodos) * 4096.0 / (1 << 20),
                  m.est.altura, ms / k, ios / k, res / k, ok ? "OK" : "ERROR EN BUSQUEDAS",
                  m.directo ? "" : (" (ATENCIÓN: las búsquedas no leyeron directo del disco: " + m.motivo + ")").c_str());
    return buf;
}

/**
 * @brief Experimento principal (sección 4): para cada N = 2^nMin..2^nMax construye
 *        los tres árboles, los guarda, hace las búsquedas y escribe los CSV.
 *
 * Con --repeticiones R, cada construcción y sus búsquedas se repiten R veces (los
 * árboles se van alternando dentro de cada repetición). Los rangos son los mismos
 * para todo N y toda repetición.
 *
 * @param op opciones (entrada).
 * @return 0 si todas las búsquedas coincidieron con la fuerza bruta, 2 si no.
 */
int comandoPrincipal(const Opciones& op) {
    const int busquedas = op.busquedas > 0 ? op.busquedas : 50;
    crearCarpeta(op.resultados);
    crearCarpeta(op.arboles);
    const std::vector<Consulta> consultas = generarConsultas(busquedas, op.semilla);
    guardarConsultas(op.resultados + "/consultas_principal.csv", consultas);
    std::FILE* fArboles = abrirCsv(op.resultados + "/principal.csv", kEncabezadoArbol);
    std::FILE* fBusq = abrirCsv(op.resultados + "/principal_busquedas.csv", kEncabezadoBusqueda);
    bool todoOk = true;
    for (int e = op.nMin; e <= op.nMax; ++e) {
        const uint64_t n = 1ULL << e;
        registrar("N=2^" + std::to_string(e) + ": calculando resultados esperados por fuerza bruta");
        const std::vector<Resumen> esperado = fuerzaBruta(op.datos, n, consultas);
        const bool guardar = std::find(op.guardarConsulta.begin(), op.guardarConsulta.end(), e) != op.guardarConsulta.end();
        std::vector<Par> consulta0;
        bool consultaGuardada = false;
        for (int rep = 0; rep < op.repeticiones; ++rep) {
            // El orden de los árboles rota en cada repetición, para que una interferencia
            // pasajera no caiga siempre sobre el mismo árbol.
            for (size_t j = 0; j < op.tipos.size(); ++j) {
                const std::string& tipo = op.tipos[(j + static_cast<size_t>(rep)) % op.tipos.size()];
                registrar("N=2^" + std::to_string(e) + " rep " + std::to_string(rep) + ": construyendo " + tipo);
                MedicionArbol m = medirTipo(tipo, alphaDe(tipo, op), op, e, consultas, &esperado,
                                            (guardar && !consultaGuardada) ? &consulta0 : nullptr);
                m.repeticion = rep;
                todoOk = escribirFilaArbol(fArboles, m) && todoOk;
                escribirFilasBusquedas(fBusq, m);
                registrar(describir(m));
                if (guardar && !consultaGuardada) {
                    guardarPares(op.resultados + "/consulta0_N" + std::to_string(e) + ".csv", consulta0);
                    consultaGuardada = true;
                }
            }
        }
    }
    std::fclose(fArboles);
    std::fclose(fBusq);
    registrar(todoOk ? "Experimento principal terminado: todas las búsquedas verificadas"
                     : "Experimento principal terminado CON ERRORES en búsquedas");
    return todoOk ? 0 : 2;
}

/**
 * @brief Experimento bonus: con N = 2^nAlpha construye el árbol Bε elegido para
 *        cada valor de α y hace las búsquedas (20 por defecto).
 *
 * Con --repeticiones R se recorre R veces la lista completa de α, para que el ruido
 * de la máquina se reparta entre los distintos valores.
 *
 * @param op opciones (entrada).
 * @return 0 si todas las búsquedas coincidieron con la fuerza bruta, 2 si no.
 */
int comandoAlpha(const Opciones& op) {
    if (op.arbolAlpha != "BE4" && op.arbolAlpha != "BE126") throw std::invalid_argument("--arbol debe ser BE4 o BE126");
    const int busquedas = op.busquedas > 0 ? op.busquedas : 20;
    crearCarpeta(op.resultados);
    crearCarpeta(op.arboles);
    const std::vector<Consulta> consultas = generarConsultas(busquedas, op.semilla + 1);
    const std::string base = op.resultados + "/alpha_" + op.arbolAlpha;
    guardarConsultas(base + "_consultas.csv", consultas);
    registrar("alpha: calculando resultados esperados por fuerza bruta");
    const std::vector<Resumen> esperado = fuerzaBruta(op.datos, 1ULL << op.nAlpha, consultas);
    std::FILE* fArboles = abrirCsv(base + ".csv", kEncabezadoArbol);
    std::FILE* fBusq = abrirCsv(base + "_busquedas.csv", kEncabezadoBusqueda);
    bool todoOk = true;
    for (int rep = 0; rep < op.repeticiones; ++rep) {
        for (size_t j = 0; j < op.alphas.size(); ++j) {  // el orden de los α también rota
            const double a = op.alphas[(j + static_cast<size_t>(rep)) % op.alphas.size()];
            registrar("alpha rep " + std::to_string(rep) + ": construyendo " + op.arbolAlpha + " con alpha=" +
                      std::to_string(a));
            MedicionArbol m = medirTipo(op.arbolAlpha, a, op, op.nAlpha, consultas, &esperado, nullptr);
            m.repeticion = rep;
            todoOk = escribirFilaArbol(fArboles, m) && todoOk;
            escribirFilasBusquedas(fBusq, m);
            registrar(describir(m));
        }
    }
    std::fclose(fArboles);
    std::fclose(fBusq);
    elegirAlpha(op.resultados, op.arbolAlpha);
    return todoOk ? 0 : 2;
}

/**
 * @brief Estadísticas de datos.bin para el informe: cantidad de pares, rango de llaves y
 *        de temperaturas, y llaves repetidas. Escribe resultados/datos.csv.
 * @param op opciones (datos, resultados) (entrada).
 * @return 0 al terminar.
 */
int comandoDatos(const Opciones& op) {
    crearCarpeta(op.resultados);
    const uint64_t n = paresEnArchivo(op.datos);
    LectorDatos lector(op.datos, n);
    std::vector<Par> bloque;
    std::vector<int32_t> llaves;
    llaves.reserve(static_cast<size_t>(n));
    int32_t lMin = INT32_MAX, lMax = INT32_MIN;
    float vMin = 1e30f, vMax = -1e30f;
    uint64_t noNumericos = 0;
    while (const size_t k = lector.siguiente(bloque, kParesPorBloque)) {
        for (size_t i = 0; i < k; ++i) {
            const Par& p = bloque[i];
            llaves.push_back(p.llave);
            lMin = std::min(lMin, p.llave);
            lMax = std::max(lMax, p.llave);
            if (std::isnan(p.valor)) {
                ++noNumericos;
            } else {
                vMin = std::min(vMin, p.valor);
                vMax = std::max(vMax, p.valor);
            }
        }
    }
    std::sort(llaves.begin(), llaves.end());
    uint64_t repetidas = 0;  // pares cuya llave ya apareció antes
    for (size_t i = 1; i < llaves.size(); ++i) repetidas += llaves[i] == llaves[i - 1] ? 1 : 0;
    std::FILE* f = abrirCsv(op.resultados + "/datos.csv",
                            "pares,llave_min,llave_max,llaves_repetidas,temp_min,temp_max,temp_no_numericas\n");
    std::fprintf(f, "%llu,%d,%d,%llu,%.4f,%.4f,%llu\n", static_cast<unsigned long long>(n), lMin, lMax,
                 static_cast<unsigned long long>(repetidas), static_cast<double>(vMin), static_cast<double>(vMax),
                 static_cast<unsigned long long>(noNumericos));
    std::fclose(f);
    registrar("datos: " + std::to_string(n) + " pares, " + std::to_string(repetidas) + " llaves repetidas");
    return 0;
}

/// @brief Bloque de 4096 bytes sin estructura (archivo de prueba de latencia).
struct Bloque4K {
    char bytes[4096];  ///< Contenido del bloque.
};

/**
 * @brief Mide la latencia de leer bloques de 4 KiB del disco donde se guardan los árboles,
 *        de la misma forma que las búsquedas (pread a un buffer alineado, O_DIRECT).
 *
 * Crea en la carpeta de árboles un archivo de prueba de --mib MiB con datos aleatorios, lo
 * lee en orden aleatorio y en orden secuencial (--lecturas veces cada uno) y escribe
 * resultados/latencia.csv. Después borra el archivo.
 *
 * @param op opciones (arboles, resultados, mib, lecturasLatencia, directo) (entrada).
 * @return 0 si las lecturas fueron directas al disco, 3 si no (ver la columna motivo).
 */
int comandoLatencia(const Opciones& op) {
    crearCarpeta(op.arboles);
    crearCarpeta(op.resultados);
    const std::string ruta = op.arboles + "/latencia.bin";
    const int64_t bloques = static_cast<int64_t>(op.mib) * 256;
    if (bloques < 2 * op.lecturasLatencia) throw std::invalid_argument("--mib demasiado chico para --lecturas");
    {
        std::mt19937_64 rng(op.semilla);
        std::vector<uint64_t> mega((1 << 20) / sizeof(uint64_t));
        const int fd = crearArchivoBloques(ruta);
        for (int i = 0; i < op.mib; ++i) {
            for (uint64_t& x : mega) x = rng();  // datos aleatorios: no se pueden comprimir
            escribirTodo(fd, mega.data(), mega.size() * sizeof(uint64_t), ruta);
        }
        terminarArchivo(fd, ruta);
    }
    ArchivoArbol<Bloque4K> archivo(ruta, op.directo);
    alignas(4096) static Bloque4K destino;
    std::FILE* f = abrirCsv(op.resultados + "/latencia.csv",
                            "modo,lecturas,prom_ms,mediana_ms,p90_ms,p99_ms,mib_archivo,odirect,motivo_sin_odirect\n");
    std::mt19937_64 rng(op.semilla + 7);
    for (const char* modo : {"aleatoria", "secuencial"}) {
        std::vector<double> t;
        const int64_t base = static_cast<int64_t>(rng() % static_cast<uint64_t>(bloques - op.lecturasLatencia));
        for (int i = 0; i < op.lecturasLatencia; ++i) {
            const int64_t b = std::strcmp(modo, "aleatoria") == 0 ? static_cast<int64_t>(rng() % static_cast<uint64_t>(bloques))
                                                                   : base + i;
            const auto t0 = Reloj::now();
            archivo.leer(static_cast<int32_t>(b), destino);
            t.push_back(std::chrono::duration<double, std::milli>(Reloj::now() - t0).count());
        }
        std::sort(t.begin(), t.end());
        double suma = 0;
        for (double x : t) suma += x;
        std::fprintf(f, "%s,%d,%.6f,%.6f,%.6f,%.6f,%d,%d,%s\n", modo, op.lecturasLatencia, suma / static_cast<double>(t.size()),
                     t[t.size() / 2], t[t.size() * 9 / 10], t[t.size() * 99 / 100], op.mib, archivo.directo() ? 1 : 0,
                     archivo.motivo().c_str());
        registrar(std::string("latencia ") + modo + ": mediana " + std::to_string(t[t.size() / 2]) + " ms" +
                  (archivo.directo() ? "" : " (ATENCIÓN: no se leyó directo del disco: " + archivo.motivo() + ")"));
    }
    std::fclose(f);
    std::remove(ruta.c_str());
    return archivo.directo() ? 0 : 3;
}

/**
 * @brief Valida y devuelve el rango [l, u] pedido por línea de comandos.
 * @param op opciones con l y u (entrada).
 * @return consulta con id 0.
 */
Consulta rangoPedido(const Opciones& op) {
    if (op.l < INT32_MIN || op.u > INT32_MAX || op.l > op.u || op.l == -1 || op.u == -1) {
        throw std::invalid_argument("indique un rango válido con --l y --u (l <= u, enteros de 32 bits)");
    }
    return {0, static_cast<int32_t>(op.l), static_cast<int32_t>(op.u)};
}

/**
 * @brief Construye un árbol con 2^n pares, lo guarda y responde una búsqueda [l, u].
 *        Escribe los pares encontrados como CSV y un resumen en stderr.
 * @param op opciones (tipo, n, alpha, l, u, salida) (entrada).
 * @return 0 si la búsqueda coincide con la fuerza bruta, 2 si no.
 */
int comandoConsulta(const Opciones& op) {
    const Consulta c = rangoPedido(op);
    crearCarpeta(op.arboles);
    const std::vector<Consulta> consultas = {c};
    const std::vector<Resumen> esperado = fuerzaBruta(op.datos, 1ULL << op.nAlpha, consultas);
    const double alpha = op.alpha > 0 ? op.alpha : alphaDe(op.tipo, op);
    std::vector<Par> pares;
    const MedicionArbol m = medirTipo(op.tipo, alpha, op, op.nAlpha, consultas, &esperado, &pares);
    guardarPares(op.salida, pares);
    registrar(describir(m));
    return m.busquedas[0].correcta ? 0 : 2;
}

/**
 * @brief Responde una búsqueda [l, u] sobre un archivo de árbol ya guardado
 *        (por ejemplo, con --conservar-arboles).
 * @param op opciones (tipo, archivo, l, u, salida) (entrada).
 * @return 0 al terminar.
 */
int comandoBuscar(const Opciones& op) {
    const Consulta c = rangoPedido(op);
    if (op.archivo.empty()) throw std::invalid_argument("indique el archivo del árbol con --archivo");
    std::vector<Par> pares;
    uint64_t ios = 0;
    double ms = 0;
    // Busca [l, u] en el archivo con el tipo de árbol indicado por el puntero (solo se usa
    // su tipo, vale nullptr). Deja los pares en `pares`, el tiempo en `ms` y las I/Os en `ios`.
    auto buscar = [&](auto* tipoArbol) {
        using Arbol = std::remove_pointer_t<decltype(tipoArbol)>;
        ArchivoArbol<typename Arbol::Nodo> archivo(op.archivo, op.directo);
        const auto t0 = Reloj::now();
        Arbol::buscarRango(archivo, c.l, c.u, pares);
        ms = std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
        ios = archivo.lecturas();
    };
    if (op.tipo == "B") {
        buscar(static_cast<ArbolB340*>(nullptr));
    } else if (op.tipo == "BE4") {
        buscar(static_cast<ArbolBe4*>(nullptr));
    } else if (op.tipo == "BE126") {
        buscar(static_cast<ArbolBe126*>(nullptr));
    } else {
        throw std::invalid_argument("tipo de árbol desconocido: " + op.tipo);
    }
    std::sort(pares.begin(), pares.end(), CompararLlave{});
    guardarPares(op.salida, pares);
    registrar(std::to_string(pares.size()) + " pares, " + std::to_string(ios) + " I/Os, " + std::to_string(ms) +
              " ms");
    return 0;
}

/// @brief Muestra el uso del programa en stderr.
void ayuda() {
    std::fputs(
        "Uso: tarea2 <comando> [opciones]\n"
        "\n"
        "Comandos:\n"
        "  principal   Experimento de la sección 4: N = 2^nmin..2^nmax, árboles B, BE4 y BE126.\n"
        "  alpha       Bonus: N = 2^24, un árbol Bε con varios α, 20 búsquedas por α.\n"
        "  consulta    Construye un árbol con 2^n pares, lo guarda y busca [l, u].\n"
        "  buscar      Busca [l, u] en un archivo de árbol ya guardado.\n"
        "  elegir-alpha  Vuelve a elegir α desde resultados/alpha_<tipo>.csv (--arbol).\n"
        "  datos       Estadísticas de datos.bin para el informe (resultados/datos.csv).\n"
        "  latencia    Latencia de lecturas de 4 KiB en la carpeta de árboles (resultados/latencia.csv).\n"
        "\n"
        "Opciones generales:\n"
        "  --datos RUTA          datos.bin (por defecto data/datos.bin)\n"
        "  --resultados DIR      carpeta de los CSV (por defecto resultados)\n"
        "  --arboles DIR         carpeta de los archivos de árboles (por defecto arboles)\n"
        "  --busquedas K         búsquedas por árbol (50 en principal, 20 en alpha)\n"
        "  --repeticiones R      repeticiones de cada construcción con sus búsquedas (1)\n"
        "  --semilla S           semilla de los rangos aleatorios\n"
        "  --sin-odirect         buscar usando la caché del sistema operativo\n"
        "  --conservar-arboles   no borrar los archivos de árboles al terminar\n"
        "principal:\n"
        "  --nmin E --nmax E     exponentes de N (15 y 26)\n"
        "  --tipos B,BE4,BE126   árboles a medir\n"
        "  --alpha-be4 A         α del árbol Bε c=4 (por defecto: el elegido por `alpha`, o 0.9)\n"
        "  --alpha-be126 A       α del árbol Bε c=126 (ídem)\n"
        "  --guardar-consulta E1,E2   exponentes para los que se guarda la búsqueda 0 (16,20,26)\n"
        "alpha:\n"
        "  --arbol BE4|BE126     árbol a estudiar (BE126)\n"
        "  --n E                 N = 2^E (24)\n"
        "  --alphas A1,A2,...    valores de α (0.1,0.25,0.4,0.5,0.6,0.75,0.9,0.99)\n"
        "consulta / buscar:\n"
        "  --tipo B|BE4|BE126    tipo de árbol\n"
        "  --n E                 (consulta) N = 2^E\n"
        "  --alpha A             (consulta) α para Bε\n"
        "  --archivo RUTA        (buscar) archivo del árbol\n"
        "  --l L --u U           rango de llaves (tiempo Unix)\n"
        "  --salida RUTA         CSV de salida (por defecto la salida estándar)\n"
        "latencia:\n"
        "  --mib M               tamaño del archivo de prueba en MiB (512)\n"
        "  --lecturas K          lecturas por modo (3000)\n",
        stderr);
}

/**
 * @brief Separa una lista separada por comas.
 * @param s texto como "a,b,c" (entrada).
 * @return elementos no vacíos.
 */
std::vector<std::string> separar(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, ',')) {
        if (!x.empty()) v.push_back(x);
    }
    return v;
}

/**
 * @brief Interpreta los argumentos de la línea de comandos.
 * @param argc cantidad de argumentos (entrada).
 * @param argv argumentos (entrada).
 * @return opciones resultantes.
 */
Opciones parsear(int argc, char** argv) {
    Opciones op;
    if (argc < 2) throw std::invalid_argument("falta el comando");
    op.comando = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        // Devuelve el argumento que sigue a la opción actual y avanza i; falla si no hay.
        auto valor = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("falta el valor de " + a);
            return argv[++i];
        };
        if (a == "--datos") op.datos = valor();
        else if (a == "--resultados") op.resultados = valor();
        else if (a == "--arboles") op.arboles = valor();
        else if (a == "--nmin") op.nMin = std::stoi(valor());
        else if (a == "--nmax") op.nMax = std::stoi(valor());
        else if (a == "--n") op.nAlpha = std::stoi(valor());
        else if (a == "--busquedas") op.busquedas = std::stoi(valor());
        else if (a == "--repeticiones") op.repeticiones = std::stoi(valor());
        else if (a == "--mib") op.mib = std::stoi(valor());
        else if (a == "--lecturas") op.lecturasLatencia = std::stoi(valor());
        else if (a == "--semilla") op.semilla = std::stoull(valor());
        else if (a == "--alpha-be4") op.alphaBe4 = std::stod(valor());
        else if (a == "--alpha-be126") op.alphaBe126 = std::stod(valor());
        else if (a == "--tipos") op.tipos = separar(valor());
        else if (a == "--guardar-consulta") {
            op.guardarConsulta.clear();
            for (const std::string& x : separar(valor())) op.guardarConsulta.push_back(std::stoi(x));
        } else if (a == "--arbol") op.arbolAlpha = valor();
        else if (a == "--alphas") {
            op.alphas.clear();
            for (const std::string& x : separar(valor())) op.alphas.push_back(std::stod(x));
        } else if (a == "--sin-odirect") op.directo = false;
        else if (a == "--conservar-arboles") op.conservar = true;
        else if (a == "--tipo") op.tipo = valor();
        else if (a == "--archivo") op.archivo = valor();
        else if (a == "--alpha") op.alpha = std::stod(valor());
        else if (a == "--l") op.l = std::stoll(valor());
        else if (a == "--u") op.u = std::stoll(valor());
        else if (a == "--salida") op.salida = valor();
        else throw std::invalid_argument("opción desconocida: " + a);
    }
    if (op.nMin < 1 || op.nMax > 26 || op.nMin > op.nMax) throw std::invalid_argument("rango de exponentes inválido");
    if (op.nAlpha < 0 || op.nAlpha > 26) throw std::invalid_argument("--n debe estar entre 0 y 26");
    if (op.repeticiones < 1) throw std::invalid_argument("--repeticiones debe ser al menos 1");
    if (op.busquedas == 0 || op.busquedas < -1) throw std::invalid_argument("--busquedas debe ser al menos 1");
    return op;
}

}  // namespace

/**
 * @brief Punto de entrada.
 * @param argc cantidad de argumentos (entrada).
 * @param argv argumentos: comando y opciones (entrada).
 * @return 0 si todo salió bien, 1 si hubo un error de uso o de ejecución, 2 si una búsqueda no coincidió con la fuerza bruta.
 */
int main(int argc, char** argv) {
    try {
        Opciones op = parsear(argc, argv);
        if (op.comando == "principal" || op.comando == "consulta") resolverAlphas(op);
        if (op.comando == "principal") return comandoPrincipal(op);
        if (op.comando == "alpha") return comandoAlpha(op);
        if (op.comando == "consulta") return comandoConsulta(op);
        if (op.comando == "buscar") return comandoBuscar(op);
        if (op.comando == "elegir-alpha") {
            elegirAlpha(op.resultados, op.arbolAlpha);
            return 0;
        }
        if (op.comando == "datos") return comandoDatos(op);
        if (op.comando == "latencia") return comandoLatencia(op);
        if (op.comando == "ayuda" || op.comando == "--help" || op.comando == "-h") {
            ayuda();
            return 0;
        }
        throw std::invalid_argument("comando desconocido: " + op.comando);
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "Error: %s\n\n", e.what());
        ayuda();
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
