.RECIPEPREFIX := >

CXX := g++

# Banderas GTK4 y Cairo obtenidas mediante pkg-config
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 tinyxml2 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk4 tinyxml2 2>/dev/null)

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic $(GTK_CFLAGS)
LDLIBS   := $(GTK_LIBS)

TARGET := GerberViwer
BUILD  := obj

# Todos los .cpp del proyecto donde esten (2 niveles: raiz + carpetas)
ALL_CPP := $(shell find . -maxdepth 2 -name '*.cpp' -not -path './$(BUILD)/*' | sort)

# Excluidos: visores antiguos (tienen main() y rompen el enlazado)
EXCLUDES := %/GerberViwer.cpp %/GerberViwer1.cpp %/GerberViwer2.cpp

SOURCES := $(filter-out $(EXCLUDES),$(ALL_CPP))
OBJECTS := $(patsubst ./%.cpp,$(BUILD)/%.o,$(SOURCES))
DEPENDS := $(OBJECTS:.o=.d)

.PHONY: all list run clean rebuild check

all: $(TARGET)

$(TARGET): $(OBJECTS)
>$(CXX) $(OBJECTS) -o $@ $(LDLIBS)

$(BUILD)/%.o: ./%.cpp
>@mkdir -p $(dir $@) && $(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Comprueba duplicados (dos ficheros con el mismo nombre en carpetas
# distintas romperian el enlazado por simbolos repetidos)
list:
>@echo "Fuentes que se compilaran:"; for s in $(SOURCES); do echo "  $$s"; done
>@ls $(SOURCES) | xargs -n1 basename | sort | uniq -d

run: $(TARGET)
>./$(TARGET)

clean:
>rm -rf $(BUILD) $(TARGET)

rebuild: clean all

check:
>@pkg-config --exists gtk4 && echo "GTK4: disponible" || echo "GTK4: no disponible"

-include $(DEPENDS)
