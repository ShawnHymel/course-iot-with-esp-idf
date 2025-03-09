#!/bin/bash

# Set output directory (default to /output if not specified)
OUTPUT_DIR=${OUTPUT_DIR:-/output}

# Ensure the output directory exists
mkdir -p "$OUTPUT_DIR"

# Generate the CA key
openssl genrsa -out "$OUTPUT_DIR/ca.key" 4096

# Generate the CA certificate (expires in 100 years)
openssl req \
    -new -x509 -days 36500 \
    -key "$OUTPUT_DIR/ca.key" \
    -out "$OUTPUT_DIR/ca.crt" \
    -subj "/C=US/ST=Colorado/L=Denver/O=custom-ca/CN=custom-ca"

# Ensure proper permissions
chmod 644 "$OUTPUT_DIR/ca.key"
chmod 644 "$OUTPUT_DIR/ca.crt"

echo "CA key and certificate have been generated"
