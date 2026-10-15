# Tarea 2 CC4102: árboles B y Bε en memoria externa

Implementación en C++17 de un árbol B (b = 340) y de dos árboles Bε
(c = 4, l_max = 506, ε ≈ 0,25; y c = 126, l_max = 384, ε ≈ 0,75). Todos usan
nodos de 4096 bytes. Los árboles se construyen sobre un disco simulado en RAM
que cuenta las I/Os, después se guardan en un archivo binario y las búsquedas
por rango leen los nodos desde ese archivo.

El repositorio tiene dos partes independientes:

- **C++** (`src/`, `tests/`): árboles, disco simulado, experimentos. Genera CSV en `resultados/`.
- **Python** (`graficos/`): solo lee esos CSV y genera figuras y tablas en `informe/figuras/`.

## Estructura

```
src/
  par.hpp           par llave-valor (int32 + float32, 8 bytes)
  arbol_comun.hpp   inserción ordenada, mezcla de lotes, estadísticas
  disco.hpp         disco simulado: arreglo de bloques en RAM que cuenta lecturas/escrituras
  archivo.hpp       lectura de un bloque del archivo binario (pread + O_DIRECT)
  datos.hpp         lectura secuencial de datos.bin
  arbol_b.hpp       árbol B: split, inserción y búsqueda por rango
  arbol_be.hpp      árbol Bε: split de hoja/interno, flush con α, inserción y búsqueda
  main.cpp          programa principal (experimentos, consultas)
tests/pruebas.cpp   pruebas de correctitud
graficos/
  graficar.py       figuras y tablas del informe (matplotlib)
  requirements.txt  versiones fijas de las dependencias de Python
resultados/         CSV de los experimentos usados en el informe
informe/            informe.typ (fuente), informe.pdf, figuras/ (generadas)
data/               datos.bin (NO se incluye; ver abajo)
```

## Requisitos

