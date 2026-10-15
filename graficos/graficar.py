#!/usr/bin/env python3
"""Gráficos y tablas del informe a partir de los CSV que genera el programa C++.

Este script solo lee los resultados (carpeta resultados/) y dibuja. No construye
árboles ni ejecuta experimentos: eso lo hace build/tarea2.

Entradas (en --resultados):
    principal.csv, principal_busquedas.csv   experimento principal (N = 2^15..2^26)
    consulta0_N{E}.csv                        pares de la búsqueda 0 para N = 2^E
    alpha_{BE126,BE4}.csv, alpha_*_busquedas.csv   experimento bonus de α (N = 2^24)

Salidas (en --salida): figuras SVG, tablas CSV ya formateadas y valores.json (todos los
números y afirmaciones del texto del informe; ver graficos/valores.py), que lee informe.typ.

Uso:
    .venv/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras
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

from valores import generar_valores, leer_csv, num  # noqa: E402  (mismo directorio que este script)

try:
    from zoneinfo import ZoneInfo

    ZONA = ZoneInfo("America/Santiago")
    NOMBRE_ZONA = "hora de Chile continental"
except Exception:  # pragma: no cover - sin base de datos de zonas horarias
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


# ----------------------------------------------------------------------------
# Lectura y agregación
# ----------------------------------------------------------------------------


def prom_desv(valores: list[float]) -> tuple[float, float]:
    """Calcula el promedio y la desviación estándar muestral.

    Parámetros:
        valores: lista de números (no vacía).
    Retorna:
        (promedio, desviación); la desviación es 0 si hay un solo valor.
    """
    if len(valores) == 1:
        return float(valores[0]), 0.0
    return statistics.fmean(valores), statistics.stdev(valores)


def agregar(arboles: list[dict], busquedas: list[dict], clave: str) -> dict:
    """Junta las repeticiones de cada configuración (tipo, clave).

    Parámetros:
        arboles: filas de principal.csv o alpha_*.csv (una por construcción).
        busquedas: filas de *_busquedas.csv (una por búsqueda).
        clave: columna que distingue las configuraciones ("exponente" o "alpha").
    Retorna:
        {(tipo, valor): resumen} con promedios, desviaciones y las columnas
        deterministas (I/Os, nodos, altura...), que se comprueba que no cambien
        entre repeticiones.
    """
    grupos = defaultdict(list)
    for f in arboles:
        grupos[(f["tipo"], f[clave])].append(f)
    por_busqueda = defaultdict(list)
    for b in busquedas:
        por_busqueda[(b["tipo"], b[clave])].append(b)
    resumen = {}
    deterministas = ["N", "ios_insercion", "lecturas_insercion", "escrituras_insercion", "nodos", "bytes",
                     "altura", "nodos_internos", "hojas", "elementos_en_internos", "elementos_en_hojas",
                     "vaciados", "lotes", "elementos_empujados", "divisiones_hoja", "divisiones_internas"]
    for k, filas in grupos.items():
        for col in deterministas:
            if len({f[col] for f in filas}) != 1:
                raise ValueError(f"{col} cambia entre repeticiones en {k}")
        if not all(f["verificado"] == 1 for f in filas):
            raise ValueError(f"hay búsquedas no verificadas en {k}")
        r = {col: filas[0][col] for col in deterministas}
        r["repeticiones"] = len(filas)
        tiempos = [f["t_insercion_s"] for f in filas]
        # La máquina es compartida y la carga externa solo puede alargar una medición:
        # se usa la mediana de las repeticiones (más robusta que el promedio) y se guarda el rango.
        r["t_ins"] = statistics.median(tiempos)
        r["t_ins_prom"], r["t_ins_desv"] = prom_desv(tiempos)
        r["t_ins_min"] = min(tiempos)
        r["t_ins_max"] = max(tiempos)
        r["t_ins_err"] = [r["t_ins"] - r["t_ins_min"], r["t_ins_max"] - r["t_ins"]]
        r["t_esc"], _ = prom_desv([f["t_escritura_s"] for f in filas])
        bs = por_busqueda[k]
        if not bs or not all(b["correcta"] == 1 for b in bs):
            raise ValueError(f"búsquedas faltantes o incorrectas en {k}")
        r["busquedas"] = len(bs)
        r["t_bus"], r["t_bus_desv"] = prom_desv([b["tiempo_ms"] for b in bs])
        r["t_bus_eem"] = r["t_bus_desv"] / math.sqrt(len(bs))
        r["io_bus"], r["io_bus_desv"] = prom_desv([b["ios"] for b in bs])
        r["res_bus"], _ = prom_desv([b["resultados"] for b in bs])
        r["ms_por_io"] = statistics.fmean([b["tiempo_ms"] / b["ios"] for b in bs if b["ios"] > 0])
        resumen[k] = r
    return resumen


# ----------------------------------------------------------------------------
# Utilidades de gráficos
# ----------------------------------------------------------------------------


def eje_n(ax, exponentes: list[int], cada: int = 1) -> None:
    """Configura el eje x en escala log2 con marcas 2^E.

    Parámetros:
        ax: eje de matplotlib.
        exponentes: exponentes presentes en los datos.
        cada: rotula uno de cada `cada` exponentes (2 para paneles angostos).
    """
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_locator(FixedLocator([2**e for e in exponentes]))
    ax.xaxis.set_minor_locator(NullLocator())

    def rotulo(x, _):
        """Rótulo de la marca x: 2^E si E corresponde según `cada`, o vacío."""
        e = int(round(math.log2(x)))
        return f"$2^{{{e}}}$" if (e - exponentes[0]) % cada == 0 else ""

    ax.xaxis.set_major_formatter(FuncFormatter(rotulo))
    ax.set_xlabel("N (pares insertados)")


def leyenda_abajo(fig, ax, ncol: int = 3) -> None:
    """Pone una sola leyenda para toda la figura, debajo de los paneles.

    Parámetros:
        fig: figura de matplotlib.
        ax: eje del que se toman las entradas de la leyenda.
        ncol: columnas de la leyenda.
    """
    handles, labels = ax.get_legend_handles_labels()
    fig.tight_layout(rect=(0, 0.09, 1, 1), w_pad=1.2)
    fig.legend(handles, labels, loc="lower center", ncol=ncol, frameon=False, bbox_to_anchor=(0.5, 0.0),
               columnspacing=1.6, handlelength=2.2)


def serie(resumen: dict, tipo: str, campo: str, exponentes: list[int]) -> tuple[list, list]:
    """Extrae una serie (N, valor) de un tipo de árbol.

    Parámetros:
        resumen: salida de agregar() con clave "exponente".
        tipo: B, BE4 o BE126.
        campo: nombre del valor a extraer.
        exponentes: exponentes a incluir.
    Retorna:
        (lista de N, lista de valores) solo para los exponentes disponibles.
    """
    xs, ys = [], []
    for e in exponentes:
        if (tipo, e) in resumen:
            xs.append(2**e)
            ys.append(resumen[(tipo, e)][campo])
    return xs, ys


def dibujar(ax, resumen, campo, exponentes, desv=None, escala=1.0, tipos=ARBOLES):
    """Dibuja una línea por árbol, con barras de error opcionales.

    Parámetros:
        ax: eje de matplotlib.
        resumen: salida de agregar().
        campo: valor a graficar.
        exponentes: exponentes del eje x.
        desv: nombre del campo con la desviación para las barras (o None). Si cada valor
            es un par [abajo, arriba], las barras son asimétricas (por ejemplo, mínimo–máximo).
        escala: factor que multiplica los valores (por ejemplo, para cambiar unidades).
        tipos: árboles a incluir.
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


