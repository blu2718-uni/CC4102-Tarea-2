# Tarea 2 CC4102 - Árboles B y Bε
#
# Todo lo que usa el informe se genera en la máquina que ejecuta esto (nada viene en el repo):
#
#   make descargar-datos   descarga data/datos.bin y verifica su sha256
#   make todo              pruebas, entorno, experimentos, gráficos e informe (en ese orden)
#
# Pasos sueltos:
#   make                   compila build/tarea2 (programa principal) y build/pruebas
#   make test              ejecuta las pruebas (resultado en resultados/pruebas.csv)
#   make test-san          ejecuta las pruebas con AddressSanitizer y UBSan
#   make entorno           registra la máquina, la latencia del disco y los datos (resultados/)
#   make alpha             bonus: N = 2^24, 8 valores de α, ambos Bε; elige α automáticamente
#   make principal         experimento principal (N = 2^15..2^26) con el α elegido
#   make graficos          crea .venv con matplotlib y genera figuras, tablas y valores del informe
#   make informe           compila informe/informe.typ a informe/informe.pdf
#   make clean             borra build/ y arboles/ (no borra resultados/)
#
# Variables: REPS (repeticiones, por defecto 5), TYPST (ejecutable de typst).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD    := build
HEADERS  := $(wildcard src/*.hpp)
PYTHON   ?= python3
VENV     := .venv
REPS     ?= 5
DATOS    := data/datos.bin
DATOS_URL := https://github.com/CristobalPuentes27/tarea2-CC4102-2026-2/releases/download/datos/datos.bin
DATOS_SHA := 63e5f8da1cc7bf640f6b768dc5565d32fd1d601c65a68869ef17472b21d27a67

# Typst: el del PATH si existe; si no, una versión fija descargada en .herramientas/.
TYPST_VERSION := 0.15.1
TYPST_SHA := a6d077d0a95eed5a2eba715b2dae06be954f624ccbf85758a03f389ded33118c
TYPST ?= $(shell command -v typst 2>/dev/null || echo .herramientas/typst)

.PHONY: all test test-san entorno alpha principal graficos informe todo descargar-datos verificar-datos clean

all: $(BUILD)/tarea2 $(BUILD)/pruebas

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tarea2: src/main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

$(BUILD)/pruebas: tests/pruebas.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/pruebas.cpp

$(BUILD)/pruebas-san: tests/pruebas.cpp $(HEADERS) | $(BUILD)
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -o $@ tests/pruebas.cpp

descargar-datos:
	mkdir -p data
	curl -L --fail -o $(DATOS).part $(DATOS_URL)
	echo "$(DATOS_SHA)  $(DATOS).part" | sha256sum -c
	mv $(DATOS).part $(DATOS)

verificar-datos:
	@test -f $(DATOS) || { echo "Falta $(DATOS). Ejecute: make descargar-datos" >&2; exit 1; }

test: $(BUILD)/pruebas
	mkdir -p resultados
	./$(BUILD)/pruebas resultados/pruebas.csv

test-san: $(BUILD)/pruebas-san
	./$(BUILD)/pruebas-san

entorno: $(BUILD)/tarea2 verificar-datos
	bash scripts/entorno.sh resultados/entorno.csv arboles $(DATOS) "$(CXX) $(CXXFLAGS)"
	./$(BUILD)/tarea2 latencia
	./$(BUILD)/tarea2 datos

alpha: $(BUILD)/tarea2 verificar-datos
	./$(BUILD)/tarea2 alpha --arbol BE126 --repeticiones $(REPS)
	./$(BUILD)/tarea2 alpha --arbol BE4 --repeticiones $(REPS)

principal: $(BUILD)/tarea2 verificar-datos
	./$(BUILD)/tarea2 principal --repeticiones $(REPS)

# Entorno de Python solo para los gráficos. Si el Python del sistema no trae
# ensurepip (pasa en Debian sin python3-venv), se intenta con uv. El archivo
# .venv/.instalado marca que el entorno está listo.
$(VENV)/.instalado: graficos/requirements.txt
	rm -rf $(VENV)
	if $(PYTHON) -m venv $(VENV); then \
		$(VENV)/bin/python -m pip install -r graficos/requirements.txt; \
	elif command -v uv >/dev/null; then \
		rm -rf $(VENV) && uv venv --python $(PYTHON) $(VENV) && \
		uv pip install --python $(VENV)/bin/python -r graficos/requirements.txt; \
	else \
		rm -rf $(VENV); \
		echo "No se pudo crear el entorno de Python. En Debian/Ubuntu: sudo apt install python3-venv" >&2; \
		exit 1; \
	fi
	touch $@

graficos: $(VENV)/.instalado
	@test -f resultados/principal.csv || { echo "Faltan resultados: ejecute make entorno, make alpha y make principal" >&2; exit 1; }
	$(VENV)/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras

.herramientas/typst:
	mkdir -p .herramientas
	curl -L --fail -o .herramientas/typst.tar.xz \
		https://github.com/typst/typst/releases/download/v$(TYPST_VERSION)/typst-x86_64-unknown-linux-musl.tar.xz
	echo "$(TYPST_SHA)  .herramientas/typst.tar.xz" | sha256sum -c
	tar -xJf .herramientas/typst.tar.xz -C .herramientas --strip-components=1 typst-x86_64-unknown-linux-musl/typst
	rm .herramientas/typst.tar.xz

informe: $(if $(filter .herramientas/typst,$(TYPST)),.herramientas/typst)
	@test -f informe/figuras/valores.json || { echo "Faltan las figuras: ejecute make graficos" >&2; exit 1; }
	$(TYPST) compile informe/informe.typ informe/informe.pdf

# Orden fijo, sin paralelismo: las mediciones no deben competir entre sí.
todo:
	$(MAKE) -j1 all
	$(MAKE) -j1 test
	$(MAKE) -j1 entorno
	$(MAKE) -j1 alpha
	$(MAKE) -j1 principal
	$(MAKE) -j1 graficos
	$(MAKE) -j1 informe

clean:
	rm -rf $(BUILD) arboles