| Para | Necesita | Probado con |
|---|---|---|
| Programa C++ | Linux x86-64, `g++` con C++17, `make` | Debian forky/sid, kernel 7.1.8, g++ 15.3.0 |
| Gráficos | Python ≥ 3.10 con `venv` (o [`uv`](https://docs.astral.sh/uv/)) | Python 3.14.4, matplotlib 3.11.2 |
| Informe | [Typst](https://github.com/typst/typst) ≥ 0.13 | Typst 0.15.1 |

El experimento principal con N = 2^26 necesita unos 1,3 GiB de RAM libre (el
árbol B ocupa ~1,1 GiB mientras se construye) y unos 1,2 GiB libres en disco
para el archivo temporal del árbol. Las búsquedas abren ese archivo con
`O_DIRECT`, así que la carpeta `arboles/` debe estar en un sistema de archivos
que lo soporte (ext4, xfs, etc.; tmpfs no lo soporta). Si no se puede usar, el
programa lee de forma normal y lo indica con la columna `odirect = 0`.

En Debian/Ubuntu:

```sh
sudo apt install g++ make python3 python3-venv
```

## 1. Datos

`datos.bin` no se incluye en la entrega. Para descargarlo y verificarlo:

```sh
mkdir -p data
curl -L -o data/datos.bin \
  https://github.com/CristobalPuentes27/tarea2-CC4102-2026-2/releases/download/datos/datos.bin
sha256sum data/datos.bin
# 63e5f8da1cc7bf640f6b768dc5565d32fd1d601c65a68869ef17472b21d27a67  data/datos.bin
```

## 2. Compilar y probar (C++)

```sh
make            # compila build/tarea2 y build/pruebas
make test       # ejecuta las pruebas (≈ 45 s; usa data/datos.bin si está)
make test-san   # opcional: las mismas pruebas con AddressSanitizer y UBSan
```

Las pruebas comparan contra fuerza bruta e invariantes (orden, rangos, ocupación,
altura y multiconjunto de pares) y cuentan I/Os exactas en casos pequeños hechos
a mano. Deben terminar con `... verificaciones, 0 fallas`.

## 3. Experimentos (C++)

```sh
make alpha       # bonus: N = 2^24, 8 valores de α, 20 búsquedas, 3 repeticiones, ambos Bε (≈ 10 min)
make principal   # N = 2^15..2^26, 3 árboles, 50 búsquedas, 3 repeticiones (≈ 25 min)
```

Los tiempos entre paréntesis son los de la máquina del informe (1 vCPU a 3,7 GHz).

`make` acepta `REPS=1` para hacer una sola repetición. Ambos comandos escriben en
`resultados/` y muestran el avance en la terminal. Cada búsqueda se compara con
una búsqueda por fuerza bruta sobre los primeros N pares; si alguna no coincide,
el programa termina con código 2. Los archivos de los árboles se escriben en
`arboles/` y se borran después de buscar.

El programa también se puede llamar directamente (`./build/tarea2 ayuda` muestra
todas las opciones):

```sh
./build/tarea2 principal [--nmin 15] [--nmax 26] [--busquedas 50] [--repeticiones 1]
                         [--tipos B,BE4,BE126] [--alpha-be4 A] [--alpha-be126 A]
./build/tarea2 alpha --arbol BE126|BE4 [--n 24] [--alphas 0.1,0.25,...] [--repeticiones 1]
```

Opciones comunes: `--datos RUTA` (por defecto `data/datos.bin`), `--resultados DIR`,
`--arboles DIR`, `--semilla S`, `--sin-odirect` y `--conservar-arboles`.

Los α por defecto de los árboles Bε son los que se eligieron con el experimento
bonus (ver informe): α = 0,9 para c = 4 y para c = 126.

### Una búsqueda puntual

```sh
# Construye un árbol con los primeros 2^20 pares, lo guarda y busca [l, u]:
./build/tarea2 consulta --tipo BE126 --n 20 --l 1597289407 --u 1597894207 --salida semana.csv

# Busca en un árbol ya guardado (por ejemplo, uno creado con --conservar-arboles):
./build/tarea2 consulta --tipo B --n 20 --l 1597289407 --u 1597894207 --conservar-arboles > /dev/null
./build/tarea2 buscar --tipo B --archivo arboles/B_N20.bin --l 1600000000 --u 1600604800
```

El resultado es un CSV `llave,valor` (tiempo Unix y temperatura), ordenado por
llave. El resumen (pares, I/Os y tiempo) se escribe en stderr.

### Archivos de resultados

| Archivo | Contenido |
|---|---|
| `principal.csv` | una fila por árbol construido (tipo, α, N, repetición): tiempo de inserción, lecturas/escrituras simuladas, nodos, bytes, altura, ocupación, contadores de flush/splits, promedio y desviación del tiempo y de las I/Os de búsqueda |
| `principal_busquedas.csv` | una fila por búsqueda: rango [l, u], pares encontrados, I/Os, tiempo (ms) y si coincidió con la fuerza bruta |
| `consultas_principal.csv` | los 50 rangos usados (los mismos para todo N) |
| `consulta0_N{16,20,26}.csv` | los pares que devuelve la búsqueda 0 con N = 2^16, 2^20 y 2^26 |
| `alpha_{BE126,BE4}.csv`, `alpha_*_busquedas.csv`, `alpha_*_consultas.csv` | lo mismo para el experimento de α |

El tiempo de inserción no incluye la lectura de `datos.bin`, que se informa
aparte (`t_lectura_datos_s`). Las I/Os de inserción son las del disco simulado y
las de búsqueda son bloques leídos del archivo.

## 4. Gráficos (Python)

```sh
make graficos
```

Esto crea `.venv/` con las versiones de `graficos/requirements.txt` (con `python3 -m venv`,
o con `uv` si el Python del sistema no trae `ensurepip`) y ejecuta `graficos/graficar.py`,
que lee `resultados/` y escribe las figuras (SVG) y las tablas (CSV) que usa el informe en
`informe/figuras/`. Con `--formato png` genera PNG para mirarlas rápido. Para hacerlo a mano:

```sh
python3 -m venv .venv                      # o bien: uv venv .venv
.venv/bin/pip install -r graficos/requirements.txt   # o bien: uv pip install --python .venv/bin/python -r graficos/requirements.txt
.venv/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras
```

## 5. Informe

```sh
make informe                # usa `typst` del PATH
make informe TYPST=/ruta/a/typst
```

Typst se puede instalar desde sus [releases](https://github.com/typst/typst/releases)
(un binario sin dependencias), con `cargo install typst-cli` o con el gestor de
paquetes del sistema. El informe usa las figuras y tablas de `informe/figuras/`,
así que primero hay que ejecutar `make graficos`.

## Reproducir todo desde cero

```sh
make && make test
make alpha && make principal
make graficos && make informe
```