FORMATO = "svg"  # formato de las figuras; se cambia con --formato


def guardar(fig, salida: str, nombre: str) -> None:
    """Guarda una figura (en FORMATO) y la cierra.

    Parámetros:
        fig: figura de matplotlib.
        salida: carpeta de salida.
        nombre: nombre del archivo sin extensión.
    """
    ruta = os.path.join(salida, f"{nombre}.{FORMATO}")
    fig.savefig(ruta, dpi=150)
    plt.close(fig)
    print(f"  {ruta}")


def escribir_tabla(ruta: str, encabezado: list[str], filas: list[list[str]]) -> None:
    """Escribe una tabla ya formateada (texto) para que el informe la lea.

    Parámetros:
        ruta: archivo CSV de salida.
        encabezado: nombres de las columnas.
        filas: filas con los valores ya convertidos a texto.
    """
    with open(ruta, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(encabezado)
        w.writerows(filas)
    print(f"  {ruta}")


def coma_decimal(ax, eje: str = "y") -> None:
    """Hace que las marcas de un eje lineal usen coma decimal (0,5 en vez de 0.5).

    Parámetros:
        ax: eje de matplotlib.
        eje: "x", "y" o "xy".
    """
    formato = FuncFormatter(lambda v, _: f"{v:.10g}".replace(".", ","))
    if "x" in eje:
        ax.xaxis.set_major_formatter(formato)
    if "y" in eje:
        ax.yaxis.set_major_formatter(formato)


# ----------------------------------------------------------------------------
# Experimento principal
# ----------------------------------------------------------------------------


def graficos_principal(resultados: str, salida: str) -> dict | None:
    """Genera las figuras y tablas del experimento principal.

    Parámetros:
        resultados: carpeta con principal.csv y principal_busquedas.csv.
        salida: carpeta de salida.
    Retorna:
        resumen agregado (o None si no están los CSV).
    """
    ruta = os.path.join(resultados, "principal.csv")
    if not os.path.exists(ruta):
        print("(no hay principal.csv: se omiten los gráficos del experimento principal)")
        return None
    res = agregar(leer_csv(ruta), leer_csv(os.path.join(resultados, "principal_busquedas.csv")), "exponente")
    exps = sorted({e for (_, e) in res})

    # Creación: tiempo total y por inserción.
    fig, (a, b) = plt.subplots(1, 2, figsize=(7.2, 2.75))
    dibujar(a, res, "t_ins", exps, desv="t_ins_err")
    a.set_yscale("log")
    a.set_ylabel("tiempo de creación (s)")
    a.set_title("(a) Tiempo de creación")
    for t in ARBOLES:
        xs, ys = serie(res, t, "t_ins", exps)
        b.plot(xs, [y / x * 1e6 for x, y in zip(xs, ys)], color=COLORES[t], marker=MARCADORES[t], label=ETIQUETAS[t])
    b.set_ylabel("µs por inserción")
    b.set_title("(b) Tiempo por par insertado")
    b.set_ylim(bottom=0)
    coma_decimal(b)
    for ax in (a, b):
        eje_n(ax, exps)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "creacion_tiempo")

    # Creación: I/Os totales, por inserción y por inserción sin contar la raíz.
    fig, (a, b, c) = plt.subplots(1, 3, figsize=(7.2, 2.75))
    dibujar(a, res, "ios_insercion", exps)
    a.set_yscale("log")
    a.set_ylabel("I/Os (lecturas + escrituras)")
    a.set_title("(a) I/Os totales")
    for t in ARBOLES:
        xs, ys = serie(res, t, "ios_insercion", exps)
        b.plot(xs, [y / x for x, y in zip(xs, ys)], color=COLORES[t], marker=MARCADORES[t], label=ETIQUETAS[t])
        raiz = 1 if t == "B" else 2  # B: lectura de la raíz; Bε: lectura y escritura de la raíz
        c.plot(xs, [y / x - raiz for x, y in zip(xs, ys)], color=COLORES[t], marker=MARCADORES[t], label=ETIQUETAS[t])
    modelo_linea = None
    for t in ("BE4", "BE126"):
        xs, _ = serie(res, t, "lotes", exps)
        # 2 I/Os (leer y escribir el hijo) por cada lote bajado, sin contar los splits.
        modelo = [2 * res[(t, e)]["lotes"] / res[(t, e)]["N"] for e in exps if (t, e) in res]
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

    # Búsquedas: tiempo, I/Os y I/Os relativas al árbol B.
    fig, (a, b, c) = plt.subplots(1, 3, figsize=(7.2, 2.75))
    dibujar(a, res, "t_bus", exps, desv="t_bus_desv")
    a.set_yscale("log")
    a.set_ylabel("tiempo por búsqueda (ms)")
    a.set_title("(a) Tiempo promedio")
    dibujar(b, res, "io_bus", exps, desv="io_bus_desv")
    b.set_yscale("log")
    b.set_ylabel("I/Os por búsqueda")
    b.set_title("(b) I/Os promedio")
    for t in ARBOLES:
        xs, ys = serie(res, t, "io_bus", exps)
        _, base = serie(res, "B", "io_bus", exps)
        c.plot(xs, [y / bb for y, bb in zip(ys, base)], color=COLORES[t], marker=MARCADORES[t], label=ETIQUETAS[t])
    c.set_ylabel("I/Os / I/Os del árbol B")
    c.set_title("(c) Relativas al árbol B")
    coma_decimal(c)
    for ax in (a, b, c):
        eje_n(ax, exps, cada=2)
    leyenda_abajo(fig, a)
    guardar(fig, salida, "busqueda")

    # Análisis: modelo de I/Os de búsqueda p·nodos + altura (p = fracción del espacio de llaves).
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

    # Tabla completa.
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

    # Tabla de estructura de los Bε y del árbol B para el N mayor.
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

    print("\nResumen del experimento principal (creación: mediana ± desv.; búsqueda: promedios):")
    for e in exps:
        for t in ARBOLES:
            if (t, e) in res:
                r = res[(t, e)]
                print(f"  2^{e:<2} {t:<5} ins {r['t_ins']:8.3f}±{r['t_ins_desv']:.3f} s  I/O/par "
                      f"{r['ios_insercion'] / r['N']:.3f}  nodos {r['nodos']:>7}  h {r['altura']:>2}  "
                      f"busq {r['t_bus']:8.2f} ms  I/O {r['io_bus']:7.1f}  res {r['res_bus']:9.1f}  "
                      f"({r['repeticiones']} rep, {r['busquedas']} búsq)")
    return res


