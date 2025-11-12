#!/bin/bash

set -xe

ADDR=${1:-localhost}

openssl genrsa -out ./server-key.pem 2048
openssl req -new -key ./server-key.pem -out ./server-csr.pem -subj "/CN=$ADDR"
openssl x509 -req -days 365 -in ./server-csr.pem -signkey ./server-key.pem -out server-crt.pem
