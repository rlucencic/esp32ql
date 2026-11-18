# Sinclair QL machine (platform independent; host build in host/)
idf_component_register(SRCS "ql.c"
                            "mem.c"
                            "io.c"
                            "ipc.c"
                            "mdv.c"
                            "flp.c"
                            "video.c"
                            "sound.c"
                            "beep.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES musashi)

# Runs together with the CPU core in the emulation loop
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