def grafico_consulta(resultados: str, salida: str, exponentes: list[int], tabla: list[int]) -> None:
    """Dibuja la misma búsqueda por rango (consulta 0) para varios N y la resume en una tabla.

    Parámetros:
        resultados: carpeta con consulta0_N{E}.csv y consultas_principal.csv.
        salida: carpeta de salida.
        exponentes: exponentes de N que se dibujan (uno por panel), en orden.
        tabla: exponentes de N que se incluyen en la tabla resumen, en orden.
    """
    disponibles = [e for e in exponentes if os.path.exists(os.path.join(resultados, f"consulta0_N{e}.csv"))]
    if len(disponibles) < 2:
        print("(faltan archivos consulta0_N*.csv: se omite el gráfico de la consulta)")
        return
    consultas = leer_csv(os.path.join(resultados, "consultas_principal.csv"))
    l, u = consultas[0]["l"], consultas[0]["u"]
    en_tabla = [e for e in tabla if os.path.exists(os.path.join(resultados, f"consulta0_N{e}.csv"))]
    datos = {e: leer_csv(os.path.join(resultados, f"consulta0_N{e}.csv")) for e in sorted(set(disponibles + en_tabla))}
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

    # Comparación numérica: el resultado de un N menor está contenido en el de N mayor.
    filas = []
    mayor = datos[max(datos)]
    conj_mayor = {(p["llave"], p["valor"]) for p in mayor}
    for e in en_tabla:
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
    print(f"\nConsulta 0: [{l}, {u}] = {inicio:%Y-%m-%d %H:%M} a {fin:%Y-%m-%d %H:%M} ({NOMBRE_ZONA})")
    for f in filas:
        print("  ", f)


