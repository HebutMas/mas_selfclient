# Shared third-party dependency resolution for the RoboMaster custom client
# and the protocol simulator.
#
# Deps live in <repo>/third_party (see third_party/setup-deps.sh):
#   protobuf3196/                      built from source: bin/protoc, include/, lib64/
#   paho/                              Eclipse Paho C + C++ (built from source)
#
# No system libraries are required: everything is vendored under third_party/.
# Override DEPS_ROOT to point elsewhere.

if(NOT DEFINED DEPS_ROOT)
  get_filename_component(_rm_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  set(DEPS_ROOT "${_rm_repo_root}/third_party")
else()
  get_filename_component(_rm_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(RM_PROTO_DIR      "${_rm_repo_root}/proto")

# Vendored libs install under lib64 on Linux (setup-deps.sh forces it) and lib
# under MSYS2/MinGW. protoc gains a .exe suffix on Windows.
if(WIN32)
  set(_RM_LIBDIR "lib")
  set(RM_PROTOC "${DEPS_ROOT}/protobuf3196/bin/protoc.exe")
else()
  set(_RM_LIBDIR "lib64")
  set(RM_PROTOC "${DEPS_ROOT}/protobuf3196/bin/protoc")
endif()

set(RM_PROTOBUF_INC   "${DEPS_ROOT}/protobuf3196/include")

find_library(RM_PROTOBUF_LIB NAMES protobuf
  PATHS "${DEPS_ROOT}/protobuf3196/${_RM_LIBDIR}" NO_DEFAULT_PATH)
get_filename_component(RM_PROTOBUF_LIB_DIR "${RM_PROTOBUF_LIB}" DIRECTORY)

set(RM_PAHO_INC       "${DEPS_ROOT}/paho/include")
set(RM_PAHO_LIB_DIR   "${DEPS_ROOT}/paho/${_RM_LIBDIR}")
# Upstream keeps the "-static" suffix on Windows (it only strips it on *nix),
# so accept both archive names.
find_library(RM_PAHO_CPP_LIB NAMES paho-mqttpp3 paho-mqttpp3-static
  PATHS "${RM_PAHO_LIB_DIR}" NO_DEFAULT_PATH)
find_library(RM_PAHO_C_LIB NAMES paho-mqtt3a paho-mqtt3a-static
  PATHS "${RM_PAHO_LIB_DIR}" NO_DEFAULT_PATH)

foreach(_v RM_PROTOBUF_LIB RM_PAHO_CPP_LIB RM_PAHO_C_LIB)
  if(NOT ${_v})
    message(FATAL_ERROR "RM deps not found (${_v}). Run third_party/setup-deps.sh first, "
                        "or set DEPS_ROOT.")
  endif()
endforeach()

# Compile proto/robomaster.proto into the given target and link protobuf.
# Usage: rm_add_proto(<target>)
function(rm_add_proto target)
  set(_gen "${CMAKE_CURRENT_BINARY_DIR}/gen")
  add_custom_command(
    OUTPUT "${_gen}/robomaster.pb.cc" "${_gen}/robomaster.pb.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_gen}"
    COMMAND "${RM_PROTOC}" --cpp_out="${_gen}" -I"${RM_PROTO_DIR}"
            "${RM_PROTO_DIR}/robomaster.proto"
    DEPENDS "${RM_PROTO_DIR}/robomaster.proto"
    COMMENT "protoc robomaster.proto")
  target_sources(${target} PRIVATE "${_gen}/robomaster.pb.cc")
  target_include_directories(${target} PRIVATE "${_gen}" "${RM_PROTOBUF_INC}")
  target_link_libraries(${target} PRIVATE "${RM_PROTOBUF_LIB}")
endfunction()

# Link Eclipse Paho MQTT C++ against the given target.
function(rm_link_paho target)
  target_include_directories(${target} PRIVATE "${RM_PAHO_INC}")
  target_link_libraries(${target} PRIVATE "${RM_PAHO_CPP_LIB}" "${RM_PAHO_C_LIB}")
  if(WIN32)
    # Paho is static on Windows: Paho C++ headers otherwise expand to
    # __declspec(dllimport) (see the generated mqtt/export.h).
    target_compile_definitions(${target} PRIVATE PAHO_MQTTPP_STATIC_DEFINE)
    # Static Paho C needs Winsock (ws2_32/iphlpapi), RPC (UuidCreate) and
    # CryptoAPI (CryptStringToBinaryA) at link time.
    target_link_libraries(${target} PRIVATE ws2_32 iphlpapi rpcrt4 crypt32)
  endif()
endfunction()

# Link the slim static FFmpeg (see third_party/build-ffmpeg.sh) against a target.
# Software HEVC/H.264 decode + VAAPI (AMD/Intel) + NVDEC/NVENC (NVIDIA).
set(RM_FFMPEG_DIR "${DEPS_ROOT}/ffmpeg")
find_library(RM_AVCODEC_LIB NAMES avcodec PATHS "${RM_FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(RM_AVUTIL_LIB  NAMES avutil  PATHS "${RM_FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(RM_SWSCALE_LIB NAMES swscale PATHS "${RM_FFMPEG_DIR}/lib" NO_DEFAULT_PATH)

function(rm_link_ffmpeg target)
  foreach(_v RM_AVCODEC_LIB RM_AVUTIL_LIB RM_SWSCALE_LIB)
    if(NOT ${_v})
      message(FATAL_ERROR "FFmpeg not found (${_v}). Run third_party/setup-deps.sh first.")
    endif()
  endforeach()
  target_include_directories(${target} PRIVATE "${RM_FFMPEG_DIR}/include")
  # Static archives: wrap in a group so inter-library references resolve.
  target_link_libraries(${target} PRIVATE
    -Wl,--start-group "${RM_AVCODEC_LIB}" "${RM_SWSCALE_LIB}" "${RM_AVUTIL_LIB}" -Wl,--end-group)
  if(WIN32)
    # MinGW: NVDEC/NVENC dlopen their runtime libraries (no VAAPI on Windows).
    # Static libavutil uses the Windows BCrypt RNG in av_random_bytes.
    target_link_libraries(${target} PRIVATE bcrypt)
    return()
  endif()
  # VAAPI (AMD/Intel) links libva; NVDEC/NVENC dlopen their runtime libraries.
  set(_va_dir "${DEPS_ROOT}/ffmpeg-deps/lib")
  if(EXISTS "${_va_dir}/libva.so")
    target_link_directories(${target} PRIVATE "${_va_dir}")
    target_link_libraries(${target} PRIVATE va va-drm drm)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(_RM_VA QUIET libva-drm)
      if(_RM_VA_FOUND)
        target_include_directories(${target} PRIVATE ${_RM_VA_INCLUDE_DIRS})
        target_link_directories(${target} PRIVATE ${_RM_VA_LIBRARY_DIRS})
        target_link_libraries(${target} PRIVATE ${_RM_VA_LIBRARIES})
      endif()
    endif()
  endif()
  target_link_libraries(${target} PRIVATE m pthread dl)
endfunction()
