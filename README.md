# Tarea 2 CC4102: árboles B y Bε en memoria externa

Implementación en C++17 de un árbol B (b = 340) y de dos árboles Bε
(c = 4, l_max = 506, ε ≈ 0,25; y c = 126, l_max = 384, ε ≈ 0,75). Todos usan
nodos de 4096 bytes. Los árboles se construyen sobre un disco simulado en RAM
que cuenta las I/Os, después se guardan en un archivo binario y las búsquedas
por rango leen los nodos desde ese archivo.

**El repositorio contiene solo código.** Todo lo que usa el informe (resultados,
entorno de la máquina, α elegido, figuras, tablas y los números del texto) se
genera en la máquina que ejecuta los experimentos. El informe describe siempre la
corrida que se hizo en esa máquina.

El repositorio tiene dos partes independientes:

- **C++** (`src/`, `tests/`): árboles, disco simulado, experimentos. Genera CSV en `resultados/`.
- **Python** (`graficos/`): solo lee esos CSV y genera figuras, tablas y `valores.json` en `informe/figuras/`.

## Inicio rápido (Fedora)

```sh
sudo dnf install gcc-c++ make python3 curl tar xz   # dependencias
git clone git@github.com:blu2718-uni/CC4102-Tarea-2.git
cd CC4102-Tarea-2
make descargar-datos     # data/datos.bin (512 MiB), verifica el sha256
make todo                # pruebas, entorno, experimentos, gráficos e informe
```

