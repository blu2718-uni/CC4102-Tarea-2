// Informe de la Tarea 2 (CC4102): árboles B y Bε en memoria externa.
// Compilar con: make informe (o typst compile informe/informe.typ informe/informe.pdf).
//
// Este archivo no tiene números de resultados escritos a mano. Todo lo que depende de la
// corrida (figuras, tablas, números del texto y las afirmaciones que dependen de los datos)
// se lee de figuras/, que genera graficos/graficar.py a partir de resultados/ en la máquina
// que ejecutó los experimentos (ver README: make todo).

#set document(title: "Tarea 2: Árboles B y Bε en memoria externa", author: "Julio Yáñez")
#set page(paper: "a4", margin: (x: 2.2cm, top: 2.1cm, bottom: 2.3cm), numbering: "1")
#set text(font: "New Computer Modern", size: 10.5pt, lang: "es")
#set par(justify: true, leading: 0.6em, spacing: 0.85em)
#set heading(numbering: "1.1.")
#show heading.where(level: 1): set block(above: 1.3em, below: 0.75em)
#show heading.where(level: 2): set block(above: 1.05em, below: 0.6em)
#show figure.caption: set text(size: 9pt)
#show figure: set block(breakable: false)
#set figure(gap: 0.6em)
#set math.equation(numbering: "(1)")
#show raw: set text(font: "DejaVu Sans Mono", size: 0.88em)
#set list(spacing: 0.65em)
#set enum(spacing: 0.65em)
// "I/O" e "I/Os" no se cortan al final de una línea.
#show regex("I/Os?"): it => box(it)

// ---------------------------------------------------------------------------
// Valores generados (graficos/valores.py) y utilidades
// ---------------------------------------------------------------------------

#let V = json("figuras/valores.json")
/// Texto ya formateado de un resultado (coma decimal, separador de miles).
#let val(k) = V.v.at(k)
/// Condición que depende de los datos (true/false).
#let cond(k) = V.b.at(k)
/// Lista de exponentes de N.
#let lst(k) = V.l.at(k)
/// $2^e$ a partir de un exponente (número o texto).
#let pot(e) = $2^#str(e)$
/// Lista de potencias en español: "2^17 y 2^25".
#let pots(xs) = {
  let p = xs.map(pot)
  if p.len() == 0 { [] } else if p.len() == 1 { p.at(0) } else { p.slice(0, -1).join([, ]) + [ y ] + p.last() }
}
#let Nmin = pot(val("n_min"))
#let Nmax = pot(val("n_max"))
#let Na = pot(val("alpha_n"))

#let Be = $B^epsilon$
#let nombres = (B: [B], BE4: [#Be c=4], BE126: [#Be c=126])
#let potencia(s) = { let e = s.split("^").at(1); $2^#e$ }

/// Tabla con filetes horizontales (estilo libro).
/// - encabezado: arreglo de celdas de encabezado (pueden tener saltos de línea con \).
/// - celdas: arreglo plano de celdas (puede incluir table.hline()).
/// - izquierda: cantidad de columnas iniciales alineadas a la izquierda; el resto va a la derecha.
/// - tam: tamaño de letra de la tabla.
#let tabla(encabezado, celdas, izquierda: 1, tam: 8.6pt) = {
  set text(size: tam, hyphenate: false)
  table(
    columns: encabezado.len(),
    align: (x, y) => (if x < izquierda { left } else { right }) + bottom,
    stroke: none,
    inset: (x: 4.2pt, y: 2.6pt),
    table.hline(stroke: 0.8pt),
    table.header(..encabezado),
    table.hline(stroke: 0.4pt),
    ..celdas,
    table.hline(stroke: 0.8pt),
  )
}

/// Filas de un CSV de figuras/, sin el encabezado.
#let filas(nombre) = csv("figuras/" + nombre + ".csv").slice(1)

// ---------------------------------------------------------------------------
// Título
// ---------------------------------------------------------------------------

#align(center)[
  #text(size: 17pt, weight: "bold")[Tarea 2: Árboles B y #Be en memoria externa]
  #v(0.15em)
  #text(size: 11pt)[CC4102 Diseño y Análisis de Algoritmos · Semestre 2026-2]
  #v(0.1em)
  Julio Yáñez · #datetime.today().display("[day]-[month]-[year]")
]
#v(0.4em)

= Introducción

En memoria externa, el costo de una operación se mide en transferencias de bloques entre el disco y la memoria principal (I/Os). El árbol B es la estructura clásica para un diccionario ordenado en este modelo: cada nodo ocupa un bloque con $Theta(B)$ llaves, por lo que insertar y buscar cuesta $Theta(log_B N)$ I/Os. El árbol #Be cambia ese equilibrio a favor de las inserciones: cada nodo interno usa solo del orden de $B^epsilon$ llaves divisorias y destina el resto del bloque a un _buffer_ de inserciones pendientes, que bajan a los hijos en lotes cuando el buffer se llena. Así, cada I/O de un vaciado mueve muchos pares a la vez y una inserción cuesta $O(log_B N \/ (epsilon B^(1-epsilon)))$ I/Os amortizadas, a cambio de una altura $O(log_B N \/ epsilon)$, que es lo que pagan las búsquedas.

En esta tarea se implementaron en C++ un árbol B ($b = 340$) y dos árboles #Be, todos con nodos de 4096 bytes: uno con $epsilon approx$ 0,25 ($c = 4$, $l_"max" = 506$) y otro con $epsilon approx$ 0,75 ($c = 126$, $l_"max" = 384$). Con ellos se indexaron los primeros $N in {#Nmin, ..., #Nmax}$ pares (tiempo Unix, temperatura) medidos en la estación Quinta Normal entre 2019 y 2025. Los árboles se construyen sobre un arreglo de bloques en RAM que simula el disco y cuenta cada lectura y escritura; después se guardan en un archivo binario y se hacen #val("busquedas_principal") búsquedas de rangos de una semana leyendo los nodos desde ese archivo. Se reportan el tiempo y las I/Os de la construcción, el tamaño y la altura de cada árbol, y el tiempo y las I/Os de las búsquedas. En el experimento bonus se estudia además el parámetro $alpha$ del vaciado con $N = #Na$.

La @sec:desarrollo describe las estructuras, los algoritmos y las decisiones de implementación que el enunciado deja abiertas. La @sec:resultados presenta el entorno, la metodología y los resultados; la @sec:analisis los analiza con el análisis amortizado de los vaciados y la altura en función de $epsilon$, y la @sec:conclusion concluye.

*Hipótesis.* Un bloque de 4096 bytes tiene espacio para $B = 512$ pares de 8 bytes (el nodo del árbol B guarda 340, porque también almacena los índices de los hijos), y $B^"0,25" approx "4,8"$ y $B^"0,75" approx 108$, que corresponden aproximadamente a los 5 y 127 hijos de las dos configuraciones.

+ _Inserción._ El árbol B lee el camino desde la raíz y escribe la hoja: unas $h + 1$ I/Os por inserción, con una altura $h$ de 2 a 4 para estos $N$. En un árbol #Be cada inserción lee y escribe la raíz (2 I/Os) y los vaciados agregan poco, porque cada lote mueve muchos pares: con $epsilon approx$ 0,25 el hijo más poblado recibe al menos $ceil(506 \/ 5) = 102$ pares por vaciado, pero con $epsilon approx$ 0,75 solo $ceil(384 \/ 127) = 4$. Se espera entonces, en I/Os por inserción, #Be (0,25) < #Be (0,75) < B, con el primero muy cerca de 2. Como en la simulación cada I/O copia un bloque de 4 KiB, el tiempo de creación debería seguir el mismo orden.
+ _Altura y búsqueda._ La altura de un #Be crece como $1\/epsilon$: el árbol con $epsilon approx$ 0,25 debería tener unas 3 veces la altura del de 0,75, que a su vez debería ser casi tan bajo como el árbol B. En búsquedas que tocan pocos nodos eso ordena los árboles como B $approx$ #Be (0,75) < #Be (0,25). Pero un rango de una semana contiene $approx$ 0,29 % de los datos (casi 200 000 pares con $N = 2^26$, cientos de bloques), así que para $N$ grande debería dominar el número de bloques con resultados y los tres árboles deberían acercarse.
+ _Tamaño._ Las hojas de los #Be tienen espacio para más pares que las del árbol B (506 y 384 contra 340), así que deberían ocupar menos bloques, en especial con $c = 4$.

