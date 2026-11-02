/**
 * @file main.cpp
 * @brief Programa principal de la Tarea 2 (CC4102): construye árboles B y Bε con
 *        los datos de temperatura, los guarda en disco, ejecuta búsquedas por rango y
 *        registra los resultados en archivos CSV.
 *
 * Comandos:
 *  - principal: experimento de la sección 4 (N = 2^15..2^26, 50 búsquedas).
 *  - alpha:     experimento bonus (N = 2^24, 8 valores de α, 20 búsquedas).
 */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "arbol_b.hpp"
#include "arbol_be.hpp"
#include "datos.hpp"

namespace {

using Reloj = std::chrono::steady_clock;
using ArbolB340 = ArbolB<340>;         ///< Árbol B de la tarea (b = 340).
using ArbolBe4 = ArbolBe<4, 506>;      ///< Árbol Bε con ε ≈ 0.25 (c = 4, l_max = 506).
using ArbolBe126 = ArbolBe<126, 384>;  ///< Árbol Bε con ε ≈ 0.75 (c = 126, l_max = 384).

constexpr int64_t kLlaveMin = 1546300800;   ///< Valor mínimo de l (enunciado).
constexpr int64_t kLlaveMax = 1754006400;   ///< Valor máximo de l (enunciado).
constexpr int64_t kAnchoRango = 604800;     ///< Tamaño del rango: una semana en segundos.
constexpr size_t kParesPorBloque = 1 << 16; ///< Pares leídos de datos.bin por cada fread.
constexpr double kAlphaPorDefecto = 0.9;    ///< Valor de α usado por defecto en los árboles Bε.

/// @brief Opciones de ejecución de los experimentos.
struct Opciones {
    std::string comando;                                  ///< principal | alpha.
    std::string datos = "data/datos.bin";                 ///< Ruta del archivo binario de entrada.
    std::string resultados = "resultados";                ///< Carpeta para los CSV de salida.
    std::string arboles = "arboles";                      ///< Carpeta temporal para los árboles en disco.
    int nMin = 15;                                        ///< Exponente mínimo de N (principal).
    int nMax = 26;                                        ///< Exponente máximo de N (principal).
    int nAlpha = 24;                                      ///< Exponente de N en el bonus (alpha).
    int busquedas = -1;                                   ///< Cantidad de búsquedas (-1: 50 en principal, 20 en alpha).
    int repeticiones = 1;                                 ///< Repeticiones de cada configuración.
    uint64_t semilla = 20261015;                          ///< Semilla para generar rangos aleatorios.
    double alpha = kAlphaPorDefecto;                      ///< Parámetro α para los árboles Bε en principal.
    std::vector<std::string> tipos = {"B", "BE4", "BE126"};  ///< Árboles del experimento principal.
    std::vector<int> guardarConsulta = {16, 26};          ///< Dos exponentes para guardar los pares de la búsqueda 0.
    std::string arbolAlpha = "BE126";                     ///< Árbol Bε para el experimento bonus.
    std::vector<double> alphas = {0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 0.99};  ///< 8 valores de α (bonus).
};

/// @brief Rango de búsqueda [l, u].
struct Consulta {
    int id;     ///< Identificador de la consulta (0..K-1).
    int32_t l;  ///< Límite inferior inclusive.
    int32_t u;  ///< Límite superior inclusive.
};

/// @brief Medición individual de una búsqueda por rango.
struct MedicionBusqueda {
    int id;               ///< Identificador de la consulta.
    int32_t l;            ///< Límite inferior.
    int32_t u;            ///< Límite superior.
    uint64_t resultados;  ///< Cantidad de pares encontrados en [l, u].
    uint64_t ios;         ///< Cantidad de bloques leídos desde el archivo binario.
    double ms;            ///< Tiempo de ejecución de la búsqueda en milisegundos.
};

/// @brief Mediciones de creación, tamaño y búsquedas de un árbol construido con N pares.
struct MedicionArbol {
    std::string tipo;          ///< B, BE4 o BE126.
    double alpha = 0;          ///< Parámetro α usado (0 si es árbol B).
    int exponente = 0;         ///< Exponente de N (N = 2^exponente).
    uint64_t n = 0;            ///< Cantidad de pares insertados.
    int repeticion = 0;        ///< Índice de repetición (0, 1, ...).
    double tInsercion = 0;     ///< Tiempo de inserción de los N pares en segundos.
    double tLecturaDatos = 0;  ///< Tiempo de lectura de datos.bin en segundos.
    double tEscritura = 0;     ///< Tiempo de escritura del árbol a disco en segundos.
    uint64_t lecturas = 0;     ///< Lecturas simuladas durante la inserción.
    uint64_t escrituras = 0;   ///< Escrituras simuladas durante la inserción.
    EstadisticasArbol est;     ///< Estadísticas de nodos y ocupación del árbol.
    ContadoresArbol cont;      ///< Contadores de vaciados, lotes y divisiones.
    std::vector<MedicionBusqueda> busquedas;  ///< Resultados de cada búsqueda ejecutada.
};

/**
 * @brief Imprime un mensaje de avance en stderr con marca de tiempo.
 * @param msg mensaje a mostrar (entrada).
 */
void registrar(const std::string& msg) {
    const std::time_t t = std::time(nullptr);
    char hora[16];
    std::strftime(hora, sizeof hora, "%H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "[%s] %s\n", hora, msg.c_str());
    std::fflush(stderr);
}

/**
 * @brief Crea un directorio y sus directorios padres si no existen.
 * @param ruta ruta del directorio a crear (entrada).
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
 * @brief Genera rangos aleatorios [l, l + 604800] con l uniforme en [1546300800, 1754006400].
 * @param cantidad número de consultas a generar (entrada).
 * @param semilla  semilla del generador pseudoaleatorio (entrada).
 * @return vector con las consultas generadas.
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
 * @brief Lee los primeros n pares de datos.bin e inserta cada uno en el árbol.
 * @tparam Arbol tipo de árbol (ArbolB o ArbolBe).
 * @param arbol    instancia del árbol donde se insertan los pares (entrada/salida).
 * @param ruta     ruta de datos.bin (entrada).
 * @param n        cantidad de pares a insertar (entrada).
 * @param tLectura segundos empleados leyendo datos.bin (salida).
 * @return segundos empleados exclusivamente en las inserciones.
 */
template <class Arbol>
double construir(Arbol& arbol, const std::string& ruta, uint64_t n, double& tLectura) {
    LectorDatos lector(ruta, n);
    std::vector<Par> bloque;
    double segundos = 0;
    tLectura = 0;
    while (true) {
        const auto t0 = Reloj::now();
        const size_t k = lector.siguiente(bloque, kParesPorBloque);
        const auto t1 = Reloj::now();
        tLectura += std::chrono::duration<double>(t1 - t0).count();
        if (k == 0) break;
        for (size_t i = 0; i < k; ++i) arbol.insertar(bloque[i]);
        segundos += std::chrono::duration<double>(Reloj::now() - t1).count();
    }
    return segundos;
}

/**
 * @brief Construye un árbol en RAM, lo serializa a disco, libera la RAM y ejecuta las búsquedas desde disco.
 * @tparam Arbol tipo de árbol.
 * @param arbol     árbol vacío recién creado; se destruye antes de las búsquedas (entrada).
 * @param tipo      identificador del árbol ("B", "BE4" o "BE126") (entrada).
 * @param alpha     parámetro α usado (0 para árbol B) (entrada).
 * @param op        opciones del experimento (entrada).
 * @param exponente exponente tal que N = 2^exponente (entrada).
 * @param consultas rangos a buscar desde el archivo binario (entrada).
 * @param consulta0 si no es nullptr, recibe los pares ordenados de la consulta 0 (salida).
 * @return estructura con todas las métricas de creación, tamaño y búsqueda.
 */
template <class Arbol>
MedicionArbol medirArbol(std::unique_ptr<Arbol> arbol, const std::string& tipo, double alpha, const Opciones& op,
                         int exponente, const std::vector<Consulta>& consultas, std::vector<Par>* consulta0) {
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
    m.tInsercion = construir(*arbol, op.datos, m.n, m.tLecturaDatos);
    m.lecturas = arbol->disco().lecturas();
    m.escrituras = arbol->disco().escrituras();
    m.est = arbol->estadisticas();
    m.cont = arbol->contadores();
    const auto inicioEscritura = Reloj::now();
    arbol->disco().guardar(ruta);
    m.tEscritura = std::chrono::duration<double>(Reloj::now() - inicioEscritura).count();
    arbol.reset();  // Libera el disco simulado: las búsquedas solo usan el archivo.
    ArchivoArbol<typename Arbol::Nodo> archivo(ruta);
    std::vector<Par> resultado;
    for (const Consulta& c : consultas) {
        resultado.clear();
        archivo.reiniciarLecturas();
        const auto t0 = Reloj::now();
        Arbol::buscarRango(archivo, c.l, c.u, resultado);
        const double ms = std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
        m.busquedas.push_back({c.id, c.l, c.u, resultado.size(), archivo.lecturas(), ms});
        if (c.id == 0 && consulta0 != nullptr) {
            *consulta0 = resultado;
            std::sort(consulta0->begin(), consulta0->end(), CompararLlave{});
        }
    }
    std::remove(ruta.c_str());
    return m;
}

/**
 * @brief Despacha la construcción y medición según el nombre del tipo de árbol.
 * @param tipo      "B", "BE4" o "BE126" (entrada).
 * @param alpha     parámetro α para los árboles Bε (entrada).
 * @param op        opciones del experimento (entrada).
 * @param exponente exponente de N (entrada).
 * @param consultas lista de consultas por rango (entrada).
 * @param consulta0 puntero opcional donde guardar el resultado de la consulta 0 (salida).
 * @return mediciones del árbol.
 */
MedicionArbol medirTipo(const std::string& tipo, double alpha, const Opciones& op, int exponente,
                        const std::vector<Consulta>& consultas, std::vector<Par>* consulta0) {
    if (tipo == "B") {
        return medirArbol(std::make_unique<ArbolB340>(), tipo, 0.0, op, exponente, consultas, consulta0);
    }
    if (tipo == "BE4") {
        return medirArbol(std::make_unique<ArbolBe4>(alpha), tipo, alpha, op, exponente, consultas, consulta0);
    }
    if (tipo == "BE126") {
        return medirArbol(std::make_unique<ArbolBe126>(alpha), tipo, alpha, op, exponente, consultas, consulta0);
    }
    throw std::invalid_argument("tipo de árbol desconocido: " + tipo + " (use B, BE4 o BE126)");
}

/**
 * @brief Calcula el promedio y la desviación estándar muestral de un vector de números.
 * @param v    valores de entrada (entrada).
 * @param prom promedio calculado (salida).
 * @param desv desviación estándar muestral, 0 si hay menos de 2 elementos (salida).
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

/// Encabezado del CSV resumen por árbol construido.
const char* kEncabezadoArbol =
    "tipo,alpha,exponente,N,repeticion,t_insercion_s,t_lectura_datos_s,lecturas_insercion,escrituras_insercion,"
    "ios_insercion,nodos,bytes,altura,nodos_internos,hojas,elementos_en_internos,elementos_en_hojas,"
    "vaciados,lotes,elementos_empujados,divisiones_hoja,divisiones_internas,t_escritura_s,busquedas,"
    "t_busqueda_prom_ms,t_busqueda_desv_ms,ios_busqueda_prom,ios_busqueda_desv,resultados_prom\n";

/// Encabezado del CSV detallado por búsqueda.
const char* kEncabezadoBusqueda = "tipo,alpha,exponente,N,repeticion,id,l,u,resultados,ios,tiempo_ms\n";

/**
 * @brief Escribe una fila en el CSV de árboles con las métricas de una construcción.
 * @param f archivo CSV abierto para escritura (entrada/salida).
 * @param m mediciones del árbol (entrada).
 */
void escribirFilaArbol(std::FILE* f, const MedicionArbol& m) {
    std::vector<double> tiempos, ios, res;
    for (const MedicionBusqueda& b : m.busquedas) {
        tiempos.push_back(b.ms);
        ios.push_back(static_cast<double>(b.ios));
        res.push_back(static_cast<double>(b.resultados));
    }
    double tProm, tDesv, ioProm, ioDesv, resProm, resDesv;
    promedioDesv(tiempos, tProm, tDesv);
    promedioDesv(ios, ioProm, ioDesv);
    promedioDesv(res, resProm, resDesv);
    std::fprintf(f,
                 "%s,%g,%d,%llu,%d,%.6f,%.6f,%llu,%llu,%llu,%lld,%lld,%d,%lld,%lld,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
                 "%.6f,%zu,%.6f,%.6f,%.3f,%.3f,%.3f\n",
                 m.tipo.c_str(), m.alpha, m.exponente, static_cast<unsigned long long>(m.n), m.repeticion,
                 m.tInsercion, m.tLecturaDatos, static_cast<unsigned long long>(m.lecturas),
                 static_cast<unsigned long long>(m.escrituras),
                 static_cast<unsigned long long>(m.lecturas + m.escrituras), static_cast<long long>(m.est.nodos),
                 static_cast<long long>(m.est.nodos) * 4096LL, m.est.altura, static_cast<long long>(m.est.internos),
                 static_cast<long long>(m.est.hojas), static_cast<unsigned long long>(m.est.elementosEnInternos),
                 static_cast<unsigned long long>(m.est.elementosEnHojas),
                 static_cast<unsigned long long>(m.cont.vaciados), static_cast<unsigned long long>(m.cont.lotes),
                 static_cast<unsigned long long>(m.cont.elementosEmpujados),
                 static_cast<unsigned long long>(m.cont.divisionesHoja),
                 static_cast<unsigned long long>(m.cont.divisionesInternas), m.tEscritura, m.busquedas.size(), tProm,
                 tDesv, ioProm, ioDesv, resProm);
    std::fflush(f);
}

/**
 * @brief Escribe todas las búsquedas de un árbol en el CSV de búsquedas.
 * @param f archivo CSV abierto para escritura (entrada/salida).
 * @param m mediciones del árbol (entrada).
 */
void escribirFilasBusquedas(std::FILE* f, const MedicionArbol& m) {
    for (const MedicionBusqueda& b : m.busquedas) {
        std::fprintf(f, "%s,%g,%d,%llu,%d,%d,%d,%d,%llu,%llu,%.6f\n", m.tipo.c_str(), m.alpha, m.exponente,
                     static_cast<unsigned long long>(m.n), m.repeticion, b.id, b.l, b.u,
                     static_cast<unsigned long long>(b.resultados), static_cast<unsigned long long>(b.ios), b.ms);
    }
    std::fflush(f);
}

/**
 * @brief Crea un archivo CSV y escribe su línea de encabezado.
 * @param ruta       ruta del archivo CSV (entrada).
 * @param encabezado primera línea del archivo (entrada).
 * @return puntero FILE abierto.
 */
std::FILE* abrirCsv(const std::string& ruta, const char* encabezado) {
    std::FILE* f = std::fopen(ruta.c_str(), "w");
    if (f == nullptr) throw std::runtime_error("no se pudo crear " + ruta);
    std::fputs(encabezado, f);
    std::fflush(f);
    return f;
}

/**
 * @brief Guarda una lista de pares llave-valor en un archivo CSV.
 * @param ruta  ruta del archivo de salida (entrada).
 * @param pares vector de pares a guardar (entrada).
 */
void guardarPares(const std::string& ruta, const std::vector<Par>& pares) {
    std::FILE* f = abrirCsv(ruta, "llave,valor\n");
    for (const Par& p : pares) std::fprintf(f, "%d,%.4f\n", p.llave, static_cast<double>(p.valor));
    std::fclose(f);
}

/**
 * @brief Guarda los rangos de consulta generados en un archivo CSV.
 * @param ruta      ruta del archivo CSV (entrada).
 * @param consultas vector de consultas (entrada).
 */
void guardarConsultas(const std::string& ruta, const std::vector<Consulta>& consultas) {
    std::FILE* f = abrirCsv(ruta, "id,l,u\n");
    for (const Consulta& c : consultas) std::fprintf(f, "%d,%d,%d\n", c.id, c.l, c.u);
    std::fclose(f);
}

/**
 * @brief Formatea un resumen de una línea con los resultados de un árbol.
 * @param m mediciones del árbol (entrada).
 * @return cadena descriptiva.
 */
std::string describir(const MedicionArbol& m) {
    double ms = 0, ios = 0, res = 0;
    for (const MedicionBusqueda& b : m.busquedas) {
        ms += b.ms;
        ios += static_cast<double>(b.ios);
        res += static_cast<double>(b.resultados);
    }
    const double k = m.busquedas.empty() ? 1.0 : static_cast<double>(m.busquedas.size());
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "N=2^%d rep %d %-5s a=%-5g insercion %.2fs, I/Os %llu (%.3f por par), %lld nodos (%.1f MiB), "
                  "altura %d, busqueda %.3f ms, %.1f I/Os, %.1f resultados",
                  m.exponente, m.repeticion, m.tipo.c_str(), m.alpha, m.tInsercion,
                  static_cast<unsigned long long>(m.lecturas + m.escrituras),
                  static_cast<double>(m.lecturas + m.escrituras) / static_cast<double>(m.n),
                  static_cast<long long>(m.est.nodos), static_cast<double>(m.est.nodos) * 4096.0 / (1 << 20),
                  m.est.altura, ms / k, ios / k, res / k);
    return buf;
}

/**
 * @brief Ejecuta el experimento principal (Sección 4): N = 2^15..2^26 con los 3 árboles y 50 búsquedas.
 * @param op opciones configuradas (entrada).
 * @return 0 al finalizar correctamente.
 */
int comandoPrincipal(const Opciones& op) {
    const int busquedas = op.busquedas > 0 ? op.busquedas : 50;
    crearCarpeta(op.resultados);
    crearCarpeta(op.arboles);
    const std::vector<Consulta> consultas = generarConsultas(busquedas, op.semilla);
    guardarConsultas(op.resultados + "/consultas_principal.csv", consultas);
    std::FILE* fArboles = abrirCsv(op.resultados + "/principal.csv", kEncabezadoArbol);
    std::FILE* fBusq = abrirCsv(op.resultados + "/principal_busquedas.csv", kEncabezadoBusqueda);
    for (int e = op.nMin; e <= op.nMax; ++e) {
        const bool guardar = std::find(op.guardarConsulta.begin(), op.guardarConsulta.end(), e) != op.guardarConsulta.end();
        for (int rep = 0; rep < op.repeticiones; ++rep) {
            for (size_t j = 0; j < op.tipos.size(); ++j) {
                const std::string& tipo = op.tipos[(j + static_cast<size_t>(rep)) % op.tipos.size()];
                const double a = (tipo == "B") ? 0.0 : op.alpha;
                // Los pares de la consulta 0 se guardan una vez por N: los de la primera construcción.
                const bool guardarConsulta0 = guardar && rep == 0 && j == 0;
                std::vector<Par> consulta0;
                registrar("N=2^" + std::to_string(e) + " rep " + std::to_string(rep) + ": construyendo " + tipo);
                MedicionArbol m = medirTipo(tipo, a, op, e, consultas, guardarConsulta0 ? &consulta0 : nullptr);
                m.repeticion = rep;
                escribirFilaArbol(fArboles, m);
                escribirFilasBusquedas(fBusq, m);
                registrar(describir(m));
                if (guardarConsulta0) {
                    guardarPares(op.resultados + "/consulta0_N" + std::to_string(e) + ".csv", consulta0);
                }
            }
        }
    }
    std::fclose(fArboles);
    std::fclose(fBusq);
    registrar("Experimento principal terminado");
    return 0;
}

/**
 * @brief Ejecuta el experimento bonus (Sección 5.1): N = 2^24, 8 valores de α y 20 búsquedas.
 * @param op opciones configuradas (entrada).
 * @return 0 al finalizar correctamente.
 */
int comandoAlpha(const Opciones& op) {
    if (op.arbolAlpha != "BE4" && op.arbolAlpha != "BE126") {
        throw std::invalid_argument("--arbol debe ser BE4 o BE126");
    }
    const int busquedas = op.busquedas > 0 ? op.busquedas : 20;
    crearCarpeta(op.resultados);
    crearCarpeta(op.arboles);
    const std::vector<Consulta> consultas = generarConsultas(busquedas, op.semilla + 1);
    const std::string base = op.resultados + "/alpha_" + op.arbolAlpha;
    guardarConsultas(base + "_consultas.csv", consultas);
    std::FILE* fArboles = abrirCsv(base + ".csv", kEncabezadoArbol);
    std::FILE* fBusq = abrirCsv(base + "_busquedas.csv", kEncabezadoBusqueda);
    for (int rep = 0; rep < op.repeticiones; ++rep) {
        for (size_t j = 0; j < op.alphas.size(); ++j) {
            const double a = op.alphas[(j + static_cast<size_t>(rep)) % op.alphas.size()];
            registrar("alpha rep " + std::to_string(rep) + ": construyendo " + op.arbolAlpha + " con alpha=" +
                      std::to_string(a));
            MedicionArbol m = medirTipo(op.arbolAlpha, a, op, op.nAlpha, consultas, nullptr);
            m.repeticion = rep;
            escribirFilaArbol(fArboles, m);
            escribirFilasBusquedas(fBusq, m);
            registrar(describir(m));
        }
    }
    std::fclose(fArboles);
    std::fclose(fBusq);
    registrar("Experimento bonus terminado");
    return 0;
}

/// @brief Muestra el mensaje de ayuda en stderr.
void ayuda() {
    std::fputs(
        "Uso: tarea2 <comando> [opciones]\n"
        "\n"
        "Comandos:\n"
        "  principal   Experimento de la sección 4: N = 2^nmin..2^nmax, árboles B, BE4 y BE126.\n"
        "  alpha       Bonus: N = 2^24, un árbol Bε con 8 valores de α, 20 búsquedas por α.\n"
        "\n"
        "Opciones generales:\n"
        "  --datos RUTA          datos.bin (por defecto data/datos.bin)\n"
        "  --resultados DIR      carpeta de los CSV (por defecto resultados)\n"
        "  --arboles DIR         carpeta temporal para los árboles (por defecto arboles)\n"
        "  --busquedas K         búsquedas por árbol (50 en principal, 20 en alpha)\n"
        "  --repeticiones R      repeticiones de cada construcción (por defecto 1)\n"
        "  --semilla S           semilla de los rangos aleatorios\n"
        "principal:\n"
        "  --nmin E --nmax E     exponentes mínimo y máximo de N (15 y 26)\n"
        "  --alpha A             valor de α para los árboles Bε (por defecto 0.9)\n"
        "alpha:\n"
        "  --arbol BE4|BE126     árbol Bε a evaluar (por defecto BE126)\n"
        "  --n E                 exponente de N = 2^E (por defecto 24)\n"
        "  --alphas A1,A2,...    lista de valores de α separados por comas\n",
        stderr);
}

/**
 * @brief Divide una cadena por comas.
 * @param s texto de entrada separado por comas (entrada).
 * @return vector con las subcadenas no vacías.
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
 * @brief Parsea los argumentos de la línea de comandos.
 * @param argc cantidad de argumentos (entrada).
 * @param argv arreglo de argumentos (entrada).
 * @return estructura Opciones configurada.
 */
Opciones parsear(int argc, char** argv) {
    Opciones op;
    if (argc < 2) throw std::invalid_argument("falta el comando");
    op.comando = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
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
        else if (a == "--semilla") op.semilla = std::stoull(valor());
        else if (a == "--alpha") op.alpha = std::stod(valor());
        else if (a == "--arbol") op.arbolAlpha = valor();
        else if (a == "--alphas") {
            op.alphas.clear();
            for (const std::string& x : separar(valor())) op.alphas.push_back(std::stod(x));
        } else throw std::invalid_argument("opción desconocida: " + a);
    }
    if (op.nMin < 1 || op.nMax > 26 || op.nMin > op.nMax) throw std::invalid_argument("rango de exponentes inválido");
    if (op.nAlpha < 0 || op.nAlpha > 26) throw std::invalid_argument("--n debe estar entre 0 y 26");
    if (op.repeticiones < 1) throw std::invalid_argument("--repeticiones debe ser al menos 1");
    if (op.busquedas == 0 || op.busquedas < -1) throw std::invalid_argument("--busquedas debe ser al menos 1");
    return op;
}

}  // namespace

/**
 * @brief Función principal.
 * @param argc cantidad de argumentos (entrada).
 * @param argv argumentos de línea de comandos (entrada).
 * @return 0 si la ejecución fue exitosa, 1 en caso de error.
 */
int main(int argc, char** argv) {
    try {
        Opciones op = parsear(argc, argv);
        if (op.comando == "principal") return comandoPrincipal(op);
        if (op.comando == "alpha") return comandoAlpha(op);
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
