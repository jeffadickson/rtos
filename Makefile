# Compatibility entry points; CMake is the canonical build system.
.PHONY: all test docs clean
all:
	cmake --preset riscue $(if $(CROSS),-DRISCV_TOOLCHAIN_PREFIX=$(CROSS),)
	cmake --build --preset riscue
test:
	cmake --preset host
	cmake --build --preset host
	ctest --preset host
docs:
	cmake --preset host
	cmake --build --preset host --target docs
clean:
	cmake --build --preset host --target clean
