#!/usr/bin/env python3
"""Gráficos y tablas del informe a partir de los CSV generados por el programa C++.

Entradas (en --resultados):
    principal.csv, principal_busquedas.csv   experimento principal (N = 2^15..2^26)
    consulta0_N{E}.csv                       pares de la búsqueda 0 para N = 2^E
    alpha_BE126.csv, alpha_BE126_busquedas.csv   experimento bonus de α (N = 2^24)

Salidas (en --salida): figuras SVG y tablas CSV formateadas que lee informe.typ.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import math
import os
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator  # noqa: E402

try:
    from zoneinfo import ZoneInfo

    ZONA = ZoneInfo("America/Santiago")
    NOMBRE_ZONA = "hora de Chile continental"
except Exception:  # pragma: no cover
    ZONA = dt.timezone.utc
    NOMBRE_ZONA = "UTC"

ARBOLES = ["B", "BE4", "BE126"]
ETIQUETAS = {
    "B": "Árbol B (b = 340)",
    "BE4": "Bε, ε ≈ 0,25 (c = 4, l = 506)",
    "BE126": "Bε, ε ≈ 0,75 (c = 126, l = 384)",
}
COLORES = {"B": "#1f77b4", "BE4": "#d95f02", "BE126": "#1b9e77"}
MARCADORES = {"B": "o", "BE4": "s", "BE126": "^"}

plt.rcParams.update(
    {
        "font.family": "DejaVu Sans",
        "font.size": 9,
        "axes.titlesize": 9.5,
        "axes.labelsize": 9,
        "legend.fontsize": 8,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linewidth": 0.6,
        "lines.linewidth": 1.4,
        "lines.markersize": 4.5,
        "savefig.bbox": "tight",
        "svg.fonttype": "path",
        "axes.formatter.use_locale": False,
    }
)


def leer_csv(ruta: str) -> list[dict]:
    """Lee un CSV y convierte a int o float las columnas numéricas.

    Parámetros:
        ruta: ruta del archivo CSV (entrada).
    Retorna:
        lista de diccionarios por fila.
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
    """Formatea un número con coma decimal y espacio duro como separador de miles.

    Parámetros:
        x: valor numérico (entrada).
        decimales: cantidad de decimales (entrada).
    Retorna:
        cadena formateada.
    """
    s = f"{x:,.{decimales}f}"
    return s.replace(",", "\u00a0").replace(".", ",")


def prom_desv(valores: list[float]) -> tuple[float, float]:
    """Calcula el promedio y la desviación estándar muestral.

    Parámetros:
        valores: lista de números no vacía (entrada).
    Retorna:
        tupla (promedio, desviación).
    """
    if len(valores) == 1:
        return float(valores[0]), 0.0
    return statistics.fmean(valores), statistics.stdev(valores)


def agregar(arboles: list[dict], busquedas: list[dict], clave: str) -> dict:
    """Agrupa las repeticiones de cada configuración (tipo, clave).

    Parámetros:
        arboles: filas del CSV de árboles (entrada).
        busquedas: filas del CSV de búsquedas (entrada).
        clave: columna de agrupación, "exponente" o "alpha" (entrada).
    Retorna:
        diccionario {(tipo, valor): resumen}.
    """
    grupos = defaultdict(list)
    for f in arboles:
        grupos[(f["tipo"], f[clave])].append(f)
    por_busqueda = defaultdict(list)
    for b in busquedas:
        por_busqueda[(b["tipo"], b[clave])].append(b)
    resumen = {}
    deterministas = [
        "N", "ios_insercion", "lecturas_insercion", "escrituras_insercion", "nodos", "bytes",
        "altura", "nodos_internos", "hojas", "elementos_en_internos", "elementos_en_hojas",
        "vaciados", "lotes", "elementos_empujados", "divisiones_hoja", "divisiones_internas",
    ]
    for k, filas in grupos.items():
        r = {col: filas[0][col] for col in deterministas}
        r["repeticiones"] = len(filas)
        tiempos = [f["t_insercion_s"] for f in filas]
        r["t_ins"] = statistics.median(tiempos)
        r["t_ins_prom"], r["t_ins_desv"] = prom_desv(tiempos)
        r["t_ins_min"] = min(tiempos)
        r["t_ins_max"] = max(tiempos)
        r["t_ins_err"] = [r["t_ins"] - r["t_ins_min"], r["t_ins_max"] - r["t_ins"]]
        r["t_esc"], _ = prom_desv([f["t_escritura_s"] for f in filas])
        bs = por_busqueda[k]
        r["busquedas"] = len(bs)
        r["t_bus"], r["t_bus_desv"] = prom_desv([b["tiempo_ms"] for b in bs])
        r["t_bus_eem"] = r["t_bus_desv"] / math.sqrt(len(bs))
        r["io_bus"], r["io_bus_desv"] = prom_desv([b["ios"] for b in bs])
        r["res_bus"], _ = prom_desv([b["resultados"] for b in bs])
        r["ms_por_io"] = statistics.fmean([b["tiempo_ms"] / b["ios"] for b in bs if b["ios"] > 0])
        # Valores por par insertado. La raíz cuesta 1 I/O por inserción en el árbol B
        # (lectura) y 2 en los Bε (lectura y escritura).
        r["us_por_par"] = r["t_ins"] / r["N"] * 1e6
        r["ios_por_par"] = r["ios_insercion"] / r["N"]
        r["ios_por_par_sin_raiz"] = r["ios_por_par"] - (1 if k[0] == "B" else 2)
        r["ios_lotes_por_par"] = 2 * r["lotes"] / r["N"]
        resumen[k] = r
    return resumen


