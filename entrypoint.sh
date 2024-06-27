#!/bin/bash
set -e

MYARGS=()

case $CLOUD in 
  gcp)
    LOCAL_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/network-interfaces/0/ip)
    PUBLIC_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata/computeMetadata/v1/instance/network-interfaces/0/access-configs/0/external-ip)
    ;;
  aws)
    if [ -z "$IMDSv2" ]; then
      LOCAL_IP=$(curl -s http://169.254.169.254/latest/meta-data/local-ipv4)
      PUBLIC_IP=$(curl -s http://169.254.169.254/latest/meta-data/public-ipv4)
    else 
      LOCAL_IP=$(TOKEN=`curl -s -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600"` && curl -s -H "X-aws-ec2-metadata-token: $TOKEN" http://169.254.169.254/latest/meta-data/local-ipv4)
      PUBLIC_IP=$(TOKEN=`curl -s -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600"` && curl -s -H "X-aws-ec2-metadata-token: $TOKEN" http://169.254.169.254/latest/meta-data/public-ipv4)
    fi
    ;;
  digitalocean)
    LOCAL_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/private/0/ipv4/address)
    PUBLIC_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/public/0/ipv4/address)
    ;;
  scaleway)
    LOCAL_IP=$(curl -s --local-port 1-1024 http://169.254.42.42/conf | grep PRIVATE_IP | cut -d = -f 2)
    PUBLIC_IP=$(curl -s --local-port 1-1024 http://169.254.42.42/conf | grep PUBLIC_IP_ADDRESS | cut -d = -f 2)
    ;;
  azure)
    if [ "$LB_IMDS" = true ]; then
      LOCAL_IP=$(curl -H "Metadata:true" --noproxy "*" "http://169.254.169.254:80/metadata/loadbalancer?api-version=2020-10-01&format=text" | jq -r '.loadbalancer.publicIpAddresses[0].privateIpAddress')
      PUBLIC_IP=$(curl -H "Metadata:true" --noproxy "*" "http://169.254.169.254:80/metadata/loadbalancer?api-version=2020-10-01&format=text" | jq -r '.loadbalancer.publicIpAddresses[0].frontendIpAddress')
    else
      LOCAL_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/privateIpAddress?api-version=2017-08-01&format=text")
      PUBLIC_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/publicIpAddress?api-version=2017-08-01&format=text")
    fi
    ;;
  *)
    ;;
esac

if [ -f "/proc/1/cgroup" ]; then
  # Docker container
  LOCAL_IP=$(ip route get 1.1.1.1 | grep -oP 'src \K\S+')
fi

if [ "$1" = 'drachtio' ]; then
  shift

  while (( "$#" )); do
    case $1 in
    --cloud-deployment)
      MYARGS+=("--contact")
      MYARGS+=("sip:${LOCAL_IP}:${DRACHTIO_SIP_PORT:-5060};transport=udp,tcp")
      if [ -n "$PUBLIC_IP" ]; then
        if [[ "$CLOUD" == "digitalocean" ]]; then
          MYARGS+=("--contact")
          MYARGS+=("sip:${PUBLIC_IP}:${DRACHTIO_SIP_PORT:-5060};transport=udp,tcp")
        else
          MYARGS+=("--external-ip")
          MYARGS+=("${PUBLIC_IP}")
        fi
      fi
      ;;

    --)
      shift
      break
      ;;

    *)
      thisarg="${1//PUBLIC_IP/"$PUBLIC_IP"}"
      thisarg="${thisarg//LOCAL_IP/"$LOCAL_IP"}"
      MYARGS+=($thisarg)
      ;;
    esac

    shift  
  done 
  
  if [[ -n "$PUBLIC_IP" && -n "$WSS_PORT" ]]; then
    MYARGS+=("--contact")
    MYARGS+=("sips:${LOCAL_IP}:$WSS_PORT;transport=wss")
    if [[ "$CLOUD" == "digitalocean" ]]; then
      MYARGS+=("--contact")
      MYARGS+=("sip:${PUBLIC_IP}:$WSS_PORT;transport=udp,tcp")
    else
      MYARGS+=("--external-ip")
      MYARGS+=("${PUBLIC_IP}")
    fi
  fi

  if [[ -n "$PUBLIC_IP" && -n "$TLS_PORT" ]]; then
    MYARGS+=("--contact")
    MYARGS+=("sips:${LOCAL_IP}:$TLS_PORT;transport=tls")
    if [[ "$CLOUD" == "digitalocean" ]]; then
      MYARGS+=("--contact")
      MYARGS+=("sip:${PUBLIC_IP}:$TLS_PORT;transport=tls")
    else
      MYARGS+=("--external-ip")
      MYARGS+=("${PUBLIC_IP}")
    fi
  fi

  exec drachtio "${MYARGS[@]}"
  
fi

exec "$@"
