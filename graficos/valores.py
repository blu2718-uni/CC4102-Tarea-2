"""Números y afirmaciones del informe, calculados a partir de los resultados de esta máquina.

El informe (informe/informe.typ) no tiene números de resultados escritos a mano: los lee de
valores.json, que genera este módulo a partir de los CSV de resultados/. Así el informe
siempre describe la corrida que se hizo en la máquina que ejecutó los experimentos.

valores.json tiene cuatro secciones:
    v: textos ya formateados (coma decimal, espacio duro como separador de miles)
    b: verdadero/falso para las afirmaciones que dependen de los datos
    l: listas de exponentes de N (por ejemplo, los N en que crece una altura)
    n: algunos números sin formato
También escribe tabla_entorno.csv (sección de entorno del informe).
"""

from __future__ import annotations

import csv
import datetime as dt
import json
import math
import os
import platform
import statistics
from collections import defaultdict

import matplotlib

try:
    from zoneinfo import ZoneInfo

    ZONA = ZoneInfo("America/Santiago")
except Exception:  # pragma: no cover - sin base de datos de zonas horarias
    ZONA = dt.timezone.utc

ARBOLES = ["B", "BE4", "BE126"]
CAPACIDAD_HOJA = {"B": 340, "BE4": 506, "BE126": 384}
LLAVE_MIN, LLAVE_MAX, ANCHO = 1546300800, 1754006400, 604800
UMBRAL_INTERFERENCIA = 1.10  # tiempo de reloj / tiempo de CPU sobre el que una construcción es sospechosa
T_MIN_INTERFERENCIA = 0.5  # segundos: bajo esto el cociente reloj/CPU es demasiado ruidoso


def leer_csv(ruta: str) -> list[dict]:
    """Lee un CSV y convierte a número las columnas que lo son.

    Parámetros:
        ruta: ruta del archivo CSV.
    Retorna:
        lista de filas como diccionarios (int o float si se puede, si no str).
    """
    filas = []
    with open(ruta, newline="", encoding="utf-8") as f:
        for fila in csv.DictReader(f):
            convertida = {}
            for k, v in fila.items():
                try:
                    convertida[k] = int(v)
                except (ValueError, TypeError):
                    try:
                        convertida[k] = float(v)
                    except (ValueError, TypeError):
                        convertida[k] = v
            filas.append(convertida)
    return filas


def num(x: float, decimales: int = 2) -> str:
    """Formatea un número con coma decimal y espacio duro como separador de miles (estilo español).

    Parámetros:
        x: número.
        decimales: cantidad de decimales.
    Retorna:
        texto formateado, por ejemplo 1 234,56 (el espacio es U+00A0).
    """
    s = f"{x:,.{decimales}f}"
    return s.replace(",", "\u00a0").replace(".", ",")


def sig(x: float, cifras: int = 3) -> str:
    """Formatea un número con una cantidad de cifras significativas (al menos las enteras).

    Parámetros:
        x: número.
        cifras: cifras significativas.
    Retorna:
        texto formateado con num(); por ejemplo, 83,197 con 3 cifras da "83,2".
    """
    if x == 0:
        return "0"
    return num(x, max(0, cifras - 1 - int(math.floor(math.log10(abs(x))))))


def duracion(segundos: float) -> str:
    """Formatea una duración larga en la unidad más legible (s, min u h).

    Parámetros:
        segundos: duración en segundos.
    Retorna:
        texto como "22 h", "32 min" o "45 s".
    """
    if segundos >= 3600:
        return f"{sig(segundos / 3600, 2)} h"
    if segundos >= 60:
        return f"{sig(segundos / 60, 2)} min"
    return f"{sig(segundos, 2)} s"


def alpha_txt(a: float) -> str:
    """Formatea un valor de α (0,9 en vez de 0.9).

    Parámetros:
        a: α.
    Retorna:
        texto con coma decimal.
    """
    return f"{a:g}".replace(".", ",")


def lista_txt(elementos: list[str]) -> str:
    """Une textos en español: "a", "a y b", "a, b y c".

    Parámetros:
        elementos: textos a unir.
    Retorna:
        texto unido (vacío si no hay elementos).
    """
    if not elementos:
        return ""
    if len(elementos) == 1:
        return elementos[0]
    return ", ".join(elementos[:-1]) + " y " + elementos[-1]


def leer_clave_valor(ruta: str) -> dict:
    """Lee un CSV clave,valor (como resultados/entorno.csv).

    Parámetros:
        ruta: archivo CSV.
    Retorna:
        diccionario clave -> valor (texto).
    """
    with open(ruta, newline="", encoding="utf-8") as f:
        return {fila["clave"]: fila["valor"] for fila in csv.DictReader(f)}


def requerido(resultados: str, nombre: str, comando: str) -> str:
    """Devuelve la ruta de un archivo de resultados o termina con un mensaje claro si falta.

    Parámetros:
        resultados: carpeta de resultados.
        nombre: archivo dentro de la carpeta.
        comando: comando de make que lo genera (para el mensaje).
    Retorna:
        ruta del archivo.
    """
    ruta = os.path.join(resultados, nombre)
    if not os.path.exists(ruta):
        raise SystemExit(f"Falta {ruta}: ejecute `{comando}` en esta máquina antes de generar el informe.")
    return ruta


