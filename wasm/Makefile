SOURCES := ../deci.c wrapper.c
HEADERS := ../deci.h ../deci_quad_arith.h
OUTPUT := deci.wasm

CLANG := clang

$(OUTPUT): $(SOURCES) $(HEADERS)
	$(CLANG) -O3 -flto --target=wasm32-unknown-wasi -nostdlib -Wl,--no-entry -Wl,--export-all -Wl,--lto-O3 -o $(OUTPUT) $(SOURCES)
