cmake_minimum_required(VERSION 3.20)
project(protoPython VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-fno-delete-null-pointer-checks)

include(GNUInstallDirs)

# --- protoCore: use installed package or build from sibling directory ---
# For packaging: set PROTO_CORE_PREFIX to the install prefix of protoCore (e.g. /usr/local or $HOME/.local).
# For development: leave unset to build protoCore from ../protoCore.
if(DEFINED PROTO_CORE_PREFIX)
    find_library(PROTO_CORE_LIBRARY protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTO_CORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include)
    if(PROTO_CORE_LIBRARY AND PROTO_CORE_INCLUDE_DIR)
        add_library(protoCore SHARED IMPORTED GLOBAL)
        set_target_properties(protoCore PROPERTIES
            IMPORTED_LOCATION "${PROTO_CORE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROTO_CORE_INCLUDE_DIR}")
        message(STATUS "Using installed protoCore: ${PROTO_CORE_LIBRARY}")
    else()
        message(FATAL_ERROR "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found. "
            "Need libprotoCore and include/protoCore.h under that prefix.")
    endif()
else()
    if(NOT TARGET protoCore)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../protoCore ${CMAKE_CURRENT_BINARY_DIR}/protoCore)
    endif()
    # Build-tree RPATH so executables and libs find each other without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/src/library" "${CMAKE_BINARY_DIR}/protoCore")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

include_directories(include)

# Subdirectories
add_subdirectory(src/library)

