cmake_minimum_required(VERSION 3.16)

# 1. Carica il toolchain PRIMA del progetto!
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaCordEngine)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include) 
link_directories(${CMAKE_SOURCE_DIR}/lib)
# ---------------------

# 2. I flag magici dell'esempio (con mword-relocations per GCC 15)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -fvisibility=hidden -O2 -fno-inline -fno-builtin -mword-relocations")

add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME}
  SceLibKernel_stub
  SceIofilemgr_stub   # <--- LA LIBRERIA MANCANTE!
  SceAudioIn_stub
  SceSysmodule_stub
  SceKernelThreadMgr_stub
  SceSysmem_stub
  SceVoice_stub_weak
  SceCtrl_stub
  SceAudio_stub
  SceNet_stub_weak      # <-- MAGIA! Evita il crash al caricamento
  SceNetCtl_stub_weak
  SceLibc_stub
)

# 3. Nessuna libreria standard!
set_target_properties(${PROJECT_NAME} PROPERTIES 
  LINK_FLAGS "-nostdlib"
)

# 4. Strip e Ottimizzazione Nativi!
vita_create_self(${PROJECT_NAME}.suprx ${PROJECT_NAME}
  STRIPPED
  REL_OPTIMIZE
  MODULE_ENTRY module_start,module_stop,module_exit
)
