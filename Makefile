# Tarea 2 CC4102 - Árboles B y Bε
#
#   make descargar-datos   descarga data/datos.bin y verifica su sha256
#   make                   compila build/tarea2 (programa principal) y build/pruebas
#   make test              ejecuta las pruebas de correctitud
#   make alpha             bonus: N = 2^24, 8 valores de α en Bε (c = 126)
#   make principal         experimento principal (N = 2^15..2^26)
#   make graficos          genera figuras y tablas en informe/figuras/
#   make informe           compila informe/informe.typ a informe/informe.pdf
#   make todo              ejecuta todo en orden
#   make clean             borra build/ y arboles/

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD    := build
HEADERS  := $(wildcard src/*.hpp)
PYTHON   ?= python3
VENV     := .venv
REPS     ?= 1
DATOS    := data/datos.bin
DATOS_URL := https://github.com/CristobalPuentes27/tarea2-CC4102-2026-2/releases/download/datos/datos.bin
DATOS_SHA := 63e5f8da1cc7bf640f6b768dc5565d32fd1d601c65a68869ef17472b21d27a67
TYPST    ?= typst

.PHONY: all test alpha principal graficos informe todo descargar-datos verificar-datos clean

all: $(BUILD)/tarea2 $(BUILD)/pruebas

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tarea2: src/main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

$(BUILD)/pruebas: tests/pruebas.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/pruebas.cpp

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

alpha: $(BUILD)/tarea2 verificar-datos
	./$(BUILD)/tarea2 alpha --arbol BE126 --repeticiones $(REPS)

principal: $(BUILD)/tarea2 verificar-datos
	./$(BUILD)/tarea2 principal --repeticiones $(REPS)

$(VENV)/.instalado: graficos/requirements.txt
	rm -rf $(VENV)
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/python -m pip install -r graficos/requirements.txt
	touch $@

graficos: $(VENV)/.instalado
	$(VENV)/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras

informe:
	$(TYPST) compile informe/informe.typ informe/informe.pdf

todo:
	$(MAKE) -j1 all
	$(MAKE) -j1 test
	$(MAKE) -j1 alpha
	$(MAKE) -j1 principal
	$(MAKE) -j1 graficos
	$(MAKE) -j1 informe

clean:
	rm -rf $(BUILD) arboles
