#!/usr/bin/env bash
# Registra el entorno de la máquina donde se ejecutan los experimentos, para el informe.
#
# Uso: scripts/entorno.sh SALIDA.csv CARPETA_ARBOLES DATOS.bin "COMPILADOR Y FLAGS"
#
# Escribe un CSV clave,valor (SO, CPU, cachés, RAM, disco y sistema de archivos donde se
# guardan los árboles, compilador, sha256 de datos.bin, etc.) y avisa por stderr si hay
# condiciones que afectan las mediciones (tmpfs, carga alta, gobernador de CPU, datos.bin
# distinto al publicado).
set -u

salida=${1:?falta el archivo de salida}
arboles=${2:?falta la carpeta de árboles}
datos=${3:?falta la ruta de datos.bin}
compilador=${4:-g++}
sha_esperado=63e5f8da1cc7bf640f6b768dc5565d32fd1d601c65a68869ef17472b21d27a67

mkdir -p "$(dirname "$salida")" "$arboles"
avisos=0

# Escribe una fila clave,valor con el valor entre comillas (CSV).
fila() {
    local v=${2//\"/\"\"}
    printf '%s,"%s"\n' "$1" "$v" >> "$salida"
}

# Avisa por stderr de una condición que afecta las mediciones.
aviso() {
    echo "AVISO: $*" >&2
    avisos=$((avisos + 1))
}

# Tamaño legible de un número de bytes (KiB/MiB/GiB).
legible() {
    awk -v b="$1" 'BEGIN { split("B KiB MiB GiB TiB", u, " "); i = 1; while (b >= 1024 && i < 5) { b /= 1024; i++ }
                           printf (b == int(b) ? "%d %s" : "%.1f %s"), b, u[i] }'
}

# Caché de un nivel y tipo (desde sysfs): "<tamaño por instancia> × <instancias>".
cache() {
    local nivel=$1 tipo=$2 tam="" ids=""
    for d in /sys/devices/system/cpu/cpu[0-9]*/cache/index*; do
        [ -r "$d/level" ] || continue
        [ "$(cat "$d/level")" = "$nivel" ] && [ "$(cat "$d/type")" = "$tipo" ] || continue
        tam=$(cat "$d/size")
        ids="$ids $(cat "$d/id" 2>/dev/null || cat "$d/shared_cpu_list")"
    done
    [ -n "$tam" ] || { echo "no disponible"; return; }
    local n
    n=$(echo "$ids" | tr ' ' '\n' | sed '/^$/d' | sort -u | wc -l)
    echo "${tam/K/ KiB} × $n"
}

: > "$salida"
echo "clave,valor" >> "$salida"
fila fecha "$(date '+%Y-%m-%d %H:%M %z')"
fila so "$( (. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") || uname -s)"
fila kernel "$(uname -r) ($(uname -m))"
virt=$(systemd-detect-virt 2>/dev/null || true)
fila virtualizacion "${virt:-none}"
[ -n "$virt" ] && [ "$virt" != "none" ] && aviso "la máquina es virtual ($virt): los tiempos pueden incluir interferencia del hipervisor"

modelo=$(LC_ALL=C lscpu 2>/dev/null | sed -n 's/^Model name:[[:space:]]*//p' | head -1)
fila cpu_modelo "${modelo:-$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')}"
nucleos=$(LC_ALL=C lscpu -p=CORE 2>/dev/null | grep -v '^#' | sort -u | wc -l)
fila cpu_hilos "$(nproc)"
fila cpu_nucleos "${nucleos:-?}"
frec=$(LC_ALL=C lscpu 2>/dev/null | sed -n 's/^CPU max MHz:[[:space:]]*//p' | head -1)
fila cpu_frec_max_mhz "${frec:-no disponible}"
gob=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "no disponible")
fila cpu_gobernador "$gob"
[ "$gob" = "powersave" ] && aviso "el gobernador de CPU es powersave; si puede: sudo cpupower frequency-set -g performance"
fila cache_l1d "$(cache 1 Data)"
fila cache_l1i "$(cache 1 Instruction)"
fila cache_l2 "$(cache 2 Unified)"
fila cache_l3 "$(cache 3 Unified)"

ram=$(awk '/^MemTotal:/ { print $2 * 1024 }' /proc/meminfo)
swap=$(awk '/^SwapTotal:/ { print $2 * 1024 }' /proc/meminfo)
disp=$(awk '/^MemAvailable:/ { print $2 * 1024 }' /proc/meminfo)
fila ram "$(legible "$ram")"
fila swap "$(legible "$swap")"
fila ram_disponible "$(legible "$disp")"
[ "$disp" -lt $((2 * 1024 * 1024 * 1024)) ] && aviso "hay menos de 2 GiB de RAM disponibles; el árbol B con N = 2^26 ocupa ~1,1 GiB"

fs=$(findmnt -n -o FSTYPE -T "$arboles" 2>/dev/null || stat -f -c %T "$arboles")
opciones=$(findmnt -n -o OPTIONS -T "$arboles" 2>/dev/null || echo "?")
fuente=$(findmnt -n -o SOURCE -T "$arboles" 2>/dev/null | sed 's/\[.*\]//')
disco="no disponible"
if [ -n "$fuente" ] && [ -b "$fuente" ]; then
    padre=$(lsblk -no PKNAME "$fuente" 2>/dev/null | head -1)
    dev=/dev/${padre:-$(basename "$fuente")}
    info=$(lsblk -dno MODEL,TRAN,ROTA,SIZE "$dev" 2>/dev/null | sed 's/  */ /g; s/^ //')
    [ -n "$info" ] && disco="$(echo "$info" | awk '{ rota = $(NF-1); tam = $NF; $NF = ""; $(NF-1) = "";
                                                    sub(/ +$/, ""); print $0 ", " tam (rota == 1 ? ", rotacional" : ", no rotacional") }')"
fi
fila disco "$disco"
fila sistema_archivos "$fs"
fila opciones_montaje "$opciones"
case "$fs" in
    tmpfs|ramfs) aviso "$arboles está en $fs (RAM): las búsquedas no leerían del disco; use una carpeta en disco" ;;
    btrfs) echo "nota: $arboles está en Btrfs; los árboles se crean con NOCOW (sin compresión) para poder leerlos con O_DIRECT" >&2 ;;
esac

fila compilador "$(${compilador%% *} --version 2>/dev/null | head -1)"
fila flags_compilacion "${compilador#* }"

if [ -f "$datos" ]; then
    sha=$(sha256sum "$datos" | cut -d' ' -f1)
    fila datos_sha256 "$sha"
    [ "$sha" = "$sha_esperado" ] && fila datos_ok "si" || { fila datos_ok "no"; aviso "el sha256 de $datos no coincide con el publicado"; }
else
    fila datos_sha256 "no encontrado"
    fila datos_ok "no"
    aviso "no se encontró $datos (ver README: make descargar-datos)"
fi
carga=$(cut -d' ' -f1 /proc/loadavg)
fila carga_1min "$carga"
awk -v c="$carga" -v n="$(nproc)" 'BEGIN { exit !(c > 0.5 && c > n * 0.1) }' &&
    aviso "la carga de la máquina es $carga: cierre otros programas antes de medir"

echo "Entorno guardado en $salida ($avisos avisos)" >&2
exit 0
