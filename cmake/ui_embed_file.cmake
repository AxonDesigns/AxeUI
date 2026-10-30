# Embeds a file into a C++ source as a NUL-terminated char array.
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -DNAMESPACE=<ns> -P ui_embed_file.cmake
#
# Produces:
#   namespace <ns> { extern const char <name>[]; extern const unsigned <name>_size; }
# The array is written as hex bytes rather than a string literal, because MSVC
# limits string literals to about 16 KB.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," bytes "${hex}")
# Wrap lines every 16 bytes for readable diffs of the generated file.
string(REGEX REPLACE "((('\\\\x[0-9a-f][0-9a-f]',){16}))" "\\1\n  " bytes "${bytes}")
get_filename_component(input_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
"// Generated from ${input_name} by cmake/ui_embed_file.cmake. Do not edit.\n"
"namespace ${NAMESPACE} {\n"
"extern const char ${SYMBOL}[];\n"
"extern const unsigned ${SYMBOL}_size;\n"
"const char ${SYMBOL}[] = {\n  ${bytes}'\\0'};\n"
"const unsigned ${SYMBOL}_size = ${size}u;\n"
"}  // namespace ${NAMESPACE}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