# ----------------------------------------------------------------------------
# Tabla de entorno
# ----------------------------------------------------------------------------


def cache_txt(texto: str) -> str:
    """Convierte "4096 KiB × 1" en "4 MiB" (y "32 KiB × 4" en "32 KiB × 4").

    Parámetros:
        texto: valor de cache_* de entorno.csv.
    Retorna:
        texto legible.
    """
    try:
        tam, n = [p.strip() for p in texto.split("×")]
        kib = int(tam.split()[0])
        legible = f"{kib // 1024} MiB" if kib >= 1024 and kib % 1024 == 0 else f"{kib} KiB"
        return legible if n == "1" else f"{legible} × {n}"
    except (ValueError, IndexError):
        return texto


def tabla_entorno(resultados: str, salida: str) -> dict:
    """Escribe tabla_entorno.csv (componente, valor) con el entorno de la máquina.

    Parámetros:
        resultados: carpeta con entorno.csv y latencia.csv.
        salida: carpeta de salida.
    Retorna:
        el diccionario de entorno.csv.
    """
    e = leer_clave_valor(requerido(resultados, "entorno.csv", "make entorno"))
    lat = {f["modo"]: f for f in leer_csv(requerido(resultados, "latencia.csv", "make entorno"))}
    decimal = lambda s: s.replace(".", ",")  # noqa: E731
    maquina = f"{e['cpu_modelo']}; {e['cpu_nucleos']} núcleos, {e['cpu_hilos']} hilos"
    if e.get("cpu_frec_max_mhz", "no disponible") != "no disponible":
        maquina += f", hasta {num(float(e['cpu_frec_max_mhz']) / 1000, 2)} GHz"
    if e.get("virtualizacion", "none") != "none":
        maquina += f"; máquina virtual ({e['virtualizacion']})"
    if e.get("cpu_gobernador", "no disponible") != "no disponible":
        maquina += f"; gobernador de frecuencia: {e['cpu_gobernador']}"
    disco = f"{e['disco']}; sistema de archivos {e['sistema_archivos']}"
    if "compress" in e.get("opciones_montaje", ""):
        disco += " con compresión (los archivos de los árboles se crean sin compresión, NOCOW)"
    a, s = lat["aleatoria"], lat["secuencial"]
    latencia = (f"lectura de 4 KiB: mediana {num(a['mediana_ms'], 3)} ms (aleatoria) y {num(s['mediana_ms'], 3)} ms "
                f"(secuencial), {num(a['lecturas'], 0)} lecturas de cada tipo"
                + (" con O_DIRECT" if a["odirect"] == 1 else f" SIN lectura directa ({a['motivo_sin_odirect']})"))
    filas = [
        ["Sistema operativo", f"{e['so']}, kernel Linux {e['kernel']}"],
        ["Procesador", maquina],
        ["Cachés", f"L1d {cache_txt(e['cache_l1d'])}, L1i {cache_txt(e['cache_l1i'])}, L2 {cache_txt(e['cache_l2'])}, "
                   f"L3 {cache_txt(e['cache_l3'])}"],
        ["RAM", f"{decimal(e['ram'])} ({decimal(e['ram_disponible'])} disponibles al registrar el entorno); "
                f"swap de {decimal(e['swap'])}"],
        ["Disco de los árboles", disco],
        ["Latencia del disco", latencia],
        ["Compilador", f"{e['compilador']}, {e['flags_compilacion']}"],
        ["Gráficos", f"Python {platform.python_version()}, matplotlib {matplotlib.__version__}"],
    ]
    ruta = os.path.join(salida, "tabla_entorno.csv")
    with open(ruta, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["componente", "valor"])
        w.writerows(filas)
    print(f"  {ruta}")
    return e


# ----------------------------------------------------------------------------
# Valores
# ----------------------------------------------------------------------------


def exponentes_desde(cumple: dict[int, bool]) -> int | None:
    """Primer exponente desde el cual una condición se cumple para todos los mayores.

    Parámetros:
        cumple: exponente -> la condición se cumple.
    Retorna:
        el exponente, o None si no se cumple en el mayor.
    """
    desde = None
    for e in sorted(cumple, reverse=True):
        if not cumple[e]:
            break
        desde = e
    return desde


def comparar_pareado(busq: dict, e: int, a: str, b: str) -> tuple[float, float]:
    """Diferencia de tiempo de búsqueda entre dos árboles, pareada por consulta y repetición.

    Parámetros:
        busq: (tipo, exponente, repetición, id) -> tiempo (ms).
        e: exponente de N.
        a, b: árboles a comparar.
    Retorna:
        (promedio de t_a - t_b, error estándar de ese promedio) en ms.
    """
    dif = [busq[(a, e, r, i)] - t for (tb, eb, r, i), t in busq.items() if tb == b and eb == e and (a, e, r, i) in busq]
    if len(dif) < 2:
        return 0.0, float("inf")
    return statistics.fmean(dif), statistics.stdev(dif) / math.sqrt(len(dif))