def eje_n(ax, exponentes: list[int], cada: int = 1) -> None:
    """Configura el eje x en escala log2 con marcas 2^E.

    Parámetros:
        ax: eje de matplotlib (entrada/salida).
        exponentes: lista de exponentes presentes (entrada).
        cada: paso entre marcas rotuladas (entrada).
    """
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_locator(FixedLocator([2**e for e in exponentes]))
    ax.xaxis.set_minor_locator(NullLocator())

    def rotulo(x, _):
        e = int(round(math.log2(x)))
        return f"$2^{{{e}}}$" if (e - exponentes[0]) % cada == 0 else ""

    ax.xaxis.set_major_formatter(FuncFormatter(rotulo))
    ax.set_xlabel("N (pares insertados)")


def leyenda_abajo(fig, ax, ncol: int = 3) -> None:
    """Coloca una leyenda común centrada debajo de los paneles.

    Parámetros:
        fig: figura de matplotlib (entrada/salida).
        ax: eje del cual extraer las etiquetas (entrada).
        ncol: número de columnas (entrada).
    """
    handles, labels = ax.get_legend_handles_labels()
    fig.tight_layout(rect=(0, 0.09, 1, 1), w_pad=1.2)
    fig.legend(handles, labels, loc="lower center", ncol=ncol, frameon=False, bbox_to_anchor=(0.5, 0.0),
               columnspacing=1.6, handlelength=2.2)


def serie(resumen: dict, tipo: str, campo: str, exponentes: list[int]) -> tuple[list, list]:
    """Extrae una serie (N, valor) para un tipo de árbol.

    Parámetros:
        resumen: diccionario agregado por exponente (entrada).
        tipo: "B", "BE4" o "BE126" (entrada).
        campo: clave de la métrica (entrada).
        exponentes: lista de exponentes (entrada).
    Retorna:
        tupla (xs, ys).
    """
    xs, ys = [], []
    for e in exponentes:
        if (tipo, e) in resumen:
            xs.append(2**e)
            ys.append(resumen[(tipo, e)][campo])
    return xs, ys


def dibujar(ax, resumen, campo, exponentes, desv=None, escala=1.0, tipos=ARBOLES):
    """Dibuja una curva por cada árbol sobre el eje dado.

    Parámetros:
        ax: eje de matplotlib (entrada/salida).
        resumen: diccionario de resultados (entrada).
        campo: métrica a graficar (entrada).
        exponentes: exponentes de N (entrada).
        desv: campo de barras de error opcional (entrada).
        escala: factor multiplicativo (entrada).
        tipos: tipos de árbol a incluir (entrada).
    """
    for t in tipos:
        xs, ys = serie(resumen, t, campo, exponentes)
        if not xs:
            continue
        ys = [y * escala for y in ys]
        if desv:
            _, es = serie(resumen, t, desv, exponentes)
            if es and isinstance(es[0], list):
                yerr = [[e[0] * escala for e in es], [e[1] * escala for e in es]]
            else:
                yerr = [e * escala for e in es]
            ax.errorbar(xs, ys, yerr=yerr, color=COLORES[t], marker=MARCADORES[t],
                        label=ETIQUETAS[t], capsize=2.5, elinewidth=0.8)
        else:
            ax.plot(xs, ys, color=COLORES[t], marker=MARCADORES[t], label=ETIQUETAS[t])


