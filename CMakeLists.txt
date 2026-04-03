cmake_minimum_required(VERSION 3.16)
project(mod_forkzstream
        VERSION 1.0.0
        DESCRIPTION "tts stream mod for FreeSWITCH."
        HOMEPAGE_URL "https://github.com/zhaomingwork/mod_forkzstream")

#include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 14)


set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")

 
set(ENV{PKG_CONFIG_PATH} "/usr/local/freeswitch/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

if(CMAKE_SYSTEM_VERSION VERSION_LESS "2.38")
    add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GNU_SOURCE")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


include(FetchContent)
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/websocket )
    FetchContent_Declare(websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
      GIT_TAG 0.8.2
      SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/websocket
      )
    
    FetchContent_MakeAvailable(websocketpp)
endif()

include_directories(${PROJECT_SOURCE_DIR}/third_party/websocket)
   
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/asio )
    FetchContent_Declare(asio
      URL   https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-24-0.tar.gz
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/asio
    )
    
    FetchContent_MakeAvailable(asio)
  endif()
  include_directories(${PROJECT_SOURCE_DIR}/third_party/asio/asio/include)
 
  if(NOT EXISTS ${PROJECT_SOURCE_DIR}/third_party/json )
    FetchContent_Declare(json
      URL   https://github.com/nlohmann/json/archive/refs/tags/v3.11.2.tar.gz
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/json
    )
    
    FetchContent_MakeAvailable(json)
endif()
include_directories(${PROJECT_SOURCE_DIR}/third_party/json/include)

find_package(PkgConfig REQUIRED)
 

pkg_check_modules(FreeSWITCH REQUIRED IMPORTED_TARGET freeswitch)
pkg_get_variable(FS_MOD_DIR freeswitch modulesdir)
 
message(STATUS "FreeSWITCH modules dir: ${FS_MOD_DIR}")

 
 
 

add_library(mod_forkzstream SHARED mod_forkzstream.c forkzstream.cpp HandlerFactory.cpp)

set_property(TARGET mod_forkzstream PROPERTY POSITION_INDEPENDENT_CODE ON)

#set_target_properties()
 
target_link_libraries(mod_forkzstream PRIVATE    curl  ssl crypto PkgConfig::FreeSWITCH pthread)


#set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, openssl, libfreeswitch1")
set(CPACK_PACKAGE_NAME "mod-forkzstream")
set(CMAKE_INSTALL_DOCDIR "share/doc/${CPACK_PACKAGE_NAME}")


 

install(TARGETS ${PROJECT_NAME}
        COMPONENT ${PROJECT_NAME}
        DESTINATION ${FS_MOD_DIR})

 

message(STATUS "Components to pack: ${CPACK_COMPONENTS_ALL}")
