#!/bin/bash
set -e

MYARGS=()

case $CLOUD in 
  gcp)
    LOCAL_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/network-interfaces/0/ip)
    PUBLIC_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata/computeMetadata/v1/instance/network-interfaces/0/access-configs/0/external-ip)
    ;;
  aws)
    LOCAL_IP=$(curl -s http://169.254.169.254/latest/meta-data/local-ipv4)
    PUBLIC_IP=$(curl -s http://169.254.169.254/latest/meta-data/public-ipv4)
    ;;
  digitalocean)
    LOCAL_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/private/0/ipv4/address)
    PUBLIC_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/public/0/ipv4/address)
    ;;
  *)
  azure)
    LOCAL_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/privateIpAddress?api-version=2017-08-01&format=text")
    PUBLIC_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/publicIpAddress?api-version=2017-08-01&format=text")
    ;;
  *)
    ;;
esac

if [ "$1" = 'drachtio' ]; then
  shift

  while (( "$#" )); do
    case $1 in
    --cloud-deployment)
      if [ -n "$PUBLIC_IP" ]; then
        MYARGS+=("--contact")
        MYARGS+=("sip:${LOCAL_IP};transport=udp")
        MYARGS+=("--external-ip")
        MYARGS+=("${PUBLIC_IP}")
      fi
      ;;

    --)
      shift
      break
      ;;

    *)
      MYARGS+=($1)
      ;;
    esac

    shift  
  done 
  
  exec drachtio "${MYARGS[@]}"
  
fi

exec "$@"

