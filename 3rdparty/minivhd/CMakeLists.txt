add_library(minivhd STATIC src/create.c src/manage.c src/io.c src/struct_rw.c src/util.c)
target_include_directories(minivhd PUBLIC src)
target_compile_definitions(minivhd PRIVATE _FILE_OFFSET_BITS=64)
