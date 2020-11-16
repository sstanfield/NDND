cmake_minimum_required(VERSION 3.10)

project(ahndn VERSION 0.1)
add_subdirectory(src)

# get all project source files
file(GLOB_RECURSE ALL_SOURCE_FILES src/*.c src/*.cpp src/*.h src/*.hpp)
file(GLOB_RECURSE ALL_CPP_FILES src/*.cpp)

# additional target to perform clang-format run, requires clang-format
add_custom_target(
        fmt
        COMMAND /usr/bin/clang-format
        --verbose
        -i
        ${ALL_SOURCE_FILES}
)

# additional target to error out if formatting is needed, requires clang-format
add_custom_target(
        check-fmt
        COMMAND clang-format
        --verbose
        --dry-run
        --Werror
        ${ALL_SOURCE_FILES}
)

# additional target for style checks, requires clang-tidy
add_custom_target(
        style
        COMMAND clang-tidy
        -checks='-*,readability-identifier-naming'
        -config='{CheckOptions: [
        { key: readability-identifier-naming.NamespaceCase, value: lower_case },
        { key: readability-identifier-naming.ClassCase, value: CamelCase  },
        { key: readability-identifier-naming.StructCase, value: CamelCase  },
        { key: readability-identifier-naming.FunctionCase, value: camelBack },
        { key: readability-identifier-naming.VariableCase, value: lower_case },
        { key: readability-identifier-naming.GlobalConstantCase, value: UPPER_CASE }
        ]}'
        -header-filter='.*'
        ${ALL_CPP_FILES}
        -- ${CMAKE_CXX_FLAGS} -std=c++14
)

# additional target for lints, requires clang-tidy
add_custom_target(
        tidy
        COMMAND clang-tidy
        -checks='-*,modernize-*,readability-*,clang-analyzer-*,cppcoreguidelines-*,performance-*'
        -config='{CheckOptions: [
        { key: readability-identifier-naming.NamespaceCase, value: lower_case },
        { key: readability-identifier-naming.ClassCase, value: CamelCase  },
        { key: readability-identifier-naming.StructCase, value: CamelCase  },
        { key: readability-identifier-naming.FunctionCase, value: camelBack },
        { key: readability-identifier-naming.VariableCase, value: lower_case },
        { key: readability-identifier-naming.GlobalConstantCase, value: UPPER_CASE }
        ]}'
        -header-filter='.*'
        ${ALL_CPP_FILES}
        -- ${CMAKE_CXX_FLAGS} -std=c++14
)

# additional target for ALL lints, requires clang-tidy
# this is not really useful except to see what checks may be there (it will lead
# to a ton of sometimes contradictory warnings).
add_custom_target(
        tidy-all
        COMMAND clang-tidy
        -checks='*'
        -header-filter='.*'
        ${ALL_CPP_FILES}
        -- ${CMAKE_CXX_FLAGS} -std=c++14
)