FORMATO = "svg"


def guardar(fig, salida: str, nombre: str) -> None:
    """Guarda una figura en disco y la cierra.

    Parámetros:
        fig: figura de matplotlib (entrada).
        salida: carpeta de destino (entrada).
        nombre: nombre base del archivo (entrada).
    """
    ruta = os.path.join(salida, f"{nombre}.{FORMATO}")
    fig.savefig(ruta, dpi=150)
    plt.close(fig)
    print(f"  {ruta}")


def escribir_tabla(ruta: str, encabezado: list[str], filas: list[list[str]]) -> None:
    """Escribe una tabla CSV formateada para el informe.

    Parámetros:
        ruta: ruta del archivo CSV (entrada).
        encabezado: nombres de las columnas (entrada).
        filas: filas de texto (entrada).
    """
    with open(ruta, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(encabezado)
        w.writerows(filas)
    print(f"  {ruta}")


def coma_decimal(ax, eje: str = "y") -> None:
    """Configura coma decimal en los ejes lineales indicados.

    Parámetros:
        ax: eje de matplotlib (entrada/salida).
        eje: "x", "y" o "xy" (entrada).
    """
    formato = FuncFormatter(lambda v, _: f"{v:.10g}".replace(".", ","))
    if "x" in eje:
        ax.xaxis.set_major_formatter(formato)
    if "y" in eje:
        ax.yaxis.set_major_formatter(formato)


def graficos_principal(resultados: str, salida: str) -> None:
    """Genera las figuras y tablas del experimento principal.

    Parámetros:
        resultados: carpeta con principal.csv y principal_busquedas.csv (entrada).
        salida: carpeta de destino (entrada).
    """
    ruta = os.path.join(resultados, "principal.csv")
    if not os.path.exists(ruta):
        print("(no hay principal.csv: se omiten los gráficos del experimento principal)")
        return
    res = agregar(leer_csv(ruta), leer_csv(os.path.join(resultados, "principal_busquedas.csv")), "exponente")
    exps = sorted({e for (_, e) in res})

    # Creación: tiempo total y por inserción.
    fig, (a, b) = plt.subplots(1, 2, figsize=(7.2, 2.75))
    dibujar(a, res, "t_ins", exps, desv="t_ins_err")
    a.set_yscale("log")
    a.set_ylabel("tiempo de creación (s)")
    a.set_title("(a) Tiempo de creación")
    dibujar(b, res, "us_por_par", exps)
    b.set_ylabel("µs por inserción")
    b.set_title("(b) Tiempo por par insertado")
    b.set_ylim(bottom=0)
    coma_decimal(b)
    for ax in (a, b):
        eje_n(ax, exps)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "creacion_tiempo")

    # Creación: I/Os totales, por inserción y sin contar la raíz.
    fig, (a, b, c) = plt.subplots(1, 3, figsize=(7.2, 2.75))
    dibujar(a, res, "ios_insercion", exps)
    a.set_yscale("log")
    a.set_ylabel("I/Os (lecturas + escrituras)")
    a.set_title("(a) I/Os totales")
    dibujar(b, res, "ios_por_par", exps)
    dibujar(c, res, "ios_por_par_sin_raiz", exps)
    modelo_linea = None
    for t in ("BE4", "BE126"):
        xs, modelo = serie(res, t, "ios_lotes_por_par", exps)
        (modelo_linea,) = c.plot(xs, modelo, color="black", linestyle=":", linewidth=0.9, label="2 · lotes / N")
    b.set_ylabel("I/Os por inserción")
    b.set_title("(b) I/Os por par insertado")
    b.set_ylim(bottom=0)
    coma_decimal(b)
    c.set_yscale("log")
    c.set_ylabel("I/Os por inserción sin la raíz")
    c.set_title("(c) Sin contar la raíz")
    if modelo_linea is not None:
        c.legend(handles=[modelo_linea], loc="center left", bbox_to_anchor=(0.0, 0.62), fontsize=7,
                 handlelength=1.8, frameon=False)
    for ax in (a, b, c):
        eje_n(ax, exps, cada=2)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "creacion_ios")

    # Tamaño y altura.
    fig, (a, b) = plt.subplots(1, 2, figsize=(7.2, 2.75))
    dibujar(a, res, "bytes", exps, escala=1 / 2**20)
    a.set_yscale("log")
    a.set_ylabel("tamaño en disco (MiB)")
    a.set_title("(a) Tamaño (nodos × 4096 B)")
    dibujar(b, res, "altura", exps)
    b.set_ylabel("altura (niveles)")
    b.set_title("(b) Altura")
    b.set_ylim(bottom=0)
    for ax in (a, b):
        eje_n(ax, exps)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "tamano_altura")

    # Búsquedas: tiempo, I/Os e I/Os relativas al árbol B.
    fig, (a, b, c) = plt.subplots(1, 3, figsize=(7.2, 2.75))
    dibujar(a, res, "t_bus", exps, desv="t_bus_desv")
    a.set_yscale("log")
    a.set_ylabel("tiempo por búsqueda (ms)")
    a.set_title("(a) Tiempo promedio")
    dibujar(b, res, "io_bus", exps, desv="io_bus_desv")
    b.set_yscale("log")
    b.set_ylabel("I/Os por búsqueda")
    b.set_title("(b) I/Os promedio")
    for (_, e), r in res.items():
        r["io_bus_rel_b"] = r["io_bus"] / res[("B", e)]["io_bus"]
    dibujar(c, res, "io_bus_rel_b", exps)
    c.set_ylabel("I/Os / I/Os del árbol B")
    c.set_title("(c) Relativas al árbol B")
    coma_decimal(c)
    for ax in (a, b, c):
        eje_n(ax, exps, cada=2)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "busqueda")

    # Tabla modelo de búsqueda p·nodos + altura.
    p = 604800 / (1754006400 - 1546300800)
    filas = []
    for e in (20, 23, 26):
        for t in ARBOLES:
            if (t, e) not in res:
                continue
            r = res[(t, e)]
            modelo = p * r["nodos"] + r["altura"]
            filas.append([f"2^{e}", t, num(r["nodos"], 0), str(r["altura"]), num(modelo, 1), num(r["io_bus"], 1),
                          num((r["io_bus"] / modelo - 1) * 100, 1)])
    escribir_tabla(os.path.join(salida, "tabla_modelo_busqueda.csv"),
                   ["N", "arbol", "nodos", "altura", "modelo", "medido", "dif_pct"], filas)

    # Tabla principal completa.
    filas = []
    for e in exps:
        for t in ARBOLES:
            if (t, e) not in res:
                continue
            r = res[(t, e)]
            filas.append([
                f"2^{e}", t, num(r["t_ins"], 3), num(r["ios_insercion"] / 1e6, 2), num(r["ios_insercion"] / r["N"], 3),
                num(r["nodos"], 0), num(r["bytes"] / 2**20, 1), str(r["altura"]), num(r["t_bus"], 2),
                num(r["io_bus"], 1), num(r["res_bus"], 0),
            ])
    escribir_tabla(os.path.join(salida, "tabla_principal.csv"),
                   ["N", "arbol", "t_ins_s", "ios_ins_M", "ios_por_par", "nodos", "MiB", "altura", "t_bus_ms",
                    "ios_bus", "resultados"], filas)

    # Tabla de estructura para N = 2^26.
    e = max(exps)
    filas = []
    for t in ARBOLES:
        r = res[(t, e)]
        en_int = r["elementos_en_internos"] / r["N"] * 100
        lotes = r["lotes"]
        filas.append([
            t, num(r["nodos_internos"], 0), num(r["hojas"], 0), num(en_int, 2),
            num(r["elementos_en_hojas"] / r["hojas"], 1),
            num(r["vaciados"], 0) if t != "B" else "-",
            num(r["elementos_empujados"] / lotes, 1) if lotes else "-",
            num(r["elementos_empujados"] / r["N"], 2) if t != "B" else "-",
            num(r["lecturas_insercion"] / r["N"], 3), num(r["escrituras_insercion"] / r["N"], 3),
            num(r["ms_por_io"], 3),
        ])
    escribir_tabla(os.path.join(salida, "tabla_estructura.csv"),
                   ["arbol", "internos", "hojas", "pct_en_internos", "pares_por_hoja", "vaciados", "pares_por_lote",
                    "bajadas_por_par", "lecturas_por_par", "escrituras_por_par", "ms_por_io"], filas)


