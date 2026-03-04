function(compile_proto)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # GP2040-CE path should be set in parent CMakeLists.txt
    # set(GP2040_CE_PATH ${CMAKE_SOURCE_DIR}/GP2040-CE)

    set(VENV ${CMAKE_CURRENT_BINARY_DIR}/venv)
    set(VENV_FILE ${VENV}/environment.txt)

    if(CMAKE_HOST_WIN32)
        set(VENV_BIN_DIR ${VENV}/Scripts)
    else()
        set(VENV_BIN_DIR ${VENV}/bin)
    endif()

    add_custom_command(
        DEPENDS ${GP2040_CE_PATH}/lib/nanopb/extra/requirements.txt
        COMMAND ${Python3_EXECUTABLE} -m venv ${VENV}
        # Install dependencies in specific order to avoid conflicts
        COMMAND ${VENV_BIN_DIR}/pip install --upgrade 'protobuf>=3.19'
        COMMAND ${VENV_BIN_DIR}/pip install --upgrade 'grpcio-tools==1.71.0'
        COMMAND ${VENV_BIN_DIR}/pip install --upgrade 'setuptools<81'
        COMMAND ${VENV_BIN_DIR}/pip freeze > ${VENV_FILE}
        OUTPUT ${VENV_FILE}
        COMMENT "Setting up Python Virtual Environment"
    )

    set(NANOPB_GENERATOR ${GP2040_CE_PATH}/lib/nanopb/generator/nanopb_generator.py)
    set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
    set(PROTO_OUTPUT_DIR ${PROTO_OUTPUT_DIR} PARENT_SCOPE)

    add_custom_command(
        DEPENDS ${VENV_FILE} ${NANOPB_GENERATOR} ${GP2040_CE_PATH}/proto/enums.proto ${GP2040_CE_PATH}/proto/config.proto ${GP2040_CE_PATH}/lib/nanopb/generator/proto/nanopb.proto
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUTPUT_DIR}
        COMMAND ${VENV_BIN_DIR}/python ${NANOPB_GENERATOR}
            -q
            -D ${PROTO_OUTPUT_DIR}
            -I ${GP2040_CE_PATH}/proto
            -I ${GP2040_CE_PATH}/lib/nanopb/generator/proto
            ${GP2040_CE_PATH}/proto/enums.proto
        COMMAND ${VENV_BIN_DIR}/python ${NANOPB_GENERATOR}
            -q
            -D ${PROTO_OUTPUT_DIR}
            -I ${GP2040_CE_PATH}/proto
            -I ${GP2040_CE_PATH}/lib/nanopb/generator/proto
            ${GP2040_CE_PATH}/proto/config.proto
        OUTPUT ${PROTO_OUTPUT_DIR}/config.pb.c ${PROTO_OUTPUT_DIR}/config.pb.h ${PROTO_OUTPUT_DIR}/enums.pb.c ${PROTO_OUTPUT_DIR}/enums.pb.h
        COMMENT "Compiling enums.proto and config.proto"
    )

    # Create a custom target to ensure proto files are generated before building
    add_custom_target(proto_gen ALL
        DEPENDS ${PROTO_OUTPUT_DIR}/config.pb.c ${PROTO_OUTPUT_DIR}/config.pb.h ${PROTO_OUTPUT_DIR}/enums.pb.c ${PROTO_OUTPUT_DIR}/enums.pb.h
    )

    # Export proto sources to parent scope
    set(PROTO_SOURCES
        ${PROTO_OUTPUT_DIR}/config.pb.c
        ${PROTO_OUTPUT_DIR}/enums.pb.c
        PARENT_SCOPE
    )
endfunction()
