idf_component_register(
    SRCS "exp_surface.c"
    INCLUDE_DIRS "include")

# The panel wants big-endian RGB565 (see exp_surface.c).
target_compile_definitions(${COMPONENT_LIB} PUBLIC EXP_SURFACE_SWAP_BYTES=1)
