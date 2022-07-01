
include(AddTests)
add_lib_doctest_tests(remotefs_tests remotefs::remotefs InodeCacheTests.cpp)
configure_cpp_project(remotefs_tests)
