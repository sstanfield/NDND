# specify the C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(-Wall -Werror)

#find_library(LIBNDN NAMES libndn-cxx PATHS /usr/local/lib/pkgconfig REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules (LIBNDN REQUIRED IMPORTED_TARGET libndn-cxx)
add_executable(ahndn nd-client.cpp ahclient.cpp multicast.cpp)
target_link_libraries(ahndn PUBLIC PkgConfig::LIBNDN)