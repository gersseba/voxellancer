# Platform-independent build, test and run harness.
#
# Usage (from any directory, on Windows, macOS or Linux):
#   cmake -P tools/agent-check.cmake                       # build and run all_tests
#   cmake -DMODE=Run -DRUN=StartServer -P tools/agent-check.cmake
#
# Options (pass as -D<NAME>=<value> before -P):
#   MODE       Test (default) or Run.
#   RUN        Run target: StartServer, StartClient, LocalServer, LocalContracts,
#              ShieldShowcase, AiComparison, WeaponRange, LocalLargeCombat,
#              MultiplayerLargeCombat. Defaults to StartClient for MODE=Run.
#   CONFIG     Release (default) or Debug.
#   BUILD_DIR  Build directory, relative to the repository root or absolute.
#   GENERATOR  CMake generator for a new build directory (default: CMake's).
#   VCPKG_ROOT Bootstrapped vcpkg checkout. Discovered automatically if unset.
#   SQUAD_SIZE 1-400, MultiplayerLargeCombat only.
#   ARGS       Extra ;-separated arguments for the launched game client.
#   TEST_ARGS  Extra ;-separated arguments for all_tests (e.g. --only=Shield).

cmake_minimum_required(VERSION 3.21)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(fail message)
    message(FATAL_ERROR "agent-check: ${message}")
endfunction()

function(note message)
    message(NOTICE "agent-check: ${message}")
endfunction()

function(sleep_seconds seconds)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep "${seconds}")
endfunction()

# ---------------------------------------------------------------------------
# Child launcher used by MultiplayerLargeCombat. Each process of the benchmark
# runs inside its own `cmake -P` launcher so the harness can start them
# concurrently through one execute_process pipeline on every platform.
# ---------------------------------------------------------------------------
if(DEFINED AGENT_CHECK_LAUNCH)
    if(DEFINED WAIT_FOR_LOG)
        set(waited 0)
        while(TRUE)
            if(EXISTS "${SERVER_DONE}")
                note("${AGENT_CHECK_LAUNCH} not started: the server exited early.")
                return()
            endif()
            if(EXISTS "${WAIT_FOR_LOG}")
                file(READ "${WAIT_FOR_LOG}" log)
                if(log MATCHES "Listening on")
                    break()
                endif()
            endif()
            if(waited GREATER_EQUAL 300)
                file(WRITE "${SERVER_DONE}" "client gave up waiting")
                fail("timed out waiting for the server to listen.")
            endif()
            sleep_seconds(0.1)
            math(EXPR waited "${waited} + 1")
        endwhile()
    endif()
    if(DEFINED WAIT_FOR_FILE)
        while(NOT EXISTS "${WAIT_FOR_FILE}")
            sleep_seconds(0.1)
        endwhile()
    endif()
    if(DEFINED START_DELAY)
        sleep_seconds(${START_DELAY})
    endif()
    if(DEFINED STARTED_FILE)
        file(WRITE "${STARTED_FILE}" "started")
    endif()

    set(redirect)
    if(DEFINED OUTPUT_LOG)
        list(APPEND redirect OUTPUT_FILE "${OUTPUT_LOG}")
    endif()
    if(DEFINED ERROR_LOG)
        list(APPEND redirect ERROR_FILE "${ERROR_LOG}")
    endif()
    execute_process(
        COMMAND ${LAUNCH_COMMAND}
        WORKING_DIRECTORY "${REPO_ROOT}"
        ${redirect}
        TIMEOUT ${LAUNCH_TIMEOUT}
        RESULT_VARIABLE result
    )
    if(DEFINED SERVER_DONE AND AGENT_CHECK_LAUNCH STREQUAL "server")
        file(WRITE "${SERVER_DONE}" "${result}")
    endif()
    if(NOT result STREQUAL "0")
        fail("${AGENT_CHECK_LAUNCH} exited with ${result}.")
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
if(NOT DEFINED MODE)
    set(MODE Test)
endif()
if(NOT MODE MATCHES "^(Test|Run)$")
    fail("MODE must be Test or Run.")
endif()
if(NOT DEFINED CONFIG)
    set(CONFIG Release)
endif()
if(NOT CONFIG MATCHES "^(Debug|Release)$")
    fail("CONFIG must be Debug or Release.")