= Desarrollo <sec:desarrollo>

== Organización del código

Los árboles y los experimentos están en C++17 (`src/`). El código de los gráficos está aparte, en Python (`graficos/`), y solo lee los CSV que produce el programa. El repositorio no contiene resultados: todo lo que usa este informe (resultados, entorno, figuras, tablas y los números del texto) se genera en la máquina que ejecuta los experimentos, con `make todo`. Los árboles son plantillas parametrizadas por $b$ o por $(c, l_"max")$: el programa usa $b = 340$, $(4, 506)$ y $(126, 384)$, y las pruebas usan valores chicos como $b = 4$ o $(c, l_"max") = (4, 6)$, que generan árboles profundos con pocos datos y ejercitan todos los casos de split y vaciado. Cada estructura y función está documentada en el código con su propósito y sus parámetros de entrada y salida.

#figure(
  tabla(
    ([*Archivo*], [*Contenido*]),
    (
      [`par.hpp`, `arbol_comun.hpp`], [par llave-valor de 8 bytes (mismo formato que `datos.bin`), inserción ordenada y mezcla de lotes],
      [`disco.hpp`], [disco simulado en RAM: lecturas y escrituras de bloques con conteo de I/Os; escritura del árbol a archivo],
      [`archivo.hpp`], [escritura de archivos de bloques y lectura de un nodo con `pread` y `O_DIRECT`, verificando que la lectura vaya al disco],
      [`arbol_b.hpp`, `arbol_be.hpp`], [árbol B y árbol #Be: splits, vaciado, inserción y búsqueda por rango],
      [`medicion.hpp`], [tiempo de CPU e indicadores de interferencia de la máquina],
      [`datos.hpp`, `main.cpp`], [lectura de `datos.bin`; experimentos, elección de $alpha$, latencia del disco, consultas y CSV],
      [`tests/pruebas.cpp`], [pruebas de correctitud (invariantes, I/Os exactas, comparación con fuerza bruta)],
      [`scripts/entorno.sh`], [registro del entorno de la máquina (SO, CPU, cachés, RAM, disco, compilador)],
    ),
    izquierda: 2,
  ),
  caption: [Archivos del programa.],
) <tab:archivos>

== Nodos, disco simulado y archivo

Los nodos son `struct` con exactamente los campos del enunciado y en el mismo orden (`NodoB<340>`, `NodoBe<4,506>` y `NodoBe<126,384>`); un `static_assert` comprueba que cada uno mide 4096 bytes. Como no contienen punteros, un nodo se guarda y se lee copiando sus bytes.

El disco simulado es un arreglo de nodos en RAM con tres operaciones, y los algoritmos solo acceden a los nodos a través de ellas: `leer(i, nodo)` copia el bloque $i$ completo a una variable local y cuenta una lectura; `escribir(i, nodo)` copia la variable al bloque $i$ y cuenta una escritura; y `agregar(nodo)` la escribe al final, cuenta una escritura y retorna su posición. Como los algoritmos trabajan siempre sobre copias locales, igual que si hubieran traído el bloque del disco, los contadores son exactamente la cantidad de bloques transferidos. La raíz está siempre en el bloque 0.

El arreglo no es un `std::vector`. Al crecer, un vector reserva un arreglo del doble de tamaño y copia el anterior, así que con el árbol B de $N = 2^26$ (más de 1 GiB) habría necesitado por un momento el doble de memoria. En su lugar, los bloques se guardan en segmentos de 1024 nodos (4 MiB) reservados con `mmap`, que se agregan cuando hacen falta y se devuelven al sistema operativo al destruir el árbol. Para los algoritmos sigue siendo un arreglo indexado por posición.

Al terminar las inserciones, el árbol se escribe en un archivo binario, bloque por bloque y en orden (el nodo $i$ queda en el byte $i dot 4096$), se hace `fsync` y se le pide al sistema operativo que saque el archivo de su caché de páginas (`posix_fadvise`). Después se destruye el árbol en RAM. Las búsquedas abren el archivo con `O_DIRECT`: leer el nodo $i$ es un `pread` de 4096 bytes en la posición $i dot 4096$ hacia un buffer alineado, que va al dispositivo sin pasar por la caché del sistema operativo. Sin `O_DIRECT`, después de la primera búsqueda el archivo quedaría en esa caché y se mediría la velocidad de la RAM. Cada nodo leído es una I/O de búsqueda.

Hay dos casos en que `O_DIRECT` se acepta pero no lee del disco: si el archivo está en `tmpfs` (en RAM) y si está en Btrfs con compresión (la opción por defecto de Fedora), porque Btrfs lee los bloques comprimidos con caché. Por eso el archivo se crea con el atributo NOCOW (como `chattr +C`), que en Btrfs desactiva la compresión, y al abrirlo se verifica el tipo de sistema de archivos y, con `FIEMAP`, que ningún bloque del archivo quedó comprimido. Si algo falla, la columna `odirect` de los resultados queda en 0 con el motivo.

== Árbol B

La inserción sigue el esquema del enunciado, con división preventiva: antes de bajar a un hijo lleno ($k = b$), se lo divide en el par de la posición $b\/2 = 170$, que deja 169 pares a la izquierda y 170 a la derecha. $U_"der"$ se agrega al final, $U_"izq"$ reemplaza a $U$ y el padre, que siempre tiene espacio porque nunca se entra a un nodo lleno, recibe la mediana y se escribe. La inserción continúa en la mitad que corresponde, que ya está en memoria y no se vuelve a leer. Una raíz llena se divide igual: ambas mitades van al final del arreglo y la nueva raíz al bloque 0.

La búsqueda por rango recorre cada nodo en orden (hijo, par, hijo, …) y baja solo a los hijos cuyo rango intersecta $[l, u]$. Con $a$ el primer índice cuya llave es $>= l$ y $z$ el primero cuya llave es $> u$ (búsquedas binarias), visita los hijos $a, ..., z$ y reporta los pares $a, ..., z - 1$. Por eso el resultado queda ordenado por llave.

== Árbol #Be <sec:be>

*Inserción.* Cada inserción lee la raíz y la escribe con el par agregado a su buffer. Si la raíz es una hoja llena, se divide en la posición $l_"max"\/2$, como indica el enunciado, y se crea una raíz interna con el par en su buffer. Si la raíz es interna y su buffer está lleno, primero se vacía; si con eso llega a $c$ llaves, se divide y se crea una raíz nueva en el bloque 0.

*Vaciado de un nodo $V$.* Como el buffer está ordenado, el grupo de cada hijo es un tramo contiguo, cuyos límites se encuentran con una búsqueda binaria por llave divisoria. Con $f = ceil(k_"buffer" \/ (k_"llaves" + 1))$ se eligen los hijos que tienen al menos $ceil(alpha f)$ pares; los pares de los demás se quedan en $V$. Los hijos elegidos se procesan de derecha a izquierda: para cada uno se lee el hijo $U$ y se le entrega su lote de $m$ pares.
- Si $U$ es interno y $k_"buffer" (U) + m > l_"max"$, se vacía $U$ (recursivamente) hasta que el lote quepa. Después se mezcla el lote con el buffer de $U$ (mezcla de dos arreglos ordenados).
- Si $U$ es una hoja y el lote no cabe, se divide la hoja (ver abajo).

Durante su vaciado, $V$ se mantiene en memoria como una _copia de trabajo_ y se escribe una sola vez, al final, por quien lo llamó. Así, el vaciado no agrega I/Os propias (el nodo ya está en memoria) y cada lote cuesta 1 lectura y 1 escritura del hijo, más 1 escritura por cada nodo nuevo que aparezca al dividirlo (@tab:costos).

