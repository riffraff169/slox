#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <source_file.c>"
    exit 1
fi

SRC_FILE="$1"
HEADER_FILE="${SRC_FILE%.c}.h"
GUARD_NAME=$(basename "${HEADER_FILE}" | tr '[:lower:]' '[:upper:]' | tr '.' '_')

INC_TEMPLATE="${SRC_FILE%.c}_includes.h.in"

cat << EOF > "${HEADER_FILE}"
#ifndef ${GUARD_NAME}
#define ${GUARD_NAME}

EOF

if [ -f "${INC_TEMPLATE}" ]; then
    echo "/* Injected dependencies from $(basename "${INC_TEMPLATE}") */" >> "${HEADER_FILE}"
    cat "${INC_TEMPLATE}" >> "${HEADER_FILE}"
    echo "" >> "${HEADER_FILE}"
else
    cat << EOF >> "${HEADER_FILE}"

EOF
fi

cat << EOF >> "${HEADER_FILE}"
// Automatically extracted signatures
EOF

awk '/^(Value|void|bool) .+\(.*\)[[:space:]]*{/{sub(/[[:space:]]*{/, ";"); print}' "${SRC_FILE}" >> "${HEADER_FILE}"

echo "" >> "${HEADER_FILE}"
echo "#endif // ${GUARD_NAME}" >> "${HEADER_FILE}"

echo "Generated ${HEADER_FILE}"