def generar_valores(resultados: str, salida: str, res: dict, res_alpha: dict, consulta_fig: list[int],
                    consulta_tabla: list[int]) -> None:
    """Calcula todos los números del informe y los guarda en valores.json.

    Parámetros:
        resultados: carpeta con los CSV del programa C++.
        salida: carpeta de salida (informe/figuras).
        res: resumen del experimento principal (salida de agregar() por exponente).
        res_alpha: tipo -> resumen del experimento de α (salida de agregar() por α).
        consulta_fig: exponentes de N dibujados en la figura de la misma búsqueda.
        consulta_tabla: exponentes de N de la tabla de la misma búsqueda.
    """
    v: dict[str, str] = {}
    b: dict[str, bool] = {}
    l: dict[str, list[int]] = {}
    n: dict[str, float] = {}
    entorno = tabla_entorno(resultados, salida)

    E = sorted({e for (_, e) in res})
    emin, emax = E[0], E[-1]
    R = lambda t, e: res[(t, e)]  # noqa: E731
    filas = leer_csv(os.path.join(resultados, "principal.csv"))
    busquedas = leer_csv(os.path.join(resultados, "principal_busquedas.csv"))
    v["n_min"], v["n_max"] = str(emin), str(emax)
    l["exponentes"] = E
    v["reps_principal"] = str(min(R(t, e)["repeticiones"] for t in ARBOLES for e in E))
    v["busquedas_principal"] = str(R("B", emin)["busquedas"] // R("B", emin)["repeticiones"])
    v["construcciones_principal"] = num(len(filas), 0)
    v["busquedas_total_principal"] = num(len(busquedas), 0)

    # α usado en el experimento principal y α elegido por el bonus.
    for t in ("BE4", "BE126"):
        usados = sorted({f["alpha"] for f in filas if f["tipo"] == t})
        v[f"alpha_usado_{t}"] = lista_txt([alpha_txt(a) for a in usados])
        ruta = os.path.join(resultados, f"alpha_{t}_elegido.csv")
        b[f"hay_elegido_{t}"] = os.path.exists(ruta)
        if os.path.exists(ruta):
            el = leer_csv(ruta)[0]
            v[f"alpha_elegido_{t}"] = alpha_txt(el["alpha"])
            b[f"usado_es_elegido_{t}"] = usados == [el["alpha"]]
            b[f"criterio_ambos_{t}"] = el.get("criterio", "carga_y_busqueda") == "carga_y_busqueda"
            v["tol_carga"] = num(el["tolerancia_carga"] * 100, 1)
            v["tol_busqueda"] = num(el["tolerancia_busqueda"] * 100, 0)
    b["alpha_iguales"] = v["alpha_usado_BE4"] == v["alpha_usado_BE126"]

    # Pruebas, lectura directa, datos y latencia.
    ruta = os.path.join(resultados, "pruebas.csv")
    b["hay_pruebas"] = os.path.exists(ruta)
    if b["hay_pruebas"]:
        p = leer_csv(ruta)[0]
        v["pruebas_verificaciones"], v["pruebas_fallas"] = num(p["verificaciones"], 0), num(p["fallas"], 0)
    todas = filas + [f for t in res_alpha for f in leer_csv(os.path.join(resultados, f"alpha_{t}.csv"))]
    b["odirect_todas"] = all(f["odirect"] == 1 for f in todas)
    v["odirect_motivos"] = lista_txt(sorted({str(f.get("motivo_sin_odirect", "")) for f in todas if f["odirect"] != 1}))
    d = leer_csv(requerido(resultados, "datos.csv", "make entorno"))[0]
    fecha = lambda s: dt.datetime.fromtimestamp(s, dt.timezone.utc).strftime("%d-%m-%Y %H:%M:%S")  # noqa: E731
    v.update(datos_pares=num(d["pares"], 0), datos_llave_min=num(d["llave_min"], 0), datos_llave_max=num(d["llave_max"], 0),
             datos_fecha_min=fecha(d["llave_min"]), datos_fecha_max=fecha(d["llave_max"]),
             datos_repetidas=num(d["llaves_repetidas"], 0), datos_temp_min=num(d["temp_min"], 2),
             datos_temp_max=num(d["temp_max"], 2), datos_mib=num(d["pares"] * 8 / 2**20, 0))
    b["datos_sha_ok"] = entorno.get("datos_ok") == "si"
    lat = {f["modo"]: f for f in leer_csv(os.path.join(resultados, "latencia.csv"))}
    latencia_ms = lat["aleatoria"]["mediana_ms"]
    v["lat_aleatoria"], v["lat_secuencial"] = num(latencia_ms, 3), num(lat["secuencial"]["mediana_ms"], 3)

    # Interferencia: construcciones en que el reloj supera claramente al tiempo de CPU.
    b["hay_interferencia"] = "t_cpu_insercion_s" in filas[0]
    if b["hay_interferencia"]:
        medibles = [f for f in filas if f["t_insercion_s"] >= T_MIN_INTERFERENCIA and f["t_cpu_insercion_s"] > 0]
        sosp = [f for f in medibles if f["t_insercion_s"] / f["t_cpu_insercion_s"] > UMBRAL_INTERFERENCIA
                or f["fallos_mayores_insercion"] > 0]
        cocientes = [f["t_insercion_s"] / f["t_cpu_insercion_s"] for f in medibles] or [1.0]
        v["interf_medibles"], v["interf_sospechosas"] = num(len(medibles), 0), num(len(sosp), 0)
        v["interf_cociente_max"] = num(max(cocientes), 2)
        v["interf_cociente_mediana"] = num(statistics.median(cocientes), 3)
        v["interf_umbral"] = num(UMBRAL_INTERFERENCIA, 2)
        v["interf_t_min"] = num(T_MIN_INTERFERENCIA, 1)
        b["interf_ninguna"] = not sosp
        b["interf_hay_medibles"] = bool(medibles)
        v["interf_fallos_mayores"] = num(sum(f["fallos_mayores_insercion"] + f["fallos_mayores_busqueda"] for f in filas), 0)

    # Creación.
    def orden(campo: str) -> dict[int, bool]:
        """Para cada exponente, si se cumple BE4 < BE126 < B en el campo dado."""
        return {e: R("BE4", e)[campo] < R("BE126", e)[campo] < R("B", e)[campo] for e in E}

    for nombre, campo in (("tiempo", "t_ins"), ("io", "ios_insercion"), ("tamano", "nodos")):
        o = orden(campo)
        b[f"orden_{nombre}_todos"] = all(o.values())
        v[f"orden_{nombre}_cumple"] = f"{sum(o.values())} de {len(E)}"
        l[f"orden_{nombre}_excepciones"] = [e for e in E if not o[e]]
    for t in ARBOLES:
        raiz = 1 if t == "B" else 2
        v[f"t_ins_{t}_max"] = sig(R(t, emax)["t_ins"])
        v[f"us_ins_{t}_min"] = sig(R(t, emin)["t_ins"] / R(t, emin)["N"] * 1e6, 2)
        v[f"us_ins_{t}_max"] = sig(R(t, emax)["t_ins"] / R(t, emax)["N"] * 1e6, 2)
        v[f"io_par_{t}_min"] = num(R(t, emin)["ios_insercion"] / R(t, emin)["N"], 3)
        v[f"io_par_{t}_max"] = num(R(t, emax)["ios_insercion"] / R(t, emax)["N"], 3)
        v[f"sinraiz_{t}_min"] = sig(R(t, emin)["ios_insercion"] / R(t, emin)["N"] - raiz, 3)
        v[f"sinraiz_{t}_max"] = sig(R(t, emax)["ios_insercion"] / R(t, emax)["N"] - raiz, 3)
    sinraiz = {t: R(t, emax)["ios_insercion"] / R(t, emax)["N"] - (1 if t == "B" else 2) for t in ARBOLES}
    for t in ("BE4", "BE126"):
        v[f"ventaja_sinraiz_{t}"] = sig(sinraiz["B"] / sinraiz[t], 2)
        v[f"ventaja_total_{t}"] = sig(R("B", emax)["ios_insercion"] / R(t, emax)["ios_insercion"], 2)
        v[f"raiz_pct_{t}"] = num(2 / (R(t, emax)["ios_insercion"] / R(t, emax)["N"]) * 100, 0)
        difs = []
        for e in E:
            medido = R(t, e)["ios_insercion"] / R(t, e)["N"] - 2
            difs.append(abs(medido - 2 * R(t, e)["lotes"] / R(t, e)["N"]) / medido * 100)
        v[f"modelo_lotes_dif_{t}"] = num(max(difs), 1)
    v["pares_hoja_B_prom"] = num(R("B", emax)["elementos_en_hojas"] / R("B", emax)["hojas"], 0)

    # Tamaño y altura.
    for t in ARBOLES:
        r = R(t, emax)
        v[f"nodos_{t}_max"] = num(r["nodos"], 0)
        v[f"mib_{t}_max"] = num(r["bytes"] / 2**20, 1)
        v[f"altura_{t}_min"], v[f"altura_{t}_max"] = str(R(t, emin)["altura"]), str(r["altura"])
        l[f"altura_{t}_sube"] = [e for e in E[1:] if R(t, e)["altura"] > R(t, e - 1)["altura"]]
        pares = r["elementos_en_hojas"] / r["hojas"]
        v[f"pares_hoja_{t}"] = num(pares, 0)
        v[f"ocupacion_{t}"] = num(pares / CAPACIDAD_HOJA[t] * 100, 0)
        v[f"pct_internos_{t}"] = num(r["elementos_en_internos"] / r["N"] * 100, 1)
    for t in ("BE4", "BE126"):
        v[f"menos_nodos_{t}"] = num((1 - R(t, emax)["nodos"] / R("B", emax)["nodos"]) * 100, 0)
    niveles = R("BE4", emax)["altura"] - R("BE4", emin)["altura"]
    v["be4_niveles_sumados"] = str(niveles)
    v["be4_factor_por_nivel"] = num(2 ** ((emax - emin) / niveles), 1) if niveles else "-"

    # Búsquedas.
    v["frac_resultados"] = num(statistics.fmean(f["resultados"] / f["N"] for f in busquedas) * 100, 3)
    v["frac_teorica"] = num(ANCHO / (LLAVE_MAX - LLAVE_MIN) * 100, 3)
    v["res_min"], v["res_max"] = num(R("B", emin)["res_bus"], 0), num(R("B", emax)["res_bus"], 0)
    por_consulta = defaultdict(set)
    for f in busquedas:
        por_consulta[(f["exponente"], f["id"])].add(f["resultados"])
    b["mismos_resultados"] = all(len(s) == 1 for s in por_consulta.values())
    for t in ARBOLES:
        v[f"io_bus_{t}_min"], v[f"io_bus_{t}_max"] = num(R(t, emin)["io_bus"], 1), num(R(t, emax)["io_bus"], 1)
        v[f"t_bus_{t}_max"] = sig(R(t, emax)["t_bus"])
        v[f"rel_{t}_min"] = num(R(t, emin)["io_bus"] / R("B", emin)["io_bus"], 2)
        v[f"rel_{t}_max"] = num(R(t, emax)["io_bus"] / R("B", emax)["io_bus"], 2)
    b["be4_mas_io_min"] = R("BE4", emin)["io_bus"] > max(R("B", emin)["io_bus"], R("BE126", emin)["io_bus"])
    cruce = exponentes_desde({e: R("BE4", e)["io_bus"] < min(R("B", e)["io_bus"], R("BE126", e)["io_bus"]) for e in E})
    b["hay_cruce"] = cruce is not None and cruce > emin
    l["cruce"] = [cruce] if b["hay_cruce"] else []
    por_io = defaultdict(list)
    tiempos = {}
    for f in busquedas:
        por_io[(f["tipo"], f["exponente"])].append(f["tiempo_ms"] / f["ios"])
        tiempos[(f["tipo"], f["exponente"], f["repeticion"], f["id"])] = f["tiempo_ms"]
    ms_io = {k: statistics.fmean(x) for k, x in por_io.items()}
    v["ms_io_min"], v["ms_io_max"] = num(min(ms_io.values()), 2), num(max(ms_io.values()), 2)
    v["ms_io_max_min"] = num(min(ms_io[(t, emax)] for t in ARBOLES), 2)
    v["ms_io_max_max"] = num(max(ms_io[(t, emax)] for t in ARBOLES), 2)
    clase = {}
    for e in E:
        comp = [comparar_pareado(tiempos, e, "BE4", o) for o in ("B", "BE126")]
        if all(m < -2 * s for m, s in comp):
            clase[e] = "rapido"
        elif all(m > 2 * s for m, s in comp):
            clase[e] = "lento"
        else:
            clase[e] = "empate"
    rapido = exponentes_desde({e: clase[e] == "rapido" for e in E})
    b["be4_rapido_al_final"] = rapido is not None
    l["be4_rapido_desde"] = [rapido] if rapido is not None else []
    l["be4_lento"] = [e for e in E if clase[e] == "lento"]
    l["be4_empate"] = [e for e in E if clase[e] == "empate"]
    m, s = comparar_pareado(tiempos, emax, "B", "BE126")
    v["b_vs_126_dif"], v["b_vs_126_ee"] = sig(abs(m), 2), sig(s, 2)
    b["b_vs_126_sig"] = abs(m) > 2 * s
    b["b_mas_lento_126"] = m > 0
    l["b_vs_126_sig_n"] = [e for e in E if abs(comparar_pareado(tiempos, e, "B", "BE126")[0])
                           > 2 * comparar_pareado(tiempos, e, "B", "BE126")[1]]

    # Una misma búsqueda.
    c0 = leer_csv(os.path.join(resultados, "consultas_principal.csv"))[0]
    l0, u0 = c0["l"], c0["u"]
    v["c0_l"], v["c0_u"] = num(l0, 0), num(u0, 0)
    v["c0_inicio"] = dt.datetime.fromtimestamp(l0, ZONA).strftime("%d-%m-%Y a las %H:%M")
    v["c0_fin"] = dt.datetime.fromtimestamp(u0, ZONA).strftime("%d-%m-%Y a las %H:%M")
    l["consulta_fig"] = [e for e in consulta_fig if os.path.exists(os.path.join(resultados, f"consulta0_N{e}.csv"))]
    l["consulta_tabla"] = [e for e in consulta_tabla if os.path.exists(os.path.join(resultados, f"consulta0_N{e}.csv"))]
    datos0 = {e: leer_csv(os.path.join(resultados, f"consulta0_N{e}.csv")) for e in set(l["consulta_fig"] + l["consulta_tabla"])}
    if datos0:
        mayor = max(datos0)
        conj = {(p["llave"], p["valor"]) for p in datos0[mayor]}
        b["c0_contenido"] = all((p["llave"], p["valor"]) in conj for e in datos0 for p in datos0[e])
        v["c0_mayor"] = str(mayor)
        for e, pares in datos0.items():
            llaves = sorted(p["llave"] for p in pares)
            brecha = statistics.median(bb - a for a, bb in zip(llaves, llaves[1:])) if len(llaves) > 1 else 0
            v[f"c0_pares_{e}"] = num(len(pares), 0)
            v[f"c0_brecha_{e}"] = f"{sig(brecha / 60, 2)} minutos" if brecha >= 120 else f"{sig(brecha, 2)} segundos"
            vals = [p["valor"] for p in pares]
            v[f"c0_prom_{e}"], v[f"c0_tmin_{e}"] = num(statistics.fmean(vals), 1), num(min(vals), 1)
        dias = defaultdict(list)
        for p in datos0[mayor]:
            dias[dt.datetime.fromtimestamp(p["llave"], ZONA).date()].append(p["valor"])
        completos = [d0 for d0 in dias if dt.datetime.combine(d0, dt.time(), ZONA).timestamp() >= l0
                     and dt.datetime.combine(d0 + dt.timedelta(days=1), dt.time(), ZONA).timestamp() <= u0]
        maximos = [max(dias[d0]) for d0 in completos] or [0]
        minimos = [min(dias[d0]) for d0 in completos] or [0]
        v["c0_dias"] = str(len(completos))
        v["c0_max_diario_min"], v["c0_max_diario_max"] = num(min(maximos), 1), num(max(maximos), 1)
        v["c0_min_diario_min"], v["c0_min_diario_max"] = num(min(minimos), 1), num(max(minimos), 1)

    # Bonus de α.
    for t, ra in res_alpha.items():
        rs = sorted(((a, r) for (tt, a), r in ra.items() if tt == t), key=lambda x: x[0])
        alphas = [a for a, _ in rs]
        N = rs[0][1]["N"]
        io = [r["ios_insercion"] / N for _, r in rs]
        v["alpha_n"] = str(int(round(math.log2(N))))
        v["alpha_busquedas"] = str(rs[0][1]["busquedas"] // rs[0][1]["repeticiones"])
        v["alpha_reps"] = str(min(r["repeticiones"] for _, r in rs))
        v["alpha_lista"] = "; ".join(alpha_txt(a) for a in alphas)
        v["alpha_cantidad"] = str(len(alphas))
        imin = min(range(len(io)), key=lambda i: io[i])
        v[f"a_{t}_io_primero"], v[f"a_{t}_io_ultimo"] = num(io[0], 3), num(io[-1], 3)
        v[f"a_{t}_io_min"], v[f"a_{t}_alpha_io_min"] = num(io[imin], 3), alpha_txt(alphas[imin])
        v[f"a_{t}_baja_pct"] = num((io[0] - io[imin]) / io[0] * 100, 1)
        v[f"a_{t}_baja_sinraiz_pct"] = num((io[0] - io[imin]) / (io[0] - 2) * 100, 0)
        b[f"a_{t}_monotona"] = all(y <= x for x, y in zip(io, io[1:]))
        v[f"a_{t}_subida_max"] = num(max([0] + [(y - x) / x * 100 for x, y in zip(io, io[1:])]), 2)
        lote = [r["elementos_empujados"] / r["lotes"] for _, r in rs]
        v[f"a_{t}_lote_primero"], v[f"a_{t}_lote_ultimo"] = num(lote[0], 1), num(lote[-1], 1)
        v[f"a_{t}_vac_primero"], v[f"a_{t}_vac_ultimo"] = num(rs[0][1]["vaciados"], 0), num(rs[-1][1]["vaciados"], 0)
        v[f"a_{t}_int_primero"] = num(rs[0][1]["elementos_en_internos"] / N * 100, 1)
        v[f"a_{t}_int_ultimo"] = num(rs[-1][1]["elementos_en_internos"] / N * 100, 1)
        tins = [r["t_ins"] for _, r in rs]
        itmin = min(range(len(tins)), key=lambda i: tins[i])
        v[f"a_{t}_t_min"], v[f"a_{t}_t_max"] = sig(min(tins)), sig(max(tins))
        v[f"a_{t}_alpha_t_min"] = alpha_txt(alphas[itmin])
        iob = [r["io_bus"] for _, r in rs]
        v[f"a_{t}_iob_min"], v[f"a_{t}_iob_max"] = num(min(iob), 1), num(max(iob), 1)
        v[f"a_{t}_iob_var"] = num((max(iob) - min(iob)) / min(iob) * 100, 1)
        tb = [r["t_bus"] for _, r in rs]
        v[f"a_{t}_tb_min"], v[f"a_{t}_tb_max"] = sig(min(tb)), sig(max(tb))
        nodos = [r["nodos"] for _, r in rs]
        v[f"a_{t}_nodos_var"] = num((max(nodos) - min(nodos)) / min(nodos) * 100, 1)
        firma = [(r["ios_insercion"], r["nodos"], r["vaciados"], r["lotes"]) for _, r in rs]
        grupos, actual = [], [0]
        for i in range(1, len(rs)):
            if firma[i] == firma[i - 1]:
                actual.append(i)
            else:
                grupos.append(actual)
                actual = [i]
        grupos.append(actual)
        iguales = [lista_txt([alpha_txt(alphas[i]) for i in g]) for g in grupos if len(g) > 1]
        b[f"a_{t}_hay_identicos"] = bool(iguales)
        v[f"a_{t}_identicos"] = "; ".join(iguales)
        if b.get(f"hay_elegido_{t}"):
            el = leer_csv(os.path.join(resultados, f"alpha_{t}_elegido.csv"))[0]
            ie = min(range(len(alphas)), key=lambda i: abs(alphas[i] - el["alpha"]))
            re = rs[ie][1]
            v[f"a_{t}_io_elegido"] = num(io[ie], 3)
            v[f"a_{t}_dif_carga_elegido"] = num((io[ie] - io[imin]) / io[imin] * 100, 2)
            v[f"a_{t}_dif_bus_elegido"] = num((iob[ie] - min(iob)) / min(iob) * 100, 1)
            v[f"a_{t}_t_elegido"] = sig(tins[ie])
            b[f"a_{t}_t_elegido_es_min"] = ie == itmin
            rapido_max = rs[itmin][1]["t_ins_max"]
            b[f"a_{t}_t_superpone"] = re["t_ins_min"] <= rapido_max
            v[f"a_{t}_t_dif_pct"] = num((tins[ie] - min(tins)) / min(tins) * 100, 1)
            mayores = [i for i in range(len(alphas)) if alphas[i] > alphas[ie]]
            b[f"a_{t}_hay_mayores"] = bool(mayores)
            if mayores:
                vac_mayor = max(rs[i][1]["vaciados"] for i in mayores)
                v[f"a_{t}_menos_vaciados"] = num((1 - re["vaciados"] / vac_mayor) * 100, 0)
        filas_b = leer_csv(os.path.join(resultados, f"alpha_{t}_busquedas.csv"))
        msio = defaultdict(list)
        for f in filas_b:
            msio[f["alpha"]].append(f["tiempo_ms"] / f["ios"])
        prom = [statistics.fmean(x) for x in msio.values()]
        v[f"a_{t}_ms_io_min"], v[f"a_{t}_ms_io_max"] = num(min(prom), 2), num(max(prom), 2)

    # Análisis de la inserción.
    rb = R("B", emax)
    v["esc_par_B"] = num(rb["escrituras_insercion"] / rb["N"], 3)
    b["identidad_B"] = all(f["escrituras_insercion"] == 1 + f["N"] + 3 * (f["divisiones_hoja"] + f["divisiones_internas"])
                           for f in filas if f["tipo"] == "B")
    todas_be = [f for f in todas if f["tipo"] != "B"]
    b["identidad_Be"] = all(f["lecturas_insercion"] == f["N"] + f["lotes"]
                            and f["escrituras_insercion"] == f["N"] + f["lotes"] + f["nodos"] for f in todas_be)
    v["construcciones_be"] = num(len(todas_be), 0)
    v["construcciones_b"] = num(sum(f["tipo"] == "B" for f in filas), 0)
    for t in ("BE4", "BE126"):
        r = R(t, emax)
        dd, mm = r["elementos_empujados"] / r["N"], r["elementos_empujados"] / r["lotes"]
        modelo, medido = 2 + 2 * dd / mm, r["ios_insercion"] / r["N"]
        v[f"eq_d_{t}"], v[f"eq_m_{t}"] = num(dd, 2), num(mm, 1)
        v[f"eq_modelo_{t}"], v[f"eq_medido_{t}"] = num(modelo, 3), num(medido, 3)
        v[f"eq_dif_{t}"], v[f"nodos_par_{t}"] = num(medido - modelo, 3), num(r["nodos"] / r["N"], 3)
        v[f"costo_bajada_{t}"] = num(2 / mm, 2)
        v[f"fanout_{t}"] = num((r["nodos"] - 1) / r["nodos_internos"], 2 if t == "BE4" else 0)
    v["fanout_B"] = num((rb["nodos"] - 1) / rb["nodos_internos"], 0)
    for t in ARBOLES:
        r = R(t, emax)
        F = (r["nodos"] - 1) / r["nodos_internos"]
        v[f"logF_{t}"] = num(math.log(r["hojas"]) / math.log(F), 1)
        v[f"hojas_{t}"] = num(r["hojas"], 0)
        v[f"niveles_internos_{t}"] = str(r["altura"] - 1)
    v["fanout_teorico_BE4"] = num(506 / ((R("BE4", emax)["nodos"] - 1) / R("BE4", emax)["nodos_internos"]), 0)
    # Extrapolación a un disco real con la latencia medida.
    seg = lambda ios: ios * latencia_ms / 1000  # noqa: E731
    v["ext_B"] = duracion(seg(rb["ios_insercion"]))
    v["ext_B_sinraiz"] = duracion(seg(rb["ios_insercion"] - rb["N"]))
    for t in ("BE4", "BE126"):
        r = R(t, emax)
        v[f"ext_{t}_sinraiz"] = duracion(seg(r["ios_insercion"] - 2 * r["N"]))
    us_io = {(t, e): R(t, e)["t_ins"] / R(t, e)["ios_insercion"] * 1e6 for t in ARBOLES for e in E}
    chicos = [x for (t, e), x in us_io.items() if e <= min(20, emax)]
    v["us_io_chico_min"], v["us_io_chico_max"] = num(min(chicos), 2), num(max(chicos), 2)
    v["us_io_max_min"] = num(min(us_io[(t, emax)] for t in ARBOLES), 2)
    v["us_io_max_max"] = num(max(us_io[(t, emax)] for t in ARBOLES), 2)
    v["us_io_BE4_max"], v["us_io_B_max"] = num(us_io[("BE4", emax)], 2), num(us_io[("B", emax)], 2)
    b["be4_mas_barato_por_io"] = us_io[("BE4", emax)] < min(us_io[("B", emax)], us_io[("BE126", emax)])
    v["razon_t_B_BE4"] = sig(R("B", emax)["t_ins"] / R("BE4", emax)["t_ins"], 2)
    v["razon_io_B_BE4"] = sig(R("B", emax)["ios_insercion"] / R("BE4", emax)["ios_insercion"], 2)
    v["mib_chico"] = num(max(R(t, min(20, emax))["bytes"] for t in ARBOLES) / 2**20, 0)
    v["mib_grande_min"] = sig(min(R(t, emax)["bytes"] for t in ARBOLES) / 2**30, 2)
    v["mib_grande_max"] = sig(max(R(t, emax)["bytes"] for t in ARBOLES) / 2**30, 2)

    # Modelo de búsqueda.
    p = ANCHO / (LLAVE_MAX - LLAVE_MIN)
    difs = [abs(R(t, e)["io_bus"] / (p * R(t, e)["nodos"] + R(t, e)["altura"]) - 1) * 100 for t in ARBOLES for e in E
            if e >= min(18, emax)]
    v["modelo_bus_dif_max"] = num(max(difs), 1)
    v["p_pct"] = num(p * 100, 2)
    pn = {t: p * R(t, emax)["nodos"] for t in ARBOLES}
    v["pn_max_min"], v["pn_max_max"] = num(min(pn.values()), 0), num(max(pn.values()), 0)
    b["pn_min_menor_1"] = all(p * R(t, emin)["nodos"] < 1 for t in ARBOLES)
    v["pn_min_max"] = num(max(p * R(t, emin)["nodos"] for t in ARBOLES), 2)

    # Umbral del vaciado con c = 126: f ≈ l_max / (hijos promedio) y ceil(α f) para los α extremos y el elegido.
    r126 = R("BE126", emax)
    f126 = 384 / ((r126["nodos"] - 1) / r126["nodos_internos"])
    v["f_BE126"] = num(f126, 0)
    alphas126 = sorted(a for (tt, a) in res_alpha.get("BE126", {}) if tt == "BE126")
    if alphas126:
        fe = math.ceil(f126 - 1e-9)
        v["umbral_min_BE126"] = str(max(1, math.ceil(alphas126[0] * fe - 1e-9)))
        v["alpha_min_BE126"] = alpha_txt(alphas126[0])
        if b.get("hay_elegido_BE126"):
            ael = leer_csv(os.path.join(resultados, "alpha_BE126_elegido.csv"))[0]["alpha"]
            v["umbral_elegido_BE126"] = str(max(1, math.ceil(ael * fe - 1e-9)))
    # El compromiso carga/búsqueda "aparece" si α cambia las I/Os de búsqueda en más de 5 %.
    b["alpha_sin_compromiso"] = all(float(v[f"a_{t}_iob_var"].replace(",", ".")) <= 5 for t in res_alpha)
    fecha_e = entorno.get("fecha", "")[:10]
    v["fecha_entorno"] = "-".join(reversed(fecha_e.split("-"))) if fecha_e else "(sin fecha)"

    # Hipótesis.
    b["h1"] = b["orden_io_todos"] and b["orden_tiempo_todos"]
    b["h1_io"] = b["orden_io_todos"]
    b["h2_alturas"] = (R("BE4", emax)["altura"] > R("BE126", emax)["altura"]
                       and R("BE126", emax)["altura"] <= R("B", emax)["altura"] + 1)
    b["h3"] = b["orden_tamano_todos"]

    datos = {"v": v, "b": b, "l": l, "n": n}
    ruta = os.path.join(salida, "valores.json")
    with open(ruta, "w", encoding="utf-8") as f:
        json.dump(datos, f, ensure_ascii=False, indent=1, sort_keys=True)
    print(f"  {ruta} ({len(v)} textos, {len(b)} condiciones)")
