
# For HTTPs server

## One-way authentication

### Generate certificate

```bash
cd scripts/self_ca/
bash generate.sh SERVER_ADDR # default: localhost
```

### Run server

```bash
# https server default: localhost:18512
./server/priskv-server -a 192.168.122.1 -f /run/memfile \
-A localhost \
--http-cert ./scripts/certificate/self_ca/server-crt.pem \
--http-key ./scripts/certificate/self_ca/server-key.pem
```

### Run client

```bash
curl -i \
-X GET \
--cacert scripts/certificate/self_ca/server-crt.pem \
"https://localhost:18512/v1/ping"
```

## Two-way authentication

### Generate certificate

### Run server

```bash
# https server default: localhost:18512
./server/priskv-server -a 192.168.122.1 -f /run/memfile \
-A localhost \
--http-cert scripts/certificate/ca/server-crt.pem \
--http-key scripts/certificate/ca/server-key.pem \
--http-ca scripts/certificate/ca/ca-crt.pem \
--http-verify-client on
```

### Run client

```bash
curl -i \
-X GET \
--key scripts/certificate/ca/client-key.pem \
--cert scripts/certificate/ca/client-crt.pem \
--cacert scripts/certificate/ca/ca-crt.pem \
"https://localhost:18512/v1/ping"
```