enable_testing()
add_subdirectory(test/library)
add_subdirectory(src/runtime)
add_subdirectory(test/regression)
add_subdirectory(test/compiler)
# Test: protopy can resolve and "run" (stubbed) the regression runner script
add_test(NAME regrtest_protopy_script COMMAND $<TARGET_FILE:protopy> --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
set_tests_properties(regrtest_protopy_script PROPERTIES LABELS "regression_gate")

# Test: run_and_report.py --output persistence (JSON written and valid)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME regrtest_persistence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/run_and_validate_output.py)
    set_tests_properties(regrtest_persistence PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/test/regression
        ENVIRONMENT "PROTOPY_BIN=$<TARGET_FILE:protopy>"
        LABELS "regression_gate")
endif()

add_test(NAME protopy_cli_help COMMAND $<TARGET_FILE:protopy> --help)
add_test(NAME protopy_cli_missing_module
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 65 $<TARGET_FILE:protopy> --dry-run --module __proto_missing__)
add_test(NAME protopy_cli_script_success
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 0 $<TARGET_FILE:protopy> --dry-run --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
add_test(NAME protopy_cli_bytecode_only
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 0 $<TARGET_FILE:protopy> --bytecode-only --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
# LOAD_METHOD's inline cache must bind every call to its own receiver.
add_test(NAME protopy_load_method_pic_receivers
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/load_method_pic_receivers.py)
set_tests_properties(protopy_load_method_pic_receivers PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")
# threading.Thread.join() must wait for the thread to finish.
add_test(NAME protopy_thread_join_waits
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/thread_join_waits.py)
set_tests_properties(protopy_thread_join_waits PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `with lock:` on native context managers must run __enter__/__exit__.
add_test(NAME protopy_with_native_context_manager
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/with_native_context_manager.py)
set_tests_properties(protopy_with_native_context_manager PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Short-lived threads with allocation churn must not crash (GC address reuse).
add_test(NAME protopy_thread_stress_no_crash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/thread_stress_no_crash.py)
set_tests_properties(protopy_thread_stress_no_crash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list mutators called from parallel threads must not lose or duplicate items.
add_test(NAME protopy_list_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_mutation_threads.py)
set_tests_properties(protopy_list_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `lst[i] = x` and list.reverse/sort/*= must not drop concurrent appends.
add_test(NAME protopy_list_rewrite_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_rewrite_threads.py)
set_tests_properties(protopy_list_rewrite_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# set mutators called from parallel threads must not lose or duplicate items.
add_test(NAME protopy_set_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_mutation_threads.py)
set_tests_properties(protopy_set_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# dict mutators called from parallel threads must keep keys and values in sync.
add_test(NAME protopy_dict_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_mutation_threads.py)
set_tests_properties(protopy_dict_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# dict keys are found through the key hash alone; inserting keys is linear.
add_test(NAME protopy_dict_key_hash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_key_hash.py)
set_tests_properties(protopy_dict_key_hash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Equal ints and floats compare, hash and key dicts as one value beyond long long.
add_test(NAME protopy_numeric_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/numeric_keys.py)
set_tests_properties(protopy_numeric_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `d[k] += v` and `obj.attr += v` store the result, evaluating the target once.
add_test(NAME protopy_aug_assign_targets
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/aug_assign_targets.py)
set_tests_properties(protopy_aug_assign_targets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# dict subclasses take their own __init__ arguments; dict.__init__ applies kwargs.
add_test(NAME protopy_dict_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_subclass_init.py)
set_tests_properties(protopy_dict_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# collections.defaultdict is a working dict subclass.
add_test(NAME protopy_defaultdict_basic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/defaultdict_basic.py)
set_tests_properties(protopy_defaultdict_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# sys.setswitchinterval / sys.getswitchinterval exist and validate like CPython.
add_test(NAME protopy_sys_switchinterval
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sys_switchinterval.py)
set_tests_properties(protopy_sys_switchinterval PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# type's getset descriptors resolve on classes (type.__qualname__); their repr works.
add_test(NAME protopy_type_getset_descriptors
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/type_getset_descriptors.py)
set_tests_properties(protopy_type_getset_descriptors PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# tuple.index / tuple.count work on raw tuples such as *args.
add_test(NAME protopy_tuple_index_raw
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/tuple_index_raw.py)
set_tests_properties(protopy_tuple_index_raw PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Class creation honours PEP 560 __mro_entries__ and records __orig_bases__.
add_test(NAME protopy_mro_entries
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/mro_entries.py)
set_tests_properties(protopy_mro_entries PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Exceptions from __init_subclass__ or the metaclass are catchable.
add_test(NAME protopy_init_subclass_exception
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/init_subclass_exception.py)
set_tests_properties(protopy_init_subclass_exception PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Sets key elements like dict keys: __hash__, 1 == 1.0 == True, tuples by elements, unhashables raise.
add_test(NAME protopy_set_python_hash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_python_hash.py)
set_tests_properties(protopy_set_python_hash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Unhashable dict keys raise TypeError; non-str/int keys are looked up by hash.
add_test(NAME protopy_dict_unhashable_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_unhashable_keys.py)
set_tests_properties(protopy_dict_unhashable_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Set operators with a dict view on the left use the set's reflected operators.
add_test(NAME protopy_set_reflected_ops
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_reflected_ops.py)
set_tests_properties(protopy_set_reflected_ops PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A class defining __eq__ without __hash__ is unhashable.
add_test(NAME protopy_class_eq_unhashable
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_eq_unhashable.py)
set_tests_properties(protopy_class_eq_unhashable PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Zero-argument __class__ in methods is the class the statement creates.
add_test(NAME protopy_class_cell
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_cell.py)
set_tests_properties(protopy_class_cell PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `del name` unbinds the name in module, class, global and exec() scopes.
add_test(NAME protopy_del_name
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/del_name.py)
set_tests_properties(protopy_del_name PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Class statements accept *bases and **keywords.
add_test(NAME protopy_class_star_bases
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_star_bases.py)
set_tests_properties(protopy_class_star_bases PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# PEP 695 type parameters on classes, functions and `type` aliases.
add_test(NAME protopy_pep695_type_params
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/pep695_type_params.py)
set_tests_properties(protopy_pep695_type_params PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# exec()/eval() run compile() code objects in the right namespaces.
add_test(NAME protopy_exec_code_object
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/exec_code_object.py)
set_tests_properties(protopy_exec_code_object PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list.__init__ fills the list; subclasses receive their own arguments.
add_test(NAME protopy_list_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_subclass_init.py)
set_tests_properties(protopy_list_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# __init_subclass__ runs once for type(), types.new_class() and metaclasses.
add_test(NAME protopy_init_subclass_dynamic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/init_subclass_dynamic.py)
set_tests_properties(protopy_init_subclass_dynamic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# reversed() works for lists, tuples, subclasses and dict views.
add_test(NAME protopy_reversed_native
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/reversed_native.py)
set_tests_properties(protopy_reversed_native PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# OrderedDict is a dict subclass keeping insertion order: move_to_end, popitem, repr, |.
add_test(NAME protopy_ordereddict_basic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/ordereddict_basic.py)
set_tests_properties(protopy_ordereddict_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque subclasses, deque.__init__ and maxlen.
add_test(NAME protopy_deque_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_subclass.py)
set_tests_properties(protopy_deque_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# str() of a dict subclass uses the subclass's __repr__.
add_test(NAME protopy_dict_subclass_str
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_subclass_str.py)
set_tests_properties(protopy_dict_subclass_str PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# sorted() accepts builtin subclasses and Python-level iterators.
add_test(NAME protopy_sorted_iterables
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sorted_iterables.py)
set_tests_properties(protopy_sorted_iterables PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# set subclasses fill through set.__init__ and iterate.
add_test(NAME protopy_set_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_subclass_init.py)
set_tests_properties(protopy_set_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# TypeVar & co. record their module and pickle by name; Box[int] repr; isinstance with tuples.
add_test(NAME protopy_typing_module_attrs
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_module_attrs.py)
set_tests_properties(protopy_typing_module_attrs PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# dict.clear/popitem called unbound act on the instance; dict.copy() returns a dict.
add_test(NAME protopy_dict_unbound_methods
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_unbound_methods.py)
set_tests_properties(protopy_dict_unbound_methods PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list.__init__ clears the list before extending it: x.__init__(x) empties x.
add_test(NAME protopy_list_init_self
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_init_self.py)
set_tests_properties(protopy_list_init_self PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque comparisons, __hash__ = None and read-only maxlen.
add_test(NAME protopy_deque_compare
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_compare.py)
set_tests_properties(protopy_deque_compare PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# cls[X] uses the metaclass __getitem__ before __class_getitem__ (Enum C['X']).
add_test(NAME protopy_metaclass_getitem
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/metaclass_getitem.py)
set_tests_properties(protopy_metaclass_getitem PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# print(), format() and str.format() use a __str__ / __format__ written in Python.
add_test(NAME protopy_print_python_str
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/print_python_str.py)
set_tests_properties(protopy_print_python_str PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# enum.Flag / IntFlag |, &, ^, ~ and F(value) build composite members (STRICT/KEEP bound, _all_bits_ set).
add_test(NAME protopy_enum_flag_ops
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/enum_flag_ops.py)
set_tests_properties(protopy_enum_flag_ops PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque mutators from parallel threads keep every item once; items are GC-traced.
add_test(NAME protopy_deque_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_threads.py)
set_tests_properties(protopy_deque_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `import typing` completes; Generic[T] subclasses, Protocol and aliases work.
add_test(NAME protopy_typing_import
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_import.py)
set_tests_properties(protopy_typing_import PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# The _typing stub's Generic must compose with ABCMeta-derived metaclasses.
add_test(NAME protopy_typing_stub_generic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_stub_generic.py)
set_tests_properties(protopy_typing_stub_generic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")

# Optional benchmark test (off by default: -DPROTOPY_BENCHMARKS=ON)
option(PROTOPY_BENCHMARKS "Run benchmark suite in CTest" OFF)
if(PROTOPY_BENCHMARKS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME protopy_benchmarks
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/benchmarks/run_benchmarks.py --output reports/benchmark_report.md)
        set_tests_properties(protopy_benchmarks PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            ENVIRONMENT "PROTOPY_BIN=$<TARGET_FILE:protopy>"
            LABELS "benchmark")
    endif()
endif()

add_subdirectory(src/compiler)

# --- Install RPATH: installed binaries and libs find each other without LD_LIBRARY_PATH ---
if(UNIX)
    if(APPLE)
        set(_rpath_exe "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
        set(_rpath_lib "@loader_path")
    else()
        set(_rpath_exe "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        set(_rpath_lib "\$ORIGIN")
    endif()
    set_target_properties(protopy protopyc PROPERTIES INSTALL_RPATH "${_rpath_exe}")
    set_target_properties(protoPython PROPERTIES INSTALL_RPATH "${_rpath_lib}")
endif()

# --- Install rules ---
set(CMAKE_INSTALL_COMPONENT "protoPython")

install(TARGETS protopy protoPython protopyc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT protoPython
)

install(DIRECTORY include/protoPython
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT protoPython
)

install(DIRECTORY lib/python3.14
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/protoPython
    USE_SOURCE_PERMISSIONS
    COMPONENT protoPython
)

# --- CPack Configuration ---
set(CPACK_PACKAGE_NAME "protopython")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "protoPython - GIL-less Python 3.14 compatible runtime")
set(CPACK_PACKAGE_VENDOR "protoPython")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoPython)

# Platform-specific configurations
if(UNIX)
    if(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    else()
        set(CPACK_GENERATOR "DEB;RPM;TGZ")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
        set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
        # protopy / libprotoPython link libprotoCore — declare the dependency
        # on protoCore's package (which provides libprotoCore), the same
        # strategy protoJS and protoST use. protoCore is no longer bundled.
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "protocore")
        set(CPACK_RPM_PACKAGE_LICENSE "MIT")
        set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
        set(CPACK_RPM_PACKAGE_REQUIRES "protoCore")
    endif()
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoPython")
endif()

include(CPack)
