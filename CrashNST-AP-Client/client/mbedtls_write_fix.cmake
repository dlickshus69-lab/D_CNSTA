# keep mbedTLS retry arguments stable when IXWebSocket appends more queued frames
get_target_property(NST_SOCKET_SOURCES ixwebsocket SOURCES)
if("ixwebsocket/IXSocketMbedTLS.cpp" IN_LIST NST_SOCKET_SOURCES)
    set(NST_TLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/tls")
    set(NST_SOCKET_DIR "${APCPP_SOURCE_DIR}/IXWebSocket/ixwebsocket")
    file(MAKE_DIRECTORY "${NST_TLS_DIR}")
    file(READ "${NST_SOCKET_DIR}/IXSocketMbedTLS.h" NST_TLS_HEADER)
    file(READ "${NST_SOCKET_DIR}/IXSocketMbedTLS.cpp" NST_TLS_SOURCE)
    set(NST_TLS_WRITE "ssize_t res = mbedtls_ssl_write(&_ssl, (unsigned char*) buf, nbyte);")
    string(FIND "${NST_TLS_SOURCE}" "${NST_TLS_WRITE}" NST_TLS_OFFSET)
    if(NST_TLS_OFFSET EQUAL -1 OR NOT NST_TLS_HEADER MATCHES "std::mutex _mutex;")
        message(FATAL_ERROR "IXWebSocket mbedTLS adapter differs from the supported revision")
    endif()
    string(REPLACE "std::mutex _mutex;" "std::mutex _mutex;\n        std::string _pendingWrite;"
        NST_TLS_HEADER "${NST_TLS_HEADER}")
    string(REPLACE "mbedtls_ssl_free(&_ssl);" "_pendingWrite.clear();\n        mbedtls_ssl_free(&_ssl);"
        NST_TLS_SOURCE "${NST_TLS_SOURCE}")
    string(REPLACE "${NST_TLS_WRITE}" [=[// Retain one TLS record across retries, including its address and length.
        if (_pendingWrite.empty())
            _pendingWrite.assign(buf, nbyte < 16384 ? nbyte : 16384);
        ssize_t res = mbedtls_ssl_write(&_ssl, (const unsigned char*) _pendingWrite.data(), _pendingWrite.size());
        if (res != MBEDTLS_ERR_SSL_WANT_READ && res != MBEDTLS_ERR_SSL_WANT_WRITE)
            _pendingWrite.clear();]=] NST_TLS_SOURCE "${NST_TLS_SOURCE}")
    file(WRITE "${NST_TLS_DIR}/IXSocketMbedTLS.h" "${NST_TLS_HEADER}")
    file(WRITE "${NST_TLS_DIR}/IXSocketMbedTLS.cpp" "${NST_TLS_SOURCE}")
    # the factory must see the same class layout as the patched adapter
    configure_file("${NST_SOCKET_DIR}/IXSocketFactory.cpp" "${NST_TLS_DIR}/IXSocketFactory.cpp" COPYONLY)
    foreach(NST_SOCKET_FILE IXSocketMbedTLS.cpp IXSocketFactory.cpp)
        list(REMOVE_ITEM NST_SOCKET_SOURCES "ixwebsocket/${NST_SOCKET_FILE}")
        list(APPEND NST_SOCKET_SOURCES "${NST_TLS_DIR}/${NST_SOCKET_FILE}")
    endforeach()
    set_property(TARGET ixwebsocket PROPERTY SOURCES "${NST_SOCKET_SOURCES}")
    target_include_directories(ixwebsocket PRIVATE "${NST_SOCKET_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${NST_SOCKET_DIR}/IXSocketMbedTLS.cpp" "${NST_SOCKET_DIR}/IXSocketMbedTLS.h"
        "${NST_SOCKET_DIR}/IXSocketFactory.cpp")
endif()