endif()
set(run_targets StartServer StartClient LocalServer LocalContracts ShieldShowcase
    AiComparison WeaponRange LocalLargeCombat MultiplayerLargeCombat)
if(MODE STREQUAL "Run")
    if(NOT DEFINED RUN OR RUN STREQUAL "")
        set(RUN StartClient)
    endif()
    if(NOT RUN IN_LIST run_targets)
        string(REPLACE ";" ", " valid "${run_targets}")
        fail("RUN must be one of: ${valid}.")
    endif()
elseif(DEFINED RUN AND NOT RUN STREQUAL "")
    fail("RUN can only be used with MODE=Run.")
endif()
if(NOT DEFINED SQUAD_SIZE)
    set(SQUAD_SIZE 0)
endif()
if(NOT SQUAD_SIZE MATCHES "^[0-9]+$" OR SQUAD_SIZE GREATER 400)
    fail("SQUAD_SIZE must be between 0 and 400.")
endif()
if(SQUAD_SIZE GREATER 0 AND NOT RUN STREQUAL "MultiplayerLargeCombat")
    fail("SQUAD_SIZE is supported only by RUN=MultiplayerLargeCombat.")
endif()

# ---------------------------------------------------------------------------
# Build directory
# ---------------------------------------------------------------------------
if(NOT DEFINED BUILD_DIR OR BUILD_DIR STREQUAL "")
    if(CMAKE_HOST_APPLE)
        # Shared with the macOS local-play tasks.
        set(BUILD_DIR build/macos)
    elseif(CMAKE_HOST_WIN32)
        # Reuse an existing Visual Studio build created by earlier harnesses.
        file(GLOB existing_vs_builds LIST_DIRECTORIES TRUE
            "${REPO_ROOT}/build/agent-vcpkg-vs*-stable")
        list(SORT existing_vs_builds COMPARE NATURAL ORDER DESCENDING)
        list(LENGTH existing_vs_builds existing_vs_count)
        if(existing_vs_count GREATER 0)
            list(GET existing_vs_builds 0 BUILD_DIR)
        else()
            set(BUILD_DIR build/agent-vcpkg-stable)
        endif()
    else()
        set(BUILD_DIR build/agent-vcpkg-stable)
    endif()
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE BASE_DIR "${REPO_ROOT}")
set(cache_file "${BUILD_DIR}/CMakeCache.txt")

set(cached_toolchain "")
set(cached_generator "")
if(EXISTS "${cache_file}")
    file(STRINGS "${cache_file}" toolchain_line REGEX "^CMAKE_TOOLCHAIN_FILE:[^=]*=")
    if(toolchain_line)
        string(REGEX REPLACE "^[^=]*=" "" cached_toolchain "${toolchain_line}")
    endif()
    file(STRINGS "${cache_file}" generator_line REGEX "^CMAKE_GENERATOR:INTERNAL=")
    if(generator_line)
        string(REGEX REPLACE "^[^=]*=" "" cached_generator "${generator_line}")
    endif()
    if(cached_toolchain STREQUAL "")
        fail("${BUILD_DIR} was configured without vcpkg. Choose another BUILD_DIR.")
    endif()
    if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "" AND
        NOT GENERATOR STREQUAL cached_generator)
        fail("${BUILD_DIR} uses the '${cached_generator}' generator. Choose another BUILD_DIR.")
    endif()
endif()

# ---------------------------------------------------------------------------
# vcpkg discovery
# ---------------------------------------------------------------------------
function(vcpkg_toolchain_from_root root out)
    set(${out} "" PARENT_SCOPE)
    if(NOT root STREQUAL "" AND EXISTS "${root}/scripts/buildsystems/vcpkg.cmake")
        get_filename_component(toolchain
            "${root}/scripts/buildsystems/vcpkg.cmake" REALPATH)
        set(${out} "${toolchain}" PARENT_SCOPE)
    endif()
endfunction()

set(vcpkg_toolchain "")
set(vcpkg_source "")
if(DEFINED VCPKG_ROOT AND NOT VCPKG_ROOT STREQUAL "")
    vcpkg_toolchain_from_root("${VCPKG_ROOT}" vcpkg_toolchain)
    set(vcpkg_source "-DVCPKG_ROOT")