El resultado es `informe/informe.pdf`. Antes de `make todo`, ver
[Condiciones para medir](#condiciones-para-medir).

## Estructura

```
src/
  par.hpp           par llave-valor (int32 + float32, 8 bytes)
  arbol_comun.hpp   inserción ordenada, mezcla de lotes, estadísticas
  disco.hpp         disco simulado: arreglo de bloques en RAM que cuenta lecturas/escrituras
  archivo.hpp       escritura de archivos de bloques y lectura con pread + O_DIRECT
  medicion.hpp      tiempo de CPU e indicadores de interferencia de la máquina
  datos.hpp         lectura secuencial de datos.bin
  arbol_b.hpp       árbol B: split, inserción y búsqueda por rango
  arbol_be.hpp      árbol Bε: split de hoja/interno, flush con α, inserción y búsqueda
  main.cpp          programa principal (experimentos, elección de α, latencia, consultas)
tests/pruebas.cpp   pruebas de correctitud
scripts/entorno.sh  registro del entorno de la máquina (SO, CPU, cachés, RAM, disco)
graficos/
  graficar.py       figuras y tablas del informe (matplotlib)
  valores.py        números y afirmaciones del texto del informe (valores.json)
  requirements.txt  versiones fijas de las dependencias de Python
informe/informe.typ fuente del informe (Typst)
```

Se generan (y git los ignora): `data/` (datos.bin), `build/`, `arboles/`
(archivos temporales de los árboles), `resultados/`, `informe/figuras/`,
`informe/informe.pdf`, `.venv/` y `.herramientas/` (Typst).

## Requisitos

| Para | Necesita |
|---|---|
| Programa C++ | Linux x86-64, `g++` con C++17, `make` |
| Gráficos | Python ≥ 3.10 con `venv` (en Fedora viene incluido) |
| Informe | Typst 0.15.1; si no está en el PATH, `make informe` descarga ese binario en `.herramientas/` y verifica su sha256 |

Memoria y disco: el experimento con N = 2^26 necesita unos 1,3 GiB de RAM libre
(el árbol B ocupa ~1,1 GiB mientras se construye) y unos 1,2 GiB libres en disco
para el archivo temporal del árbol. `datos.bin` ocupa 512 MiB.

### Disco y O_DIRECT

Las búsquedas leen los nodos con `O_DIRECT`, para medir accesos reales al disco y
no a la caché del sistema operativo. La carpeta `arboles/` debe estar en un disco:

- **Btrfs (Fedora por defecto):** Fedora monta Btrfs con compresión, y Btrfs no
  lee con `O_DIRECT` los bloques comprimidos. El programa crea los archivos de los
  árboles con el atributo NOCOW (como `chattr +C`), que desactiva la compresión, y
  verifica con FIEMAP que ningún bloque quedó comprimido. No hay que hacer nada.
- **No usar tmpfs** (por ejemplo `/tmp` en Fedora): el archivo estaría en RAM.

Si la lectura directa no se puede usar, el programa lo avisa en la terminal y lo
registra (columnas `odirect = 0` y `motivo_sin_odirect`), y el informe lo menciona.
`make entorno` mide además la latencia de lectura del disco en esa carpeta.

## Condiciones para medir

Los tiempos dependen de la máquina y de su carga; las I/Os, los tamaños y las
alturas no. Para que los tiempos sean confiables:

- Cerrar todo lo demás (navegador, IDE, sincronización de archivos) y no usar el
  equipo mientras corre `make todo`.
- Notebook: enchufado.
- Opcional, con sudo: `sudo dnf install kernel-tools` y
  `sudo cpupower frequency-set -g performance`.
- No correr dos experimentos a la vez: `make todo` los ejecuta en orden, uno tras otro.

El programa registra, para cada construcción y sus búsquedas, el tiempo de CPU,
los cambios de contexto involuntarios, los fallos de página mayores, la presión de
CPU y memoria del kernel (PSI) y el tiempo robado por el hipervisor. El informe
marca como sospechosas las construcciones en que el reloj superó claramente al
tiempo de CPU. `make entorno` avisa si la carga es alta, si queda poca RAM, si la
máquina es virtual o si el gobernador de CPU es `powersave`.

## Pasos de `make todo`

`make todo` ejecuta, en este orden (cada paso se puede correr por separado):

| Paso | Qué hace | Escribe en `resultados/` |
|---|---|---|
| `make` | compila `build/tarea2` y `build/pruebas` | |
| `make test` | pruebas de correctitud | `pruebas.csv` |
| `make entorno` | registra la máquina, mide la latencia del disco y resume `datos.bin` | `entorno.csv`, `latencia.csv`, `datos.csv` |
| `make alpha` | bonus: N = 2^24, 8 valores de α, 20 búsquedas, ambos Bε; elige α | `alpha_*.csv`, `alpha_*_elegido.csv` |
| `make principal` | N = 2^15..2^26, 3 árboles, 50 búsquedas, con el α elegido | `principal*.csv`, `consulta*.csv` |
| `make graficos` | crea `.venv/` y genera figuras, tablas y `valores.json` | (en `informe/figuras/`) |
| `make informe` | compila `informe/informe.pdf` | |

`REPS=N` cambia la cantidad de repeticiones de cada construcción (por defecto 5),
por ejemplo `make todo REPS=3`. Otras opciones: `make test-san` (pruebas con
AddressSanitizer y UBSan; usa bastante más memoria) y `make clean`.

Las pruebas comparan contra fuerza bruta e invariantes (orden, rangos, ocupación,
altura y multiconjunto de pares) y cuentan I/Os exactas en casos pequeños hechos
a mano. Deben terminar con `... verificaciones, 0 fallas`.

En los experimentos, cada búsqueda se compara con una búsqueda por fuerza bruta
sobre los primeros N pares; si alguna no coincide, el programa termina con código
2. El orden de los árboles (y de los α) rota en cada repetición. Los archivos de
los árboles se escriben en `arboles/` y se borran después de buscar.

### Elección de α

`make alpha` elige α para cada Bε con una regla basada en I/Os (que no dependen de
la máquina): el menor α cuyas I/Os de carga estén a lo más 0,5 % sobre el mínimo
entre los α probados y cuyas I/Os de búsqueda estén a lo más 3 % sobre el mínimo.
El resultado queda en `resultados/alpha_<tipo>_elegido.csv`, y `make principal` lo
usa automáticamente. Si no existe, se usa α = 0,9. Se puede forzar con
`./build/tarea2 principal --alpha-be4 A --alpha-be126 A`.

## Uso directo del programa

`./build/tarea2 ayuda` muestra todas las opciones.

```sh
./build/tarea2 principal [--nmin 15] [--nmax 26] [--busquedas 50] [--repeticiones 1]
                         [--tipos B,BE4,BE126] [--alpha-be4 A] [--alpha-be126 A]
./build/tarea2 alpha --arbol BE126|BE4 [--n 24] [--alphas 0.1,0.25,...] [--repeticiones 1]
./build/tarea2 elegir-alpha --arbol BE126|BE4     # vuelve a elegir α desde resultados/
./build/tarea2 latencia [--mib 512] [--lecturas 3000]
./build/tarea2 datos
```

Opciones comunes: `--datos RUTA` (por defecto `data/datos.bin`), `--resultados DIR`,
`--arboles DIR`, `--semilla S`, `--sin-odirect` y `--conservar-arboles`.

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

## Archivos de resultados

| Archivo | Contenido |
|---|---|
| `principal.csv` | una fila por árbol construido (tipo, α, N, repetición): tiempo de inserción (reloj y CPU), lecturas/escrituras simuladas, nodos, bytes, altura, ocupación, contadores de flush/splits, promedio y desviación del tiempo y de las I/Os de búsqueda, indicadores de interferencia y si se leyó directo del disco |
| `principal_busquedas.csv` | una fila por búsqueda: rango [l, u], pares encontrados, I/Os, tiempo (ms) y si coincidió con la fuerza bruta |
| `consultas_principal.csv` | los 50 rangos usados (los mismos para todo N) |
| `consulta0_N{16,20,26}.csv` | los pares que devuelve la búsqueda 0 con N = 2^16, 2^20 y 2^26 |
| `alpha_{BE126,BE4}.csv`, `alpha_*_busquedas.csv`, `alpha_*_consultas.csv` | lo mismo para el experimento de α |
| `alpha_{BE126,BE4}_elegido.csv` | el α elegido y los valores con que se eligió |
| `entorno.csv`, `latencia.csv`, `datos.csv`, `pruebas.csv` | máquina, latencia del disco, resumen de `datos.bin` y resultado de las pruebas |

El tiempo de inserción no incluye la lectura de `datos.bin`, que se informa
aparte (`t_lectura_datos_s`). Las I/Os de inserción son las del disco simulado y
las de búsqueda son bloques leídos del archivo.

## Gráficos e informe a mano

```sh
python3 -m venv .venv
.venv/bin/pip install -r graficos/requirements.txt
.venv/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras
typst compile informe/informe.typ informe/informe.pdf    # o: make informe
```

`graficar.py --formato png` genera PNG para mirar las figuras rápido. El informe
no tiene números de resultados escritos a mano: los lee de
`informe/figuras/valores.json` y de las tablas de esa carpeta.