*Decisiones propias en los splits.*
- _Split de hoja al recibir un lote._ El enunciado divide la hoja en la posición $l_"max"\/2$ y después reparte el lote, pero si el lote cae entero a un lado esa mitad puede quedar con más de $l_"max"$ pares. En vez de eso, se mezclan los $s = k_"buffer" + m$ pares ($s <= 2 l_"max"$) y se cortan en la mediana: los primeros $ceil(s\/2)$ quedan en $U_"izq"$, en la posición de $U$, y el resto en $U_"der"$, al final del arreglo. La llave del último par de $U_"izq"$ sube como divisoria. Es lo mismo que dividir y repartir, pero garantiza que ambas hojas quedan con entre $l_"max"\/2$ y $l_"max"$ pares.
- _Split de nodo interno._ Si en un mismo vaciado se dividen varios hijos, $V$ recibe varias llaves nuevas. Como $V$ está en memoria, se aceptan todas y, antes de escribirlo, se divide en su llave mediana (la de índice $floor((K-1)\/2)$ de sus $K$ llaves, desde 0), repitiendo en cada mitad que siga con más de $c$ llaves; el buffer se reparte en $<= k_m$ y $> k_m$. Con $K = c + 1$ quedan $c\/2$ llaves en cada mitad (2 y 2 con $c = 4$; 63 y 63 con $c = 126$). Con $K = c$, el caso habitual de la raíz (que se divide en cuanto tiene $c$ o más llaves), resultan las $c\/2 - 1$ y $c\/2$ llaves del enunciado. Las mitades nuevas se agregan al final y sus llaves se insertan en el padre justo a la derecha del hijo dividido; como los hijos se procesan de derecha a izquierda, los índices de los que faltan no cambian.
- _Umbral exacto._ $alpha$ se guarda como el entero $alpha dot 10^6$ y $ceil(alpha f)$ se calcula con aritmética entera, porque en punto flotante, por ejemplo, $100 dot "0,07" = "7,000000000000001"$ y el techo daría 8 en vez de 7.

*Búsqueda.* En cada nodo se reportan los pares del buffer que están en $[l, u]$ (dos búsquedas binarias) y, si es interno, se baja a los hijos cuyo rango intersecta $[l, u]$, como en el árbol B. El resultado no queda ordenado, porque el buffer de un nodo puede tener pares posteriores a los de sus hijos; se ordena solo al escribirlo en un CSV.

*Llaves repetidas.* Los datos tienen #val("datos_repetidas") llaves repetidas. Un split puede dejar pares con la misma llave a ambos lados de una llave divisoria, así que en ambos árboles se visita un hijo si su rango _cerrado_ $[k_(i-1), k_i]$ intersecta $[l, u]$. Las pruebas incluyen datos con muchas llaves repetidas y con una sola llave.

