# Tarea 2 CC4102: Árboles B y Bε en memoria externa

Implementación en C++17 de un árbol B ($b = 340$) y dos árboles $B^\varepsilon$ ($\varepsilon \approx 0.25$ con $c = 4, l_{\max} = 506$; y $\varepsilon \approx 0.75$ con $c = 126, l_{\max} = 384$), todos con bloques fijos de 4096 bytes. Los árboles se construyen sobre un arreglo de bloques en RAM que simula el disco y contabiliza las I/Os, luego se escriben secuencialmente a un archivo binario y las búsquedas por rango se ejecutan leyendo desde ese archivo en disco.

## Requisitos

- Linux x86-64, compilador `g++` con soporte C++17 y `make`.
- Python 3 con `venv` y `matplotlib` (se instala automáticamente en `.venv/` con `make graficos`).
- [Typst](https://typst.app/) (para compilar `informe/informe.typ` a `informe/informe.pdf`).

En Debian/Ubuntu:
```sh
sudo apt install g++ make python3 python3-venv curl
```

## Ejecución paso a paso

```sh
# 1. Descargar el dataset datos.bin (512 MiB) en data/datos.bin
make descargar-datos

# 2. Compilar el programa principal (build/tarea2) y las pruebas (build/pruebas)
make

# 3. Ejecutar las pruebas de correctitud
make test

# 4. Ejecutar el experimento bonus (N = 2^24, 8 valores de α, 20 búsquedas)
make alpha

# 5. Ejecutar el experimento principal (N = 2^15..2^26, 3 árboles, 50 búsquedas)
make principal

# 6. Generar los gráficos SVG y tablas CSV en informe/figuras/
make graficos

# 7. Compilar el informe en PDF (informe/informe.pdf)
make informe
```

También se puede ejecutar todo el flujo secuencialmente con `make todo`.

## Uso directo del ejecutable

```sh
./build/tarea2 principal [--nmin 15] [--nmax 26] [--busquedas 50] [--repeticiones 1] [--alpha 0.9]
./build/tarea2 alpha [--arbol BE126] [--n 24] [--alphas 0.1,0.25,0.4,0.5,0.6,0.75,0.9,0.99]
```

## Estructura del proyecto

```
src/
  par.hpp           Par llave-valor (int32 + float32, 8 bytes) y comparador por llave
  arbol_comun.hpp   Inserción ordenada, mezcla de lotes y estructuras de estadísticas
  disco.hpp         Disco simulado en RAM (std::vector de nodos con conteo de I/Os)
  archivo.hpp       Escritura secuencial de bloques y lectura desde archivo con pread/O_DIRECT
  datos.hpp         Lectura secuencial por bloques de datos.bin
  arbol_b.hpp       Árbol B (b = 340): split, inserción y búsqueda por rango
  arbol_be.hpp      Árbol Bε: split de hoja/interno, vaciado (flush) con α, inserción y búsqueda
  main.cpp          Programa principal (experimento principal y bonus de α)
tests/
  pruebas.cpp       Pruebas de correctitud e invariantes
graficos/
  graficar.py       Generación de gráficos SVG y tablas CSV desde resultados/
  requirements.txt  Dependencias de Python (matplotlib)
informe/
  informe.typ       Código fuente del informe en Typst
```
