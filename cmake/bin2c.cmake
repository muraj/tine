cmake_minimum_required (VERSION 3.1)

# Read the input file as raw hex values
file(READ "${IN_FILE}" FILE_CONTENTS_HEX HEX)
# Replace the hex values with 
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FILE_CONTENTS_HEX ${FILE_CONTENTS_HEX})
configure_file(${TEMPLATE_FILE} ${OUT_FILE} @ONLY)