#figure(
  tabla(
    ([*Operación*], [*Lecturas*], [*Escrituras*]),
    (
      [Árbol B: insertar un par sin splits (altura $h$)], [$h$], [1],
      [Árbol B: split de un nodo o de la raíz], [0], [3],
      [#Be: insertar un par en el buffer de la raíz], [1], [1],
      [#Be: entregar un lote a un hijo], [1], [1],
      [#Be: cada nodo nuevo que crea un split], [0], [1],
      [Búsqueda (ambos): cada nodo visitado, desde el archivo], [1], [0],
    ),
  ),
  caption: [I/Os que cuenta la implementación por cada operación.],
) <tab:costos>

== Experimentos y verificación

El ejecutable `build/tarea2` tiene los comandos `principal` ($N = 2^15, ..., 2^26$ con los tres árboles), `alpha` (bonus), `latencia` y `datos` (mediciones del disco y de `datos.bin` para este informe) y `consulta`/`buscar` (una búsqueda puntual). Para cada $N$ y cada árbol:
+ se leen los primeros $N$ pares de `datos.bin` en bloques de 65 536 y se insertan uno a uno; el tiempo de creación (`steady_clock`) cuenta solo las inserciones, no la lectura del archivo, y se registra también el tiempo de CPU del proceso;
+ se registran las I/Os simuladas y la estructura del árbol (nodos, altura, ocupación, vaciados, lotes y splits);
+ se guarda el árbol en disco y se libera su RAM;
+ se hacen las búsquedas sobre el archivo, midiendo el tiempo y las I/Os de cada una.

Los rangos se generan una vez con `mt19937_64` y semilla fija: $l$ uniforme en $[1 thin 546 thin 300 thin 800, 1 thin 754 thin 006 thin 400]$ y $u = l + 604 thin 800$. Son los mismos para todos los $N$ y árboles, lo que permite comparar una misma búsqueda. Cada construcción se repite varias veces, y el orden de los árboles (y de los $alpha$ en el bonus) rota en cada repetición, para que una interferencia pasajera de la máquina no caiga siempre sobre la misma configuración. Además de los tiempos, se registran indicadores de interferencia durante cada construcción y sus búsquedas: el tiempo de CPU del proceso, los cambios de contexto involuntarios, los fallos de página mayores, la presión de CPU y memoria que informa el kernel (PSI) y el tiempo robado por el hipervisor.

Cada búsqueda se compara con una búsqueda por fuerza bruta sobre los primeros $N$ pares (cantidad de pares, suma de llaves y suma de los bits de las temperaturas); si alguna no coincide, el programa termina con error. #if cond("hay_pruebas") [Además, `tests/pruebas.cpp` hace #val("pruebas_verificaciones") verificaciones (#val("pruebas_fallas") fallas en esta máquina)] else [Además, `tests/pruebas.cpp` hace miles de verificaciones]: cuenta las I/Os exactas de casos pequeños hechos a mano; valida todas las invariantes después de cada inserción en árboles chicos (orden, rangos de los subárboles, ocupación, hojas al mismo nivel, altura, ningún nodo perdido y el mismo multiconjunto de pares que se insertó), y compara búsquedas con fuerza bruta sobre datos aleatorios, ordenados, con llaves repetidas y extremas, y sobre una muestra de `datos.bin`.

= Resultados <sec:resultados>

== Entorno <sec:entorno>

#figure(
  tabla(([*Componente*], [*Valor*]), filas("tabla_entorno").flatten() + ([Informe], [Typst #sys.version]), izquierda: 2),
  caption: [Entorno de los experimentos, registrado por `make entorno` el #val("fecha_entorno"). La latencia es la de leer bloques de 4 KiB de un archivo de prueba en la misma carpeta donde se guardan los árboles, de la misma forma que las búsquedas.],
) <tab:entorno>

== Datos y metodología

`datos.bin` (#val("datos_mib") MiB#if cond("datos_sha_ok") [, con el sha256 publicado]) contiene #val("datos_pares") pares en orden aleatorio, con llaves entre #val("datos_llave_min") (#val("datos_fecha_min") UTC) y #val("datos_llave_max") (#val("datos_fecha_max") UTC), #val("datos_repetidas") llaves repetidas y temperaturas entre #val("datos_temp_min") y #val("datos_temp_max") °C. Para cada $N$ se usan los primeros $N$ pares; como el archivo está desordenado, cada prefijo es una muestra aleatoria de todo el período y está contenido en los prefijos más grandes.

*Experimento principal.* Cada combinación ($N$, árbol) se construyó #val("reps_principal") veces (#val("construcciones_principal") construcciones) y cada construcción se siguió de las mismas #val("busquedas_principal") búsquedas (#val("busquedas_total_principal") en total, todas iguales a la fuerza bruta). Las I/Os, nodos y alturas fueron idénticos en todas las repeticiones, porque el proceso es determinista. El tiempo de creación que se reporta es la _mediana_ de las repeticiones, y las figuras muestran además el mínimo y el máximo. El tiempo y las I/Os de búsqueda son el promedio de todas las búsquedas. #if cond("alpha_iguales") [Los árboles #Be usan $alpha = #val("alpha_usado_BE4")$] else [El #Be $c = 4$ usa $alpha = #val("alpha_usado_BE4")$ y el de $c = 126$, $alpha = #val("alpha_usado_BE126")$]#if cond("hay_elegido_BE4") and cond("hay_elegido_BE126") and cond("usado_es_elegido_BE4") and cond("usado_es_elegido_BE126") [, elegido automáticamente en el bonus (@sec:alpha)]. #if cond("odirect_todas") [Todas las búsquedas leyeron directo del disco (`O_DIRECT`).] else [*Atención:* algunas búsquedas no pudieron leer directo del disco (#val("odirect_motivos")), así que sus tiempos no son de disco.]

*Bonus.* Con $N = #Na$, para cada árbol #Be y cada uno de #val("alpha_cantidad") valores de $alpha$ (#val("alpha_lista")) se hicieron #val("alpha_reps") construcciones, cada una seguida de las mismas #val("alpha_busquedas") búsquedas (generadas con otra semilla).

*Interferencia.* #if cond("hay_interferencia") [Se considera sospechosa una construcción de al menos #val("interf_t_min") s cuyo tiempo de reloj sea más de #val("interf_umbral") veces su tiempo de CPU (el proceso estuvo esperando) o que tenga fallos de página mayores (la máquina usó swap). #if not cond("interf_hay_medibles") [Ninguna construcción duró lo suficiente para evaluarla.] else if cond("interf_ninguna") [Ninguna de las #val("interf_medibles") construcciones medibles fue sospechosa: el cociente reloj/CPU tuvo mediana #val("interf_cociente_mediana") y máximo #val("interf_cociente_max").] else [#val("interf_sospechosas") de las #val("interf_medibles") construcciones medibles fueron sospechosas (cociente reloj/CPU máximo: #val("interf_cociente_max")). Como una carga externa solo puede alargar una medición, la mediana de las repeticiones las descarta en la medida en que no se repitan.]] else [Esta corrida no registró el tiempo de CPU ni los indicadores de interferencia, así que no se puede descartar que otros procesos hayan alargado algunos tiempos; por eso se usa la mediana de las repeticiones.] Las I/Os, el tamaño y la altura no dependen de la máquina.

== Creación

#figure(
  image("figuras/creacion_tiempo.svg", width: 100%),
  caption: [Tiempo de creación (inserción de los $N$ pares) en función de $N$: (a) total, mediana de #val("reps_principal") repeticiones con barras de mínimo a máximo; (b) por par insertado.],
) <fig:tiempo>

#figure(
  image("figuras/creacion_ios.svg", width: 100%),
  caption: [I/Os simuladas al insertar los $N$ pares: (a) totales; (b) por par insertado; (c) por par sin contar la raíz (1 lectura en el árbol B, 1 lectura y 1 escritura en los #Be). La línea punteada es $2 dot "lotes" \/ N$: leer y escribir el hijo en cada lote de un vaciado.],
) <fig:ios>

- El tiempo de creación crece casi linealmente con $N$ (@fig:tiempo a). El orden #Be $c=4$ < #Be $c=126$ < B se cumple #if cond("orden_tiempo_todos") [para todo $N$] else [en #val("orden_tiempo_cumple") valores de $N$ (no en #pots(lst("orden_tiempo_excepciones")))]. Con $N = #Nmax$: #val("t_ins_BE4_max") s, #val("t_ins_BE126_max") s y #val("t_ins_B_max") s.
- Por par insertado (@fig:tiempo b), entre $N = #Nmin$ y $N = #Nmax$ el árbol B pasa de #val("us_ins_B_min") a #val("us_ins_B_max") µs, el #Be $c=126$ de #val("us_ins_BE126_min") a #val("us_ins_BE126_max") µs y el #Be $c=4$ de #val("us_ins_BE4_min") a #val("us_ins_BE4_max") µs.
- Las I/Os por inserción (@fig:ios b) pasan de #val("io_par_B_min") a #val("io_par_B_max") en el árbol B, de #val("io_par_BE126_min") a #val("io_par_BE126_max") en el #Be $c=126$ y de #val("io_par_BE4_min") a #val("io_par_BE4_max") en el #Be $c=4$. El orden #Be $c=4$ < #Be $c=126$ < B se cumple #if cond("orden_io_todos") [para todo $N$] else [en #val("orden_io_cumple") valores de $N$].
- Sin contar la raíz (@fig:ios c), con $N = #Nmax$ el árbol B hace #val("sinraiz_B_max") I/Os por inserción, el #Be $c=126$ #val("sinraiz_BE126_max") y el #Be $c=4$ #val("sinraiz_BE4_max"), es decir, #val("ventaja_sinraiz_BE126") y #val("ventaja_sinraiz_BE4") veces menos que el árbol B. En ambos #Be, la línea $2 dot "lotes" \/ N$ queda a menos de #val("modelo_lotes_dif_BE126") % (c=126) y #val("modelo_lotes_dif_BE4") % (c=4) de la curva medida.

#let estructura = filas("tabla_estructura")
#let medidas = (
  [Nodos internos], [Hojas], [% de los pares en nodos internos], [Pares por hoja (promedio)], [Vaciados],
  [Pares por lote (promedio)], [Bajadas por par], [Lecturas por par insertado], [Escrituras por par insertado],
  [ms por I/O de búsqueda],
)
#figure(
  tabla(
    ([*Medida*],) + estructura.map(r => [*#nombres.at(r.at(0))*]),
    range(medidas.len()).map(i => (medidas.at(i),) + estructura.map(r => r.at(i + 1))).flatten(),
    tam: 8.8pt,
  ),
  caption: [Estructura de los árboles y contadores de la inserción con $N = #Nmax$. _Bajadas por par_: total de pares bajados un nivel (sumando todos los lotes) dividido por $N$. La última fila es el tiempo de búsqueda dividido por sus I/Os.],
) <tab:estructura>

== Tamaño y altura

#figure(
  image("figuras/tamano_altura.svg", width: 100%),
  caption: [(a) Tamaño de los árboles (cantidad de nodos por 4096 bytes, que es el tamaño del archivo) y (b) altura, en función de $N$.],
) <fig:tamano>

- El tamaño crece proporcionalmente a $N$ (@fig:tamano a). Con $N = #Nmax$, el árbol B usa #val("nodos_B_max") nodos (#val("mib_B_max") MiB); el #Be $c=126$, #val("nodos_BE126_max") (#val("mib_BE126_max") MiB, #val("menos_nodos_BE126") % menos), y el #Be $c=4$, #val("nodos_BE4_max") (#val("mib_BE4_max") MiB, #val("menos_nodos_BE4") % menos). El orden #if cond("orden_tamano_todos") [es el mismo para todo $N$] else [se cumple en #val("orden_tamano_cumple") valores de $N$].
- La altura (@fig:tamano b) del árbol B pasa de #val("altura_B_min") a #val("altura_B_max") (sube en #pots(lst("altura_B_sube"))) y la del #Be $c=126$ de #val("altura_BE126_min") a #val("altura_BE126_max") (sube en #pots(lst("altura_BE126_sube"))). La del #Be $c=4$ pasa de #val("altura_BE4_min") a #val("altura_BE4_max") (sube en #pots(lst("altura_BE4_sube"))).
- Las hojas del árbol B tienen en promedio #val("pares_hoja_B") pares (#val("ocupacion_B") % de 340); las del #Be $c=126$, #val("pares_hoja_BE126") (#val("ocupacion_BE126") % de 384), y las del #Be $c=4$, #val("pares_hoja_BE4") (#val("ocupacion_BE4") % de 506). Además, el #val("pct_internos_BE4") % de los pares del #Be $c=4$ está en buffers de nodos internos, contra #val("pct_internos_BE126") % en el de $c=126$ y #val("pct_internos_B") % en los nodos internos del árbol B (@tab:estructura).

== Búsquedas

#figure(
  image("figuras/busqueda.svg", width: 100%),
  caption: [Búsquedas de rangos de una semana (promedio de todas las búsquedas y repeticiones) en función de $N$: (a) tiempo por búsqueda y (b) I/Os por búsqueda, con barras de ± una desviación estándar; (c) I/Os de cada árbol divididas por las del árbol B.],
) <fig:busqueda>

- Cada búsqueda devolvió en promedio el #val("frac_resultados") % de los $N$ pares (lo esperado para rangos de una semana es #val("frac_teorica") %), desde #val("res_min") pares con $N = #Nmin$ hasta #val("res_max") con $N = #Nmax$#if cond("mismos_resultados") [; los tres árboles devolvieron los mismos pares].
- Las I/Os por búsqueda (@fig:busqueda b) crecen casi proporcionalmente a $N$ cuando $N$ es grande. Con $N = #Nmin$ el #Be $c=4$ lee #val("io_bus_BE4_min") bloques, contra #val("io_bus_B_min") del árbol B (#val("rel_BE4_min") veces) y #val("io_bus_BE126_min") del #Be $c=126$. #if cond("hay_cruce") [Desde $N = #pots(lst("cruce"))$, el #Be $c=4$ es el que menos lee (@fig:busqueda c)] else [El #Be $c=4$ no llega a ser el que menos lee]: con $N = #Nmax$ hace #val("io_bus_BE4_max") I/Os, contra #val("io_bus_BE126_max") del #Be $c=126$ y #val("io_bus_B_max") del árbol B (#val("rel_BE4_max") y #val("rel_BE126_max") veces las del árbol B).
- El tiempo de búsqueda (@fig:busqueda a) sigue a las I/Os: con $N = #Nmax$, #val("t_bus_BE4_max") ms (#Be $c=4$), #val("t_bus_BE126_max") ms (#Be $c=126$) y #val("t_bus_B_max") ms (B), entre #val("ms_io_max_min") y #val("ms_io_max_max") ms por I/O (@tab:estructura). Para comparar tiempos se compara cada consulta en los distintos árboles dentro de la misma repetición, y se considera significativa una diferencia promedio mayor que dos errores estándar. Así, el #Be $c=4$ es significativamente más lento que los otros dos #if lst("be4_lento").len() > 0 [con $N$ = #pots(lst("be4_lento"))] else [en ningún $N$]#if cond("be4_rapido_al_final") [ y significativamente más rápido que ambos desde $N = #pots(lst("be4_rapido_desde"))$]#if lst("be4_empate").len() > 0 [; con $N$ = #pots(lst("be4_empate")) no hay una diferencia significativa con alguno de los otros dos]. Entre el árbol B y el #Be $c=126$, con $N = #Nmax$ la diferencia es de #val("b_vs_126_dif") ± #val("b_vs_126_ee") ms #if cond("b_vs_126_sig") [(significativa; #if cond("b_mas_lento_126") [el árbol B es el más lento]  else [el #Be $c=126$ es el más lento])] else [(no significativa)].

== Una misma búsqueda con dos valores de N

#figure(
  image("figuras/consulta_misma_busqueda.svg", width: 100%),
  caption: [Pares que devuelve la búsqueda 0, $[#val("c0_l"), #val("c0_u")]$ (del #val("c0_inicio") al #val("c0_fin"), hora de Chile), con $N = #pots(lst("consulta_fig"))$.],
) <fig:consulta>

#figure(
  tabla(
    ([*N*], [*Pares*], [*% de N*], [*Mín.\ (°C)*], [*Máx.\ (°C)*], [*Prom.\ (°C)*], [*Separación \ mediana (s)*], [*¿Contenido \ en el de $2^#val("c0_mayor")$?*]),
    filas("tabla_consulta").map(r => (potencia(r.at(0)),) + r.slice(1)).flatten(),
  ),
  caption: [Resumen de la búsqueda 0 para varios valores de $N$. _Separación mediana_: mediana del tiempo entre dos mediciones consecutivas del resultado.],
) <tab:consulta>

#let ca = str(lst("consulta_fig").first())
#let cb = str(lst("consulta_fig").last())
La búsqueda 0 cubre una semana (@fig:consulta). Con $N = 2^#ca$ devuelve #val("c0_pares_" + ca) mediciones, separadas por #val("c0_brecha_" + ca) en la mediana, y con $N = 2^#cb$ devuelve #val("c0_pares_" + cb), separadas por #val("c0_brecha_" + cb) (@tab:consulta). Las dos muestran el mismo ciclo diario: en los #val("c0_dias") días completos del rango, el máximo diario va de #val("c0_max_diario_min") a #val("c0_max_diario_max") °C y el mínimo diario de #val("c0_min_diario_min") a #val("c0_min_diario_max") °C (con $N = 2^#cb$). El promedio de la semana cambia poco entre ambos valores de $N$ (#val("c0_prom_" + ca) y #val("c0_prom_" + cb) °C). #if cond("c0_contenido") [Todos los pares de los resultados con $N$ menor aparecen en el de $N = 2^#val("c0_mayor")$, porque los prefijos están contenidos unos en otros.] Con $N = 2^#ca$ se pierden las variaciones que duran menos que la separación entre mediciones, que con $N = 2^#cb$ se ven como una curva casi continua.

== Bonus: elección de α <sec:alpha>

#figure(
  image("figuras/alpha_BE126.svg", width: 94%),
  caption: [Experimento de $alpha$ con el #Be $c = 126$ y $N = #Na$. (a) Tiempo de creación: mediana de #val("alpha_reps") repeticiones, barras de mínimo a máximo; (b) I/Os por inserción; (c) tiempo por búsqueda: promedio de todas las búsquedas, barras de ± un error estándar; (d) I/Os por búsqueda. Las escalas de (b) y (d) van de 5 % bajo el mínimo a 5 % sobre el máximo.],
) <fig:alpha126>

#figure(
  image("figuras/alpha_BE4.svg", width: 94%),
  caption: [Experimento de $alpha$ con el #Be $c = 4$ y $N = #Na$, con los mismos paneles que la @fig:alpha126.],
) <fig:alpha4>

#let encabezado-alpha = ([*α*], [*$t$ creación \ (s)*], [*desv. \ (s)*], [*I/Os \ por par*], [*Vaciados*], [*Pares \ por lote*], [*% pares \ en internos*], [*Nodos*], [*$t$ búsq. \ (ms)*], [*err. \ est. (ms)*], [*I/Os \ búsq.*])
// Columnas de tabla_alpha_*.csv que se muestran (se omite la 3, I/Os totales en millones).
#let columnas-alpha(r) = (0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11).map(i => r.at(i))

#figure(
  tabla(encabezado-alpha, filas("tabla_alpha_BE126").map(columnas-alpha).flatten()),
  caption: [Experimento de $alpha$ con el #Be $c = 126$ y $N = #Na$. El tiempo de creación es la mediana de las repeticiones y _desv._ su desviación estándar; el tiempo de búsqueda es el promedio de todas las búsquedas y _err. est._ su error estándar.],
) <tab:alpha126>

#figure(
  tabla(encabezado-alpha, filas("tabla_alpha_BE4").map(columnas-alpha).flatten()),
  caption: [Experimento de $alpha$ con el #Be $c = 4$ y $N = #Na$, con las mismas columnas que la @tab:alpha126.],
) <tab:alpha4>

/// Descripción de los resultados del bonus para un árbol (t = "BE126" o "BE4").
#let resumen-alpha(t) = [
  Las I/Os de carga pasan de #val("a_" + t + "_io_primero") por inserción con el menor $alpha$ a #val("a_" + t + "_io_ultimo") con el mayor (mínimo: #val("a_" + t + "_io_min"), con $alpha = #val("a_" + t + "_alpha_io_min")$), una baja de #val("a_" + t + "_baja_pct") % (#val("a_" + t + "_baja_sinraiz_pct") % sin contar la raíz), #if cond("a_" + t + "_monotona") [monótona en $alpha$] else [no estrictamente monótona (la mayor subida entre dos $alpha$ consecutivos es de #val("a_" + t + "_subida_max") %)]. El lote promedio pasa de #val("a_" + t + "_lote_primero") a #val("a_" + t + "_lote_ultimo") pares, los vaciados de #val("a_" + t + "_vac_primero") a #val("a_" + t + "_vac_ultimo") y la fracción de pares en nodos internos de #val("a_" + t + "_int_primero") % a #val("a_" + t + "_int_ultimo") %. #if cond("a_" + t + "_hay_identicos") [Con $alpha$ = #val("a_" + t + "_identicos") el árbol resultante es idéntico (los mismos contadores). ]La mediana del tiempo de creación queda entre #val("a_" + t + "_t_min") y #val("a_" + t + "_t_max") s (la menor con $alpha = #val("a_" + t + "_alpha_t_min")$). Las I/Os de búsqueda quedan entre #val("a_" + t + "_iob_min") y #val("a_" + t + "_iob_max") (#val("a_" + t + "_iob_var") % de diferencia) y el tiempo de búsqueda entre #val("a_" + t + "_tb_min") y #val("a_" + t + "_tb_max") ms.
]

- *#Be $c = 126$* (@fig:alpha126, @tab:alpha126). #resumen-alpha("BE126")
- *#Be $c = 4$* (@fig:alpha4, @tab:alpha4). #resumen-alpha("BE4")

/// Justificación del α elegido para un árbol (t = "BE126" o "BE4").
#let eleccion(t) = [
  Con $c = #(if t == "BE4" [4] else [126])$ se eligió $alpha = #val("alpha_elegido_" + t)$. Sus I/Os de carga están #val("a_" + t + "_dif_carga_elegido") % sobre el mínimo y sus I/Os de búsqueda, #val("a_" + t + "_dif_bus_elegido") % sobre el mínimo#if not cond("criterio_ambos_" + t) [ (ningún $alpha$ cumplía ambas tolerancias a la vez, así que se aplicó solo la de carga)]. #if cond("a_" + t + "_t_elegido_es_min") [Además tuvo la menor mediana del tiempo de creación.] else [Su mediana del tiempo de creación fue #val("a_" + t + "_t_dif_pct") % mayor que la menor#if cond("a_" + t + "_t_superpone") [, pero los rangos de sus repeticiones se superponen, así que el tiempo no distingue entre ellos].]#if cond("a_" + t + "_hay_mayores") [ Hace #val("a_" + t + "_menos_vaciados") % menos vaciados que el mayor $alpha$ probado.]
]

#if cond("hay_elegido_BE126") and cond("hay_elegido_BE4") [
*Elección.* El programa elige $alpha$ automáticamente con una regla basada en I/Os, que no dependen de la máquina: el menor $alpha$ cuyas I/Os de carga estén a lo más #val("tol_carga") % sobre el mínimo entre los $alpha$ probados y cuyas I/Os de búsqueda estén a lo más #val("tol_busqueda") % sobre el mínimo. Entre los $alpha$ que cumplen, el menor hace menos vaciados y por lo tanto menos trabajo de CPU.
- #eleccion("BE126")
- #eleccion("BE4")
#if cond("alpha_sin_compromiso") [En ambos árboles $alpha$ cambia las I/Os de búsqueda en a lo más #val("a_BE126_iob_var") % y #val("a_BE4_iob_var") %, así que en este experimento una mejor carga no empeoró la búsqueda.] else [En al menos un árbol $alpha$ cambia las I/Os de búsqueda en más de 5 % (#val("a_BE126_iob_var") % con $c = 126$ y #val("a_BE4_iob_var") % con $c = 4$): ahí hay un compromiso entre carga y búsqueda, que la tolerancia de búsqueda limita.] La @sec:analisis-alpha explica estos resultados.
]

= Análisis <sec:analisis>

== Costo amortizado de la inserción

*Árbol B.* Cada inserción lee los $h$ nodos del camino y escribe la hoja. Un split escribe 3 bloques y ocurre en promedio una vez cada $approx #val("pares_hoja_B_prom")$ inserciones en una hoja (cada split crea una hoja nueva, y las hojas tienen en promedio #val("pares_hoja_B_prom") pares), lo que suma unas $3 \/ #val("pares_hoja_B_prom")$ escrituras por inserción: con $N = #Nmax$ el árbol B hace #val("esc_par_B") escrituras por par#if cond("identidad_B") [ (en las #val("construcciones_b") construcciones del árbol B, las escrituras son exactamente $1 + N + 3 dot "splits"$)]. Por lo tanto, las I/Os por inserción son $h + 1$ más esa pequeña fracción, lo que explica los escalones de la @fig:ios b: suben un poco más de 1 cada vez que el árbol gana un nivel (en #pots(lst("altura_B_sube"))). Cuando la altura sube durante la construcción, las primeras inserciones se hicieron con un nivel menos, así que el promedio queda entre dos escalones.

*Árbol #Be.* Cada inserción cuesta 2 I/Os en la raíz. El resto son lotes: cada uno cuesta 2 I/Os (leer y escribir el hijo), más una escritura por nodo nuevo. #if cond("identidad_Be") [En las #val("construcciones_be") construcciones de #Be se cumple exactamente que las lecturas son $N + "lotes"$ y las escrituras $N + "lotes" + "nodos"$, y los nodos son pocos (#val("nodos_par_BE4") y #val("nodos_par_BE126") por inserción con $N = #Nmax$).] Si $overline(m)$ es el tamaño promedio de un lote y $overline(d)$ el número promedio de niveles que baja cada par,
$ "I/Os por inserción" approx 2 + (2 overline(d)) / overline(m). $ <eq:costo>
Con los contadores de la @tab:estructura, la @eq:costo da $2 + 2 dot #val("eq_d_BE4") \/ #val("eq_m_BE4") = #val("eq_modelo_BE4")$ para $c = 4$ y $2 + 2 dot #val("eq_d_BE126") \/ #val("eq_m_BE126") = #val("eq_modelo_BE126")$ para $c = 126$, a #val("eq_dif_BE4") y #val("eq_dif_BE126") de lo medido (#val("eq_medido_BE4") y #val("eq_medido_BE126")). Esa diferencia son las escrituras de los nodos nuevos, que la ecuación no incluye. La @fig:ios c muestra que ese término explica las I/Os sin la raíz para todo $N$.

El análisis amortizado acota ambos factores. Un par baja a lo más $h - 1 = O(log_(B^epsilon) N) = O(log_B N \/ epsilon)$ niveles. Y cada lote mueve muchos pares si el buffer está casi lleno al vaciarse, lo que se cumple siempre en la raíz (se vacía con $l_"max"$ pares) y en los demás nodos cuando se vacían porque no les cabe un lote: con $approx l_"max"$ pares repartidos entre a lo más $c + 1 approx B^epsilon$ hijos, el hijo más poblado recibe al menos $f approx l_"max" \/ (c + 1) = Omega(B^(1-epsilon))$ pares, y cada hijo elegido, al menos $alpha f$. Con un argumento de potencial, en el que cada par en un buffer guarda crédito para sus bajadas futuras y cada bajada cuesta $O(1 \/ (alpha B^(1-epsilon)))$ I/Os, el costo amortizado de los vaciados es
$ O(h / (alpha B^(1-epsilon))) = O((log_B N) / (alpha epsilon B^(1-epsilon))) "I/Os por inserción". $
En los datos, con $epsilon approx$ 0,25 los lotes son grandes ($overline(m) = #val("eq_m_BE4")$, del orden de $506 \/ #val("fanout_BE4") approx #val("fanout_teorico_BE4")$, porque los nodos internos tienen en promedio #val("fanout_BE4") hijos): aunque cada par baja #val("eq_d_BE4") niveles, cada bajada cuesta solo #val("costo_bajada_BE4") I/Os. Con $epsilon approx$ 0,75 los lotes son de #val("eq_m_BE126") pares: cada par baja solo #val("eq_d_BE126") niveles, pero cada bajada cuesta #val("costo_bajada_BE126") I/Os. La teoría predice que el costo del árbol B dividido por el de un #Be (sin la raíz) es $Theta(epsilon B^(1-epsilon))$: $approx 27$ para $epsilon = "0,25"$ y $approx "3,6"$ para $epsilon = "0,75"$. Lo medido con $N = #Nmax$ es #val("ventaja_sinraiz_BE4") y #val("ventaja_sinraiz_BE126"). Como $overline(d)$ crece con la altura, el costo sin la raíz de los #Be crece lentamente con $N$ (de #val("sinraiz_BE4_min") a #val("sinraiz_BE4_max") con $c = 4$, mientras la altura pasa de #val("altura_BE4_min") a #val("altura_BE4_max")).

*La raíz domina.* En el modelo de la tarea la raíz se lee y se escribe en cada inserción, y esas 2 I/Os son el #val("raiz_pct_BE4") % de las I/Os del #Be $c=4$ y el #val("raiz_pct_BE126") % de las del #Be $c=126$. Por eso, contando todo, la ventaja de los #Be sobre el árbol B es solo de #val("ventaja_total_BE4") y #val("ventaja_total_BE126") veces. En un sistema real la raíz (y probablemente los primeros niveles) estaría siempre en RAM, y la diferencia que importa es la de la @fig:ios c. Como ilustración, si cada I/O costara la latencia medida para este disco (#val("lat_aleatoria") ms por bloque, @tab:entorno), insertar los $#Nmax$ pares directamente en disco tomaría unas #val("ext_B") con el árbol B (#val("ext_B_sinraiz") con la raíz en RAM), mientras que con la raíz en RAM tomaría unas #val("ext_BE126_sinraiz") con el #Be $c=126$ y #val("ext_BE4_sinraiz") con el #Be $c=4$. Es una extrapolación gruesa, porque ignora la caché del sistema operativo y que las escrituras se pueden agrupar.

*Tiempo.* En la simulación, el tiempo de creación es aproximadamente las I/Os por el costo de copiar un bloque de 4 KiB: #val("us_io_chico_min")–#val("us_io_chico_max") µs por I/O con $N <= 2^20$, cuando los árboles (≤ #val("mib_chico") MiB) caben en buena parte en las cachés, y #val("us_io_max_min")–#val("us_io_max_max") µs con $N = #Nmax$, cuando ocupan #val("mib_grande_min")–#val("mib_grande_max") GiB y es probable que leer una hoja al azar falle en las cachés y en la TLB (esto no se midió directamente). #if cond("be4_mas_barato_por_io") [El #Be $c=4$ es el más barato por I/O (#val("us_io_BE4_max") µs contra #val("us_io_B_max") µs del árbol B) porque casi todas sus I/Os son la raíz, que se usa en cada inserción y por eso se mantiene en la caché. ]Por eso la ventaja en tiempo puede ser mayor que en I/Os: con $N = #Nmax$, el árbol B tarda #val("razon_t_B_BE4") veces lo que el #Be $c=4$ y hace #val("razon_io_B_BE4") veces sus I/Os.

== Altura en función de ε y costo de búsqueda

*Altura.* En un árbol con $H$ hojas cuyos nodos internos tienen en promedio $F$ hijos, $h approx 1 + log_F H$. Con $N = #Nmax$, $F$ es #val("fanout_B") en el árbol B, #val("fanout_BE126") en el #Be $c=126$ y #val("fanout_BE4") en el #Be $c=4$. Eso da $log_F H$ = #val("logF_B"), #val("logF_BE126") y #val("logF_BE4"), del orden de los #val("niveles_internos_B"), #val("niveles_internos_BE126") y #val("niveles_internos_BE4") niveles internos medidos (alturas #val("altura_B_max"), #val("altura_BE126_max") y #val("altura_BE4_max")). La fórmula es aproximada porque supone que todos los niveles tienen la misma cantidad de hijos, y cerca de la raíz no es así (la raíz puede tener pocos hijos). La razón entre los niveles internos de los dos #Be se compara con la razón 3 entre sus $1\/epsilon$, que predice $h = O(log_B N \/ epsilon)$. Con $F approx 3$, el #Be $c=4$ debería sumar un nivel cada vez que $N$ se multiplica por $approx 3$; en los datos sumó #val("be4_niveles_sumados") niveles entre $#Nmin$ y $#Nmax$, uno por cada factor de $approx #val("be4_factor_por_nivel")$. Los otros dos suman un nivel cada vez que $N$ se multiplica por un factor del orden de su $F$.

*Búsqueda.* Un rango que cubre una fracción $p$ del espacio de llaves intersecta $approx p dot n_ell + 1$ de los $n_ell$ nodos de cada nivel $ell$, así que
$ "I/Os por búsqueda" approx p dot n + h, $ <eq:busqueda>
donde $n$ es el total de nodos. Con $p = 604 thin 800 \/ 207 thin 705 thin 600 = #val("p_pct") %$, la @eq:busqueda queda a menos de #val("modelo_bus_dif_max") % de lo medido para $N >= 2^18$ (@tab:modelo). Esto explica la @fig:busqueda c:
- Con $N$ chico#if cond("pn_min_menor_1") [, $p dot n$ es menor que 1 (a lo más #val("pn_min_max") con $N = #Nmin$) y] manda la altura: con $N = #Nmin$, el #Be $c=4$ ($h = #val("altura_BE4_min")$) hace #val("rel_BE4_min") veces las I/Os del árbol B ($h = #val("altura_B_min")$).
- Con $N$ grande manda $p dot n$ (con $N = #Nmax$, de #val("pn_max_min") a #val("pn_max_max"), contra alturas de #val("altura_B_max") a #val("altura_BE4_max")), y el orden lo da el tamaño. El #Be $c=4$ tiene #val("menos_nodos_BE4") % menos nodos que el árbol B porque sus hojas guardan #val("pares_hoja_BE4") pares en promedio contra #val("pares_hoja_B"), y porque guarda el #val("pct_internos_BE4") % de los pares en buffers de nodos internos, que en el árbol B solo sirven para dirigir la búsqueda. #if cond("hay_cruce") [Las curvas se cruzan cerca de $N = #pots(lst("cruce"))$, cuando la diferencia de tamaño compensa la diferencia de altura.]

Por lo tanto, para estos rangos el árbol más bajo no es necesariamente el más rápido, sino el más compacto. En una búsqueda puntual el costo sería solo la altura (#val("altura_B_max"), #val("altura_BE126_max") y #val("altura_BE4_max") I/Os con $N = #Nmax$), y el árbol B y el #Be $c=126$ serían más baratos que el #Be $c=4$. El tiempo de búsqueda es casi exactamente las I/Os por la latencia del disco: entre #val("ms_io_min") y #val("ms_io_max") ms por I/O en todos los $N$ y árboles, del orden de la latencia medida (#val("lat_aleatoria") ms), así que revisar los buffers de los nodos visitados no tiene un costo de CPU apreciable.

#figure(
  tabla(
    ([*N*], [*Árbol*], [*Nodos $n$*], [*Altura $h$*], [*$p dot n + h$*], [*I/Os medidas*], [*Diferencia (%)*]),
    filas("tabla_modelo_busqueda").map(r => (potencia(r.at(0)), nombres.at(r.at(1))) + r.slice(2)).flatten(),
    izquierda: 2,
  ),
  caption: [I/Os por búsqueda: modelo de la @eq:busqueda y valor medido (promedio de todas las búsquedas).],
) <tab:modelo>

== Efecto de α <sec:analisis-alpha>

Un lote cuesta 2 I/Os sin importar cuántos pares lleve, así que bajar grupos chicos es lo que desperdicia I/Os: si se dejan en el buffer, bajan más tarde junto con los pares que lleguen después. El umbral $ceil(alpha f)$ decide cuáles grupos bajan. Un $alpha$ alto hace más vaciados, porque cada uno libera menos espacio, pero en esta implementación un vaciado no cuesta I/Os propias: la raíz se lee en cada inserción y un nodo interno se vacía cuando ya se leyó para recibir un lote. Solo cuesta CPU (agrupar el buffer). Por eso las I/Os de carga tienden a bajar al subir $alpha$#if cond("a_BE126_monotona") and cond("a_BE4_monotona") [, y en los dos árboles bajaron de forma monótona] else if cond("a_BE126_monotona") [: con $c = 126$ bajaron de forma monótona, y con $c = 4$, salvo variaciones de hasta #val("a_BE4_subida_max") %, porque $alpha$ también cambia la forma del árbol] else [, salvo variaciones pequeñas, porque $alpha$ también cambia la forma del árbol].

El efecto depende de cuántos pares le tocan a cada hijo. Con $c = 4$, un vaciado reparte $approx 500$ pares entre unos #val("fanout_BE4") hijos, así que los grupos tienen más de cien pares y los lotes ya son grandes con cualquier $alpha$ (de #val("a_BE4_lote_primero") a #val("a_BE4_lote_ultimo") pares): $alpha$ ahorra poco (#val("a_BE4_baja_pct") %). Con $c = 126$, en cambio, $f approx 384 \/ #val("fanout_BE126") approx #val("f_BE126")$: con tan pocos pares por hijo, es esperable que el tamaño de los grupos varíe mucho de un hijo a otro. Con $alpha = #val("alpha_min_BE126")$ el umbral es #val("umbral_min_BE126") y baja casi todo grupo no vacío#if cond("hay_elegido_BE126") [; con $alpha = #val("alpha_elegido_BE126")$ el umbral es #val("umbral_elegido_BE126") y los grupos chicos esperan]. Ahí $alpha$ cambia el costo en un #val("a_BE126_baja_sinraiz_pct") % sin contar la raíz.

En la búsqueda, $alpha$ cambia cuántos pares quedan en buffers internos (de #val("a_BE4_int_primero") % a #val("a_BE4_int_ultimo") % con $c = 4$), pero esos nodos se leen igual, porque están en el camino de la búsqueda. Según la @eq:busqueda lo que importa es el número de nodos, que con $alpha$ cambia a lo más #val("a_BE126_nodos_var") % con $c = 126$ y #val("a_BE4_nodos_var") % con $c = 4$, y las I/Os de búsqueda cambian a lo más #val("a_BE126_iob_var") % y #val("a_BE4_iob_var") %, respectivamente. El tiempo por I/O de búsqueda varía entre #val("a_BE126_ms_io_min") y #val("a_BE126_ms_io_max") ms ($c = 126$) y entre #val("a_BE4_ms_io_min") y #val("a_BE4_ms_io_max") ms ($c = 4$) entre valores de $alpha$, así que las diferencias de tiempo de búsqueda entre $alpha$ vienen sobre todo de la variación de la latencia del disco. #if cond("alpha_sin_compromiso") [Así, el compromiso que menciona el enunciado (mejor carga a cambio de peor búsqueda) no aparece en este experimento: la búsqueda casi no depende de $alpha$, y el criterio para elegirlo es la carga.] else [En este experimento sí aparece un compromiso entre carga y búsqueda en al menos un árbol, que la regla de elección resuelve con su tolerancia de búsqueda.]

= Conclusión <sec:conclusion>

Se implementaron un árbol B y dos árboles #Be con nodos de 4096 bytes. Se construyeron sobre un disco simulado que cuenta cada transferencia de bloque, se guardaron en un archivo y se buscaron leyendo de él con `O_DIRECT`, para $N = #Nmin, ..., #Nmax$ pares reales de temperatura. Todas las búsquedas se verificaron contra fuerza bruta y las invariantes de las estructuras se probaron por separado.

*Hipótesis.*
+ _Inserción: #if cond("h1") [se cumplió] else if cond("h1_io") [se cumplió en I/Os y en parte en tiempo] else [no se cumplió del todo]._ En I/Os el orden #Be $c=4$ < #Be $c=126$ < B se cumplió #if cond("orden_io_todos") [para todo $N$] else [en #val("orden_io_cumple") valores de $N$], y en tiempo #if cond("orden_tiempo_todos") [también para todo $N$] else [en #val("orden_tiempo_cumple")]: con $N = #Nmax$, #val("io_par_BE4_max"), #val("io_par_BE126_max") y #val("io_par_B_max") I/Os por inserción, y #val("t_ins_BE4_max"), #val("t_ins_BE126_max") y #val("t_ins_B_max") s. Las I/Os de los #Be están dominadas por las 2 de la raíz; sin ellas, la diferencia con el árbol B es de #val("ventaja_sinraiz_BE4") y #val("ventaja_sinraiz_BE126") veces, del orden de $epsilon B^(1-epsilon)$ que predice el análisis amortizado. La causa es el tamaño de los lotes: #val("eq_m_BE4") pares por lote con $epsilon approx$ 0,25 contra #val("eq_m_BE126") con 0,75.
+ _Altura y búsqueda: #if cond("h2_alturas") and cond("hay_cruce") [se cumplió en parte] else if cond("h2_alturas") [se cumplió] else [no se cumplió]._ #if cond("h2_alturas") [Las alturas fueron las esperadas] else [Las alturas no fueron las esperadas] (#val("altura_BE4_max") contra #val("altura_BE126_max") y #val("altura_B_max") con $N = #Nmax$)#if cond("be4_mas_io_min") [, y con $N$ chico el #Be $c=4$ fue el peor en búsquedas (#val("rel_BE4_min") veces las I/Os del árbol B con $#Nmin$)]. #if cond("hay_cruce") [Pero desde $N = #pots(lst("cruce"))$ fue el que menos leyó, no solo similar a los otros: con $N = #Nmax$ hizo #val("rel_BE4_max") veces las I/Os del árbol B. No se anticipó que una búsqueda que devuelve cientos de bloques cuesta $p dot n + h$, y que los buffers del #Be también guardan datos, lo que lo hace el árbol más compacto.] else [Con $N$ grande los tres árboles se acercaron, como se esperaba (#val("rel_BE4_max") y #val("rel_BE126_max") veces las I/Os del árbol B con $N = #Nmax$).]
+ _Tamaño: #if cond("h3") [se cumplió] else [no se cumplió para todo $N$]._ Con $N = #Nmax$, el #Be $c=4$ usó #val("menos_nodos_BE4") % menos bloques que el árbol B y el de $c=126$, #val("menos_nodos_BE126") % menos.

En el bonus, el costo de carga baja al subir $alpha$#if cond("a_BE126_monotona") [ (de forma monótona con $c = 126$)], mientras que la búsqueda #if cond("alpha_sin_compromiso") [casi no depende de $alpha$] else [sí depende de $alpha$ en al menos un árbol]. #if cond("hay_elegido_BE126") and cond("hay_elegido_BE4") [Con la regla de elección se usó $alpha = #val("alpha_elegido_BE4")$ con $c = 4$ y $alpha = #val("alpha_elegido_BE126")$ con $c = 126$.]

*Limitaciones y mejoras.*
- El modelo cuenta 2 I/Os de raíz por inserción, lo que esconde la mayor parte de la ventaja de los #Be. Una versión futura podría mantener la raíz y los primeros niveles en un _buffer pool_ (por ejemplo, LRU) y contar solo los bloques que salen de él, lo que se parecería más a un sistema real.
- Las inserciones se midieron en RAM. Medir algunas construcciones directamente en disco permitiría validar la extrapolación de la @sec:analisis.
- Solo se usaron rangos de una semana. Variar el ancho del rango (desde búsquedas puntuales hasta meses) mostraría en qué punto conviene cada árbol, según la @eq:busqueda.
- Los tiempos dependen de la máquina (@tab:entorno) y de la carga que tenía al medir; las I/Os, el tamaño y la altura no. #if cond("hay_interferencia") [En esta corrida, #val("interf_sospechosas") de #val("interf_medibles") construcciones mostraron señales de interferencia.]
- No se implementó el borrado (no se pedía), ni la carga masiva de datos ordenados, que permitiría construir árboles completamente llenos.

#heading(numbering: none)[Anexo: resultados completos del experimento principal]

#let celdas-principal = {
  let celdas = ()
  for (i, r) in filas("tabla_principal").enumerate() {
    if i > 0 and r.at(1) == "B" { celdas.push(table.hline(stroke: 0.3pt + luma(160))) }
    celdas += (if r.at(1) == "B" { potencia(r.at(0)) } else { [] }, nombres.at(r.at(1))) + r.slice(2)
  }
  celdas
}

#figure(
  tabla(
    ([*N*], [*Árbol*], [*$t$ creación \ (s)*], [*I/Os creación \ (millones)*], [*I/Os \ por par*], [*Nodos*], [*MiB*], [*Altura*], [*$t$ búsqueda \ (ms)*], [*I/Os \ búsqueda*], [*Resultados*]),
    celdas-principal,
    izquierda: 2,
    tam: 8.4pt,
  ),
  caption: [Resultados del experimento principal. El tiempo de creación es la mediana de #val("reps_principal") repeticiones; el tiempo, las I/Os y los resultados de búsqueda son promedios de todas las búsquedas. Las I/Os de creación son las del disco simulado y las de búsqueda son bloques leídos del archivo.],
) <tab:principal>
