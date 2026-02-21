# CMake generated Testfile for 
# Source directory: /home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai
# Build directory: /home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/_codeql_build_dir
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_basic "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/_codeql_build_dir/interpreter" "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/tests/test_basic.txt")
set_tests_properties(test_basic PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;35;add_test;/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;0;")
add_test(test_functions "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/_codeql_build_dir/interpreter" "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/tests/test_functions.txt")
set_tests_properties(test_functions PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;40;add_test;/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;0;")
add_test(test_patterns "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/_codeql_build_dir/interpreter" "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/tests/test_patterns.txt")
set_tests_properties(test_patterns PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;45;add_test;/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;0;")
add_test(test_dcolon "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/_codeql_build_dir/interpreter" "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/tests/test_dcolon.txt")
set_tests_properties(test_dcolon PROPERTIES  _BACKTRACE_TRIPLES "/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;50;add_test;/home/runner/work/lang-interpreter-by-ai/lang-interpreter-by-ai/CMakeLists.txt;0;")
