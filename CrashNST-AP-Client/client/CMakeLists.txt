cmake_minimum_required(VERSION 3.24)
project(CrashNSTClient LANGUAGES CXX)

set(APCPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../references/APCpp" CACHE PATH "APCpp checkout")

add_subdirectory("${APCPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/apcpp" EXCLUDE_FROM_ALL)
include(mbedtls_write_fix.cmake)
if(MINGW)
    # mbedTLS 3.6.4's MinGW debug printf specifier triggers GCC 13's format warning.
    target_compile_options(mbedtls PRIVATE -Wno-error=format)
endif()
add_library(crash_nst_proxy SHARED dinput_proxy.cpp dinput_proxy.def)
set_target_properties(crash_nst_proxy PROPERTIES PREFIX "" OUTPUT_NAME "dinput8" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proxy")
target_compile_features(crash_nst_proxy PRIVATE cxx_std_17)
if(MINGW)
    target_link_options(crash_nst_proxy PRIVATE -static -static-libstdc++ -static-libgcc)
endif()
set(NST_RUNTIME_SOURCES ap_client.cpp client_connection.cpp client_display.cpp client_progress.cpp client_gameplay.cpp client_campaigns.cpp seed_config.cpp function_hook.cpp game_pickups.cpp save_format.cpp game_memory.cpp game_hooks.cpp session.cpp cloud_saves.cpp ingame_bridge.cpp check_progress.cpp collectibles.cpp console.cpp ap_console.cpp game_runtime.cpp game_sound.cpp game_archive.cpp)
# Compile APCpp into standalone DLL
file(READ "${APCPP_SOURCE_DIR}/Archipelago.cpp" NST_APCPP_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APCPP_SOURCE_DIR}/Archipelago.cpp")
# close frames carry their reason in closeInfo, not the connection-error structure
set(NST_DISCONNECT_REASON "packet[0][\"reason\"] = msg->errorInfo.reason;")
string(FIND "${NST_APCPP_SOURCE}" "${NST_DISCONNECT_REASON}" NST_DISCONNECT_OFFSET)
if(NST_DISCONNECT_OFFSET EQUAL -1)
    message(FATAL_ERROR "APCpp disconnect callback differs from the supported revision")
endif()
string(REPLACE "${NST_DISCONNECT_REASON}"
    "packet[0][\"reason\"] = msg->type == ix::WebSocketMessageType::Close ? msg->closeInfo.reason : msg->errorInfo.reason;\n                packet[0][\"close_code\"] = msg->type == ix::WebSocketMessageType::Close ? msg->closeInfo.code : 0;\n                packet[0][\"remote\"] = msg->type == ix::WebSocketMessageType::Close && msg->closeInfo.remote;"
    NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
# stop failed connection attempts in the transport thread before a sixth attempt starts
string(REPLACE "webSocket.setMaxWaitBetweenReconnectionRetries(1000);"
    "webSocket.enableAutomaticReconnection();\n    webSocket.setMaxWaitBetweenReconnectionRetries(1000);"
    NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "packet[0][\"fallback_pending\"] = fallback;"
    "const bool retry_limit = msg->type == ix::WebSocketMessageType::Error && msg->errorInfo.retries >= 5;\n                packet[0][\"fallback_pending\"] = fallback && !retry_limit;\n                packet[0][\"retry_limit\"] = retry_limit;\n                if (retry_limit) webSocket.disableAutomaticReconnection();"
    NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "if (fallback) {" "if (fallback && !retry_limit) {" NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "printf(\"AP: Error connecting to Archipelago. Retries: %d\\n\", msg->errorInfo.retries-1);"
    "if (msg->type == ix::WebSocketMessageType::Error) printf(\"AP: Connection attempt failed: %s (retry %d).\\n\", msg->errorInfo.reason.c_str(), msg->errorInfo.retries);"
    NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "std::string const datapkg_cache_path = \"APCpp_datapkg.cache\";" "std::string nst_cache_path();" NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "datapkg_cache_path" "nst_cache_path()" NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "command != \"RoomInfo\" &&" "command != \"InvalidPacket\" && command != \"RoomInfo\" &&"
    NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(REPLACE "printf(" "nst_ap_printf(" NST_APCPP_SOURCE "${NST_APCPP_SOURCE}")
string(PREPEND NST_APCPP_SOURCE "int nst_ap_printf(const char*, ...);\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nst_archipelago.cpp" "${NST_APCPP_SOURCE}")
find_package(LibLZMA 5.8.4 REQUIRED)
add_library(crash_nst_ingame SHARED ingame.cpp game_font.cpp standalone.cpp ${NST_RUNTIME_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/nst_archipelago.cpp")
set_target_properties(crash_nst_ingame PROPERTIES PREFIX "")
target_compile_features(crash_nst_ingame PRIVATE cxx_std_17)
target_include_directories(crash_nst_ingame PRIVATE "${APCPP_SOURCE_DIR}" "${APCPP_SOURCE_DIR}/jsoncpp/include" "${APCPP_SOURCE_DIR}/IXWebSocket")
target_compile_definitions(crash_nst_ingame PRIVATE LZMA_API_STATIC=1)
target_link_libraries(crash_nst_ingame PRIVATE jsoncpp_static ixwebsocket LibLZMA::LibLZMA d3d11 dxgi d3dcompiler dinput8 dxguid user32 gdi32 psapi advapi32 crypt32 ws2_32 winmm)
if(MINGW)
    target_link_options(crash_nst_ingame PRIVATE -static -static-libstdc++ -static-libgcc)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../local/client-tests.cmake" OPTIONAL)