def grafico_consulta(resultados: str, salida: str, exponentes: list[int]) -> None:
    """Dibuja la misma búsqueda por rango (consulta 0) para dos valores de N y genera su tabla.

    Parámetros:
        resultados: carpeta con consulta0_N{E}.csv y consultas_principal.csv (entrada).
        salida: carpeta de destino (entrada).
        exponentes: lista de exponentes de N a comparar (entrada).
    """
    disponibles = [e for e in exponentes if os.path.exists(os.path.join(resultados, f"consulta0_N{e}.csv"))]
    if len(disponibles) < 2:
        print("(faltan archivos consulta0_N*.csv: se omite el gráfico de la consulta)")
        return
    consultas = leer_csv(os.path.join(resultados, "consultas_principal.csv"))
    l, u = consultas[0]["l"], consultas[0]["u"]
    datos = {e: leer_csv(os.path.join(resultados, f"consulta0_N{e}.csv")) for e in disponibles}
    fig, ejes = plt.subplots(len(disponibles), 1, figsize=(7.2, 1.9 * len(disponibles) + 0.4), sharex=True,
                             sharey=True)
    for ax, e in zip(ejes, disponibles):
        pares = datos[e]
        x = [dt.datetime.fromtimestamp(p["llave"], ZONA) for p in pares]
        y = [p["valor"] for p in pares]
        tam = 6 if len(pares) < 5000 else 0.3
        ax.scatter(x, y, s=tam, color="#444444" if len(pares) >= 5000 else "#d95f02", linewidths=0,
                   rasterized=len(pares) >= 5000)
        ax.set_title(f"N = $2^{{{e}}}$: {num(len(pares), 0)} mediciones en el rango", loc="left")
        ax.set_ylabel("temperatura (°C)")
    inicio = dt.datetime.fromtimestamp(l, ZONA)
    fin = dt.datetime.fromtimestamp(u, ZONA)
    ejes[-1].set_xlim(inicio, fin)
    ejes[-1].xaxis.set_major_locator(mdates.DayLocator(tz=ZONA))
    ejes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%d-%m-%Y", tz=ZONA))
    ejes[-1].set_xlabel(f"fecha y hora ({NOMBRE_ZONA})")
    fig.autofmt_xdate(rotation=0, ha="center")
    guardar(fig, salida, "consulta_misma_busqueda")

    filas = []
    mayor = datos[max(datos)]
    conj_mayor = {(p["llave"], p["valor"]) for p in mayor}
    for e in disponibles:
        pares = datos[e]
        vals = [p["valor"] for p in pares]
        llaves = sorted(p["llave"] for p in pares)
        brechas = [b - a for a, b in zip(llaves, llaves[1:])]
        contenido = all((p["llave"], p["valor"]) in conj_mayor for p in pares)
        filas.append([
            f"2^{e}", num(len(pares), 0), num(len(pares) / 2**e * 100, 3), num(min(vals), 2), num(max(vals), 2),
            num(statistics.fmean(vals), 2), num(statistics.median(brechas), 0) if brechas else "-",
            "sí" if contenido else "no",
        ])
    escribir_tabla(os.path.join(salida, "tabla_consulta.csv"),
                   ["N", "pares", "pct_de_N", "t_min", "t_max", "t_prom", "brecha_mediana_s", "contenido_en_mayor"],
                   filas)