# ----------------------------------------------------------------------------
# Experimento bonus de α
# ----------------------------------------------------------------------------


def graficos_alpha(resultados: str, salida: str, tipo: str) -> dict | None:
    """Genera la figura y la tabla del experimento de α para un árbol Bε.

    Parámetros:
        resultados: carpeta con alpha_{tipo}.csv y alpha_{tipo}_busquedas.csv.
        salida: carpeta de salida.
        tipo: BE4 o BE126.
    Retorna:
        resumen agregado por α (o None si no están los CSV).
    """
    ruta = os.path.join(resultados, f"alpha_{tipo}.csv")
    if not os.path.exists(ruta):
        print(f"(no hay alpha_{tipo}.csv: se omite)")
        return None
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
    ruta_elegido = os.path.join(resultados, f"alpha_{tipo}_elegido.csv")
    elegido = leer_csv(ruta_elegido)[0]["alpha"] if os.path.exists(ruta_elegido) else None
    for ax in (a, b, c, d):
        ax.set_xlabel("α")
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_locator(FixedLocator([0, 0.2, 0.4, 0.6, 0.8, 1.0]))
        if elegido is not None:
            ax.axvline(elegido, color="gray", linestyle="--", linewidth=0.8)
        coma_decimal(ax, "xy")
    for ax in (b, d):
        # Escala de ±5 % alrededor de los datos, para que variaciones de 1 % no parezcan grandes.
        ys = ax.get_lines()[0].get_ydata()
        ax.set_ylim(min(ys) * 0.95, max(ys) * 1.05)
    titulo = f"{ETIQUETAS[tipo]}, N = $2^{{{int(math.log2(n))}}}$"
    if elegido is not None:
        titulo += f" (línea punteada: α elegido = {elegido:g})".replace(".", ",")
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
    print(f"\nα ({tipo}, N = {n}):")
    for f in filas:
        print("  ", f)
    return res


