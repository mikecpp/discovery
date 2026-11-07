file(GLOB_RECURSE files RELATIVE "${INPUT_DIR}" "${INPUT_DIR}/*")
if(NOT files)
    message(FATAL_ERROR "No files to embed in ${INPUT_DIR}")
endif()
list(SORT files)

set(arrays "")
set(entries "")
set(index 0)
foreach(name IN LISTS files)
    file(READ "${INPUT_DIR}/${name}" hex HEX)
    file(SIZE "${INPUT_DIR}/${name}" size)
    if(size EQUAL 0)
        set(hex "00")
    endif()
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND arrays "const unsigned char asset${index}[] = {${bytes}};\n")
    string(APPEND entries "    {\"/${name}\", asset${index}, ${size}},\n")
    math(EXPR index "${index} + 1")
endforeach()

file(WRITE "${OUTPUT}" "#include \"EmbeddedAssets.hpp\"\n\nnamespace {\n\n${arrays}\n}\n\n"
    "const EmbeddedAsset kEmbeddedAssets[] = {\n${entries}};\n\n"
    "const std::size_t kEmbeddedAssetCount = ${index};\n")