def alpha_principal(resultados: str) -> float | None:
    """Obtiene el α con que se construyeron los árboles Bε del experimento principal.

    Parámetros:
        resultados: carpeta con principal.csv (entrada).
    Retorna:
        el valor de α, o None si no hay principal.csv o si los Bε usaron más de un α.
    """
    ruta = os.path.join(resultados, "principal.csv")
    if not os.path.exists(ruta):
        return None
    alphas = {f["alpha"] for f in leer_csv(ruta) if f["tipo"] != "B"}
    return alphas.pop() if len(alphas) == 1 else None


def graficos_alpha(resultados: str, salida: str, tipo: str = "BE126") -> None:
    """Genera la figura y la tabla del experimento bonus de α.

    Parámetros:
        resultados: carpeta con alpha_{tipo}.csv y alpha_{tipo}_busquedas.csv (entrada).
        salida: carpeta de destino (entrada).
        tipo: árbol Bε evaluado (entrada).
    """
    ruta = os.path.join(resultados, f"alpha_{tipo}.csv")
    if not os.path.exists(ruta):
        print(f"(no hay alpha_{tipo}.csv: se omite)")
        return
    res = agregar(leer_csv(ruta), leer_csv(os.path.join(resultados, f"alpha_{tipo}_busquedas.csv")), "alpha")
    alphas = sorted(a for (t, a) in res if t == tipo)
    rs = [res[(tipo, a)] for a in alphas]
    n = rs[0]["N"]
    color = COLORES[tipo]
    fig, ejes = plt.subplots(2, 2, figsize=(7.2, 5.0))
    (a, b), (c, d) = ejes
    a.errorbar(alphas, [r["t_ins"] for r in rs],
               yerr=[[r["t_ins"] - r["t_ins_min"] for r in rs], [r["t_ins_max"] - r["t_ins"] for r in rs]],
               color=color, marker="o", capsize=2.5, elinewidth=0.8)
    a.set_ylabel("tiempo de creación (s)")
    a.set_title("(a) Carga: tiempo de creación")
    b.plot(alphas, [r["ios_insercion"] / n for r in rs], color=color, marker="o")
    b.set_ylabel("I/Os por inserción")
    b.set_title("(b) Carga: I/Os por par insertado")
    c.errorbar(alphas, [r["t_bus"] for r in rs], yerr=[r["t_bus_eem"] for r in rs], color=color, marker="o",
               capsize=2.5, elinewidth=0.8)
    c.set_ylabel("tiempo por búsqueda (ms)")
    c.set_title("(c) Búsqueda: tiempo promedio")
    d.plot(alphas, [r["io_bus"] for r in rs], color=color, marker="o")
    d.set_ylabel("I/Os por búsqueda")
    d.set_title("(d) Búsqueda: I/Os promedio")
    elegido = alpha_principal(resultados)
    for ax in (a, b, c, d):
        ax.set_xlabel("α")
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_locator(FixedLocator([0, 0.2, 0.4, 0.6, 0.8, 1.0]))
        if elegido is not None:
            ax.axvline(elegido, color="gray", linestyle="--", linewidth=0.8)
        coma_decimal(ax, "xy")
    for ax in (b, d):
        ys = ax.get_lines()[0].get_ydata()
        ax.set_ylim(min(ys) * 0.95, max(ys) * 1.05)
    titulo = f"{ETIQUETAS[tipo]}, N = $2^{{{int(math.log2(n))}}}$"
    if elegido is not None:
        titulo += f" (línea punteada: α = {elegido:g})".replace(".", ",")
    fig.suptitle(titulo, fontsize=10)
    fig.tight_layout()
    guardar(fig, salida, f"alpha_{tipo}")

    filas = []
    for alpha, r in zip(alphas, rs):
        filas.append([
            f"{alpha:g}".replace(".", ","), num(r["t_ins"], 2), num(r["t_ins_desv"], 2),
            num(r["ios_insercion"] / 1e6, 2), num(r["ios_insercion"] / n, 3), num(r["vaciados"], 0),
            num(r["elementos_empujados"] / r["lotes"], 1), num(r["elementos_en_internos"] / n * 100, 2),
            num(r["nodos"], 0), num(r["t_bus"], 1), num(r["t_bus_eem"], 1), num(r["io_bus"], 1),
        ])
    escribir_tabla(os.path.join(salida, f"tabla_alpha_{tipo}.csv"),
                   ["alpha", "t_ins_s", "t_ins_desv", "ios_ins_M", "ios_por_par", "vaciados", "pares_por_lote",
                    "pct_en_internos", "nodos", "t_bus_ms", "t_bus_eem", "ios_bus"], filas)


def main() -> int:
    """Punto de entrada: genera las figuras y tablas a partir de resultados/.

    Retorna:
        0 al finalizar.
    """
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--resultados", default="resultados", help="carpeta con los CSV del programa C++")
    p.add_argument("--salida", default="informe/figuras", help="carpeta donde se guardan figuras y tablas")
    p.add_argument("--consulta", default="16,26", help="dos exponentes de N para comparar una misma búsqueda")
    p.add_argument("--formato", default="svg", choices=["svg", "png", "pdf"], help="formato de las figuras")
    args = p.parse_args()
    global FORMATO
    FORMATO = args.formato
    os.makedirs(args.salida, exist_ok=True)
    print("Generando figuras y tablas:")
    exps_consulta = [int(x) for x in args.consulta.split(",") if x]
    graficos_principal(args.resultados, args.salida)
    grafico_consulta(args.resultados, args.salida, exps_consulta)
    graficos_alpha(args.resultados, args.salida, "BE126")
    return 0


if __name__ == "__main__":
    sys.exit(main())
