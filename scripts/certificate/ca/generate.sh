#!/bin/bash

set -xe

openssl req -new -x509 -days 9999 --config ./etc/ca.cnf -keyout ./ca-key.pem -out ./ca-crt.pem

openssl genrsa -out ./server-key.pem 2048
openssl req -new -config ./etc/server.cnf -key ./server-key.pem -out ./server-csr.pem
openssl x509 -req -days 365     \
    -extfile ./etc/server.cnf   \
    -passin "pass:password"     \
    -in "./server-csr.pem"      \
    -CA "./ca-crt.pem"          \
    -CAkey "./ca-key.pem"       \
    -CAcreateserial             \
    -out server-crt.pem


openssl genrsa -out ./client-key.pem 2048
openssl req -new -config ./etc/server.cnf -key ./client-key.pem -out ./client-csr.pem
openssl x509 -req -days 365     \
    -extfile ./etc/client.cnf   \
    -passin "pass:password"     \
    -in "./client-csr.pem"      \
    -CA "./ca-crt.pem"          \
    -CAkey "./ca-key.pem"       \
    -CAcreateserial             \
    -out client-crt.pem