def main() -> int:
    """Punto de entrada: genera todo lo que permitan los CSV disponibles.

    Retorna:
        0 al terminar.
    """
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--resultados", default="resultados", help="carpeta con los CSV del programa C++")
    p.add_argument("--salida", default="informe/figuras", help="carpeta donde se guardan figuras y tablas")
    p.add_argument("--consulta", default="16,26", help="exponentes de N que se dibujan para la misma búsqueda")
    p.add_argument("--consulta-tabla", default="16,20,26", help="exponentes de N de la tabla de la misma búsqueda")
    p.add_argument("--formato", default="svg", choices=["svg", "png", "pdf"], help="formato de las figuras")
    args = p.parse_args()
    global FORMATO
    FORMATO = args.formato
    os.makedirs(args.salida, exist_ok=True)
    print("Generando figuras y tablas:")
    fig_consulta = [int(x) for x in args.consulta.split(",") if x]
    tabla_consulta = [int(x) for x in args.consulta_tabla.split(",") if x]
    res = graficos_principal(args.resultados, args.salida)
    grafico_consulta(args.resultados, args.salida, fig_consulta, tabla_consulta)
    res_alpha = {}
    for tipo in ("BE126", "BE4"):
        r = graficos_alpha(args.resultados, args.salida, tipo)
        if r is not None:
            res_alpha[tipo] = r
    if res is None or len(res_alpha) < 2:
        print("Faltan resultados del experimento principal o del bonus: no se genera valores.json", file=sys.stderr)
        return 1
    generar_valores(args.resultados, args.salida, res, res_alpha, fig_consulta, tabla_consulta)
    return 0


if __name__ == "__main__":
    sys.exit(main())