elseif(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    vcpkg_toolchain_from_root("$ENV{VCPKG_ROOT}" vcpkg_toolchain)
    set(vcpkg_source "the VCPKG_ROOT environment variable")
endif()
if(NOT vcpkg_source STREQUAL "" AND vcpkg_toolchain STREQUAL "")
    fail("${vcpkg_source} does not point to a bootstrapped vcpkg checkout "
        "(missing scripts/buildsystems/vcpkg.cmake).")
endif()
if(vcpkg_toolchain STREQUAL "" AND EXISTS "${cached_toolchain}")
    get_filename_component(vcpkg_toolchain "${cached_toolchain}" REALPATH)
    set(vcpkg_source "the existing ${BUILD_DIR} configuration")
endif()
if(vcpkg_toolchain STREQUAL "")
    set(candidates)
    if(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
        list(APPEND candidates "$ENV{VCPKG_INSTALLATION_ROOT}")
    endif()
    if(DEFINED ENV{HOME})
        list(APPEND candidates "$ENV{HOME}/vcpkg")
    endif()
    if(DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}" user_profile)
        list(APPEND candidates "${user_profile}/vcpkg")
    endif()
    if(CMAKE_HOST_WIN32)
        list(APPEND candidates "C:/vcpkg" "C:/src/vcpkg")
    else()
        list(APPEND candidates "/opt/vcpkg" "/usr/local/vcpkg")
    endif()
    find_program(vcpkg_executable vcpkg)
    if(vcpkg_executable)
        get_filename_component(vcpkg_executable "${vcpkg_executable}" REALPATH)
        get_filename_component(vcpkg_executable_dir "${vcpkg_executable}" DIRECTORY)
        list(APPEND candidates "${vcpkg_executable_dir}")
    endif()
    foreach(candidate IN LISTS candidates)
        vcpkg_toolchain_from_root("${candidate}" vcpkg_toolchain)
        if(NOT vcpkg_toolchain STREQUAL "")
            set(vcpkg_source "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(vcpkg_toolchain STREQUAL "")
    fail("No bootstrapped vcpkg checkout found. Clone https://github.com/microsoft/vcpkg "
        "to ~/vcpkg (or %USERPROFILE%\\vcpkg) and run its bootstrap script, or set "
        "VCPKG_ROOT / pass -DVCPKG_ROOT=<path>.")
endif()
if(NOT cached_toolchain STREQUAL "")
    get_filename_component(cached_toolchain_real "${cached_toolchain}" REALPATH)
    string(TOLOWER "${cached_toolchain_real}" cached_lower)
    string(TOLOWER "${vcpkg_toolchain}" expected_lower)
    if(NOT cached_lower STREQUAL expected_lower)
        fail("${BUILD_DIR} uses the toolchain ${cached_toolchain}, but vcpkg was "
            "found at ${vcpkg_toolchain}. Choose another BUILD_DIR.")
    endif()
endif()
note("vcpkg toolchain: ${vcpkg_toolchain} (from ${vcpkg_source})")

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
set(configure_args -S "${REPO_ROOT}" -B "${BUILD_DIR}"
    "-DCMAKE_TOOLCHAIN_FILE=${vcpkg_toolchain}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "" AND NOT EXISTS "${cache_file}")
    list(APPEND configure_args -G "${GENERATOR}")
endif()
if(CMAKE_HOST_APPLE AND NOT EXISTS "${cache_file}")
    # Apple Clang lacks OpenMP; prefer Homebrew LLVM and libomp when present.
    foreach(prefix /opt/homebrew /usr/local)
        if(NOT DEFINED ENV{CXX} AND EXISTS "${prefix}/opt/llvm/bin/clang++")
            list(APPEND configure_args
                "-DCMAKE_C_COMPILER=${prefix}/opt/llvm/bin/clang"
                "-DCMAKE_CXX_COMPILER=${prefix}/opt/llvm/bin/clang++")
            if(EXISTS "${prefix}/opt/libomp")
                list(APPEND configure_args "-DCMAKE_PREFIX_PATH=${prefix}/opt/libomp")
            endif()
            break()
        endif()
    endforeach()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${configure_args} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    fail("CMake configure failed (${result}).")
endif()

cmake_host_system_information(RESULT cpu_count QUERY NUMBER_OF_LOGICAL_CORES)
function(build_targets)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config "${CONFIG}"
            --parallel ${cpu_count} --target ${ARGN}
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        fail("Building ${ARGN} failed (${result}).")
    endif()
endfunction()

function(find_executable base_name out)
    set(name "${base_name}")
    if(CONFIG STREQUAL "Debug")
        string(APPEND name "d")
    endif()
    if(CMAKE_HOST_WIN32)
        string(APPEND name ".exe")
    endif()
    # Multi-config generators add a per-configuration directory.
    foreach(candidate "${BUILD_DIR}/${CONFIG}/${name}" "${BUILD_DIR}/${name}")
        if(EXISTS "${candidate}")
            set(${out} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    fail("Could not locate ${name} under ${BUILD_DIR}.")
endfunction()

function(run_foreground)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${REPO_ROOT}" RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        fail("'${ARGV0}' exited with ${result}.")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Test mode
# ---------------------------------------------------------------------------
if(MODE STREQUAL "Test")
    build_targets(all_tests)
    find_executable(all_tests test_executable)
    run_foreground("${test_executable}" ${TEST_ARGS})
    note("all_tests passed.")
    return()
endif()

# ---------------------------------------------------------------------------
# Run mode
# ---------------------------------------------------------------------------
if(RUN STREQUAL "StartServer")
    build_targets(voxellancer-server)
    find_executable(voxellancer-server server)
    run_foreground("${server}" --scenario contracts --listen-port 27801 --target-players 2)
    return()
endif()

build_targets(voxellancer voxellancer-server)
find_executable(voxellancer client)

if(RUN STREQUAL "StartClient")
    # Hold a per-slot lock for the client's lifetime so a second concurrent
    # client picks the next benchmark identity on every platform.
    foreach(slot client-1 client-2)
        file(LOCK "${BUILD_DIR}/agent-check-${slot}.lock" GUARD PROCESS
            TIMEOUT 0 RESULT_VARIABLE lock_result)
        if(lock_result STREQUAL "0")
            run_foreground("${client}" -multiplayer-client -benchmark-client-id ${slot} ${ARGS})
            return()
        endif()
    endforeach()
    fail("Two clients are already running from ${BUILD_DIR}.")
elseif(RUN STREQUAL "LocalServer")
    run_foreground("${client}" -fullscreen -local-singleplayer ${ARGS})
    return()
elseif(RUN STREQUAL "LocalContracts")
    run_foreground("${client}" -fullscreen -local-singleplayer -scenario contracts -nointro ${ARGS})
    return()
elseif(RUN STREQUAL "ShieldShowcase")
    run_foreground("${client}" -scenario shield-showcase ${ARGS})
    return()
elseif(RUN STREQUAL "AiComparison")
    run_foreground("${client}" -scenario ai-comparison -nointro ${ARGS})
    return()
elseif(RUN STREQUAL "WeaponRange")
    run_foreground("${client}" -scenario weapon-range -nointro ${ARGS})
    return()
endif()

string(TIMESTAMP run_id "%s" UTC)
string(TIMESTAMP run_millis "%f" UTC)
string(SUBSTRING "${run_millis}" 0 3 run_millis)
string(APPEND run_id "${run_millis}")
if(SQUAD_SIZE GREATER 0)
    string(APPEND run_id "-squad-${SQUAD_SIZE}")
endif()
set(report_dir "${REPO_ROOT}/benchmark-results")

if(RUN STREQUAL "LocalLargeCombat")
    run_foreground("${client}" -local-singleplayer -benchmark-large-combat
        -benchmark-run-id ${run_id} -benchmark-client-id integrated-client ${ARGS})
    return()
endif()

# MultiplayerLargeCombat: one server waiting for two clients, all started
# concurrently through launchers; clients wait until the server listens.
find_executable(voxellancer-server server)
set(work_dir "${BUILD_DIR}/agent-check-${run_id}")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")
set(server_log "${work_dir}/server.log")
set(server_error_log "${work_dir}/server-error.log")
set(server_done "${work_dir}/server.done")
set(server_report "${report_dir}/large-combat-${run_id}-server.json")
set(server_args --scenario benchmark --listen-port 27801 --target-players 2
    --ships-per-team 400 --benchmark-ticks 1800 --benchmark-run-id ${run_id})
if(SQUAD_SIZE GREATER 0)
    list(APPEND server_args --squad-size ${SQUAD_SIZE})
endif()

set(launcher "${CMAKE_CURRENT_LIST_FILE}")
function(launcher_command out role)
    string(REPLACE ";" "\\;" encoded "${ARGN}")
    set(${out} "${CMAKE_COMMAND}" "-DAGENT_CHECK_LAUNCH=${role}"
        "-DLAUNCH_COMMAND=${encoded}" "-DSERVER_DONE=${server_done}"
        ${launcher_options} -P "${launcher}" PARENT_SCOPE)
endfunction()

set(launcher_options "-DLAUNCH_TIMEOUT=420"
    "-DOUTPUT_LOG=${server_log}" "-DERROR_LOG=${server_error_log}")
launcher_command(server_command server "${server}" ${server_args})
set(client_commands)
set(previous_started "")
foreach(client_id client-1 client-2)
    set(launcher_options "-DLAUNCH_TIMEOUT=330" "-DWAIT_FOR_LOG=${server_log}"
        "-DSTARTED_FILE=${work_dir}/${client_id}.started"
        "-DOUTPUT_LOG=${work_dir}/${client_id}.log"
        "-DERROR_LOG=${work_dir}/${client_id}-error.log")
    if(NOT previous_started STREQUAL "")
        # Stagger window and GL context creation.
        list(APPEND launcher_options "-DWAIT_FOR_FILE=${previous_started}" "-DSTART_DELAY=3")
    endif()
    launcher_command(command_${client_id} ${client_id} "${client}"
        -multiplayer-client -benchmark-large-combat -benchmark-run-id ${run_id}
        -benchmark-client-id ${client_id} ${ARGS})
    set(previous_started "${work_dir}/${client_id}.started")
endforeach()

note("Starting large-combat benchmark run ${run_id}.")
execute_process(
    COMMAND ${server_command}
    COMMAND ${command_client-1}
    COMMAND ${command_client-2}
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULTS_VARIABLE results
)

function(print_log_tail path)
    if(EXISTS "${path}")
        file(STRINGS "${path}" lines)
        list(LENGTH lines count)
        if(count GREATER 30)
            math(EXPR first "${count} - 30")
            list(SUBLIST lines ${first} 30 lines)
        endif()
        string(REPLACE ";" "\n" text "${lines}")
        note("${path}:\n${text}")
    endif()
endfunction()

set(failure "")
list(GET results 0 server_result)
list(GET results 1 client1_result)
list(GET results 2 client2_result)
if(NOT client1_result STREQUAL "0" OR NOT client2_result STREQUAL "0")
    set(failure "A large-combat client failed (${client1_result}, ${client2_result}).")
elseif(NOT server_result STREQUAL "0")
    set(failure "The large-combat server failed (${server_result}).")
elseif(NOT EXISTS "${server_report}")
    set(failure "Expected server report was not written: ${server_report}")
else()
    file(READ "${server_report}" report)
    string(JSON report_run_id ERROR_VARIABLE json_error GET "${report}" run_id)
    string(JSON report_role ERROR_VARIABLE json_error GET "${report}" role)
    string(JSON report_players ERROR_VARIABLE json_error GET "${report}" target_players)
    string(JSON report_target ERROR_VARIABLE json_error GET "${report}" target_simulation_intervals)
    string(JSON report_measured ERROR_VARIABLE json_error GET "${report}" measured_simulation_intervals)
    string(JSON report_squad ERROR_VARIABLE json_error GET "${report}" squad_size)
    if(json_error OR NOT report_run_id STREQUAL run_id OR NOT report_role STREQUAL "server" OR
        NOT report_players EQUAL 2 OR NOT report_target EQUAL 1800 OR
        NOT report_measured EQUAL 1800 OR NOT report_squad EQUAL SQUAD_SIZE)
        set(failure "Server report is incomplete: ${server_report}")
    endif()
    foreach(client_id client-1 client-2)
        if(NOT EXISTS "${report_dir}/large-combat-${run_id}-${client_id}.json")
            set(failure "Expected ${client_id} report was not written.")
        endif()
    endforeach()
endif()

if(NOT failure STREQUAL "")
    foreach(log server.log server-error.log client-1-error.log client-2-error.log)
        print_log_tail("${work_dir}/${log}")
    endforeach()
    fail("${failure}")
endif()
file(REMOVE_RECURSE "${work_dir}")
note("Large-combat benchmark complete (run ${run_id}).")
note("Server report: ${server_report}")
note("Client reports: ${report_dir}/large-combat-${run_id}-client-1.json, "
    "${report_dir}/large-combat-${run_id}-client-2.json")
