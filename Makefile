# Tarea 2 CC4102 - Árboles B y Bε
#
#   make              compila build/tarea2 (programa principal) y build/pruebas
#   make test         compila y ejecuta las pruebas
#   make test-san     ejecuta las pruebas con AddressSanitizer y UBSan
#   make alpha        experimento bonus de α (N = 2^24, 8 valores, 20 búsquedas, 3 repeticiones)
#   make principal    experimento principal (N = 2^15..2^26, 50 búsquedas, 3 repeticiones)
#   make graficos     crea .venv con matplotlib y genera las figuras y tablas del informe
#   make informe      compila informe/informe.typ a informe/informe.pdf (requiere typst)
#   make clean        borra build/ y arboles/

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
BUILD    := build
HEADERS  := $(wildcard src/*.hpp)
PYTHON   ?= python3
VENV     := .venv
TYPST    ?= typst
REPS     ?= 3

.PHONY: all test test-san principal alpha graficos informe clean

all: $(BUILD)/tarea2 $(BUILD)/pruebas

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tarea2: src/main.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

$(BUILD)/pruebas: tests/pruebas.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/pruebas.cpp

$(BUILD)/pruebas-san: tests/pruebas.cpp $(HEADERS) | $(BUILD)
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -o $@ tests/pruebas.cpp

test: $(BUILD)/pruebas
	./$(BUILD)/pruebas

test-san: $(BUILD)/pruebas-san
	./$(BUILD)/pruebas-san

alpha: $(BUILD)/tarea2
	./$(BUILD)/tarea2 alpha --arbol BE126 --repeticiones $(REPS)
	./$(BUILD)/tarea2 alpha --arbol BE4 --repeticiones $(REPS)

principal: $(BUILD)/tarea2
	./$(BUILD)/tarea2 principal --repeticiones $(REPS)

# Entorno de Python solo para los gráficos. Si el Python del sistema no trae
# ensurepip (pasa en Debian sin python3-venv), se intenta con uv. El archivo
# .venv/.instalado marca que el entorno está listo (no se usa .venv/bin/python
# como objetivo porque puede ser un enlace a un intérprete fuera del proyecto).
$(VENV)/.instalado: graficos/requirements.txt
	rm -rf $(VENV)
	if $(PYTHON) -m venv $(VENV); then \
		$(VENV)/bin/python -m pip install -r graficos/requirements.txt; \
	else \
		rm -rf $(VENV) && uv venv --python $(PYTHON) $(VENV) && \
		uv pip install --python $(VENV)/bin/python -r graficos/requirements.txt; \
	fi
	touch $@

graficos: $(VENV)/.instalado
	$(VENV)/bin/python graficos/graficar.py --resultados resultados --salida informe/figuras

informe:
	$(TYPST) compile informe/informe.typ informe/informe.pdf

clean:
	rm -rf $(BUILD) arboles
