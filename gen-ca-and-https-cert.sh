#!/bin/bash
# Generates a single-use TLS Certificate Authority and certificate/key suitable
# for running a private/internal HTTPS server.
#
# This is essentially the same as using self-signed certificates, except the CA
# can be imported into browsers unlike an actual self-signed certificate.

set -e

KEY_BITS=4096
LIFESPAN_DAYS=3650

IPV4_REGEX='^[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+$'

if [ "$#" -lt 4 ]
then
	echo "Usage: $0 <server.pem> <server.key> <ca.pem> <hostname|IP> [<hostname|IP>]"
	exit 64 # EX_USAGE
fi

server_pem="$1"; shift
server_key="$1"; shift
ca_pem="$1"; shift

primary_name="$1"; shift

if [[ "$primary_name" =~ $IPV4_REGEX ]]
then
	alt_names="IP:${primary_name}"
else
	alt_names="DNS:${primary_name}"
fi

while [ "$#" -gt 0 ]
do
	if [[ "$1" =~ $IPV4_REGEX ]]
	then
		alt_names="${alt_names},IP:$1"
	else
		alt_names="${alt_names},DNS:$1"
	fi
	
	shift
done

umask 0077

tmp="$(mktemp -d)"

openssl genrsa \
	-out "${tmp}/CA.key" \
	"$KEY_BITS"

openssl req \
	-x509 \
	-new \
	-nodes \
	-key "${tmp}/CA.key" \
	-sha256 \
	-days "$LIFESPAN_DAYS" \
	-out "${tmp}/CA.pem" \
	-subj "/O=Single-purpose Certificates Inc. ($primary_name)/CN=$primary_name (CA)"

openssl genrsa \
	-out "${tmp}/server.key" \
	"$KEY_BITS"

openssl req \
	-new \
	-key "${tmp}/server.key" \
	-out "${tmp}/server.csr" \
	-subj "/CN=${primary_name}" \
	-addext "subjectAltName=${alt_names}" \
	-addext "keyUsage=digitalSignature,keyEncipherment,keyAgreement" \
	-addext "extendedKeyUsage=serverAuth"

openssl x509 \
	-req \
	-in "${tmp}/server.csr" \
	-CA "${tmp}/CA.pem" \
	-CAkey "${tmp}/CA.key" \
	-CAcreateserial \
	-out "${tmp}/server.pem" \
	-days "$LIFESPAN_DAYS" \
	-sha256 \
	-copy_extensions copyall

umask 0177
cat "${tmp}/server.key" > "$server_key"

umask 0133
cat "${tmp}/CA.pem" > "$ca_pem"
cat "${tmp}/server.pem" > "$server_pem"

rm -rf "${tmp}/"
