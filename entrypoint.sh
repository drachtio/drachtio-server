#!/bin/bash
set -e

MYARGS=()

# Use environment variables if provided, otherwise calculate them
if [ -z "$LOCAL_IP" ]; then
  case $CLOUD in 
    gcp)
      LOCAL_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/network-interfaces/0/ip)
      ;;
    aws)
      if [ -z "$IMDSv2" ]; then
        LOCAL_IP=$(curl -s http://169.254.169.254/latest/meta-data/local-ipv4)
      else 
        LOCAL_IP=$(TOKEN=`curl -s -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600"` && curl -s -H "X-aws-ec2-metadata-token: $TOKEN" http://169.254.169.254/latest/meta-data/local-ipv4)
      fi
      ;;
    digitalocean)
      LOCAL_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/private/0/ipv4/address)
      ;;
    scaleway)
      LOCAL_IP=$(curl -s --local-port 1-1024 http://169.254.42.42/conf | grep PRIVATE_IP | cut -d = -f 2)
      ;;
    azure)
      if [ "$LB_IMDS" = true ]; then
        LOCAL_IP=$(curl -H "Metadata:true" --noproxy "*" "http://169.254.169.254:80/metadata/loadbalancer?api-version=2020-10-01&format=text" | jq -r '.loadbalancer.publicIpAddresses[0].privateIpAddress')
      else
        LOCAL_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/privateIpAddress?api-version=2017-08-01&format=text")
      fi
      ;;
    *)
      ;;
  esac
fi

if [ -z "$PUBLIC_IP" ]; then
  case $CLOUD in 
    gcp)
      PUBLIC_IP=$(curl -s -H "Metadata-Flavor: Google" http://metadata/computeMetadata/v1/instance/network-interfaces/0/access-configs/0/external-ip)
      ;;
    aws)
      if [ -z "$IMDSv2" ]; then
        PUBLIC_IP=$(curl -s http://169.254.169.254/latest/meta-data/public-ipv4)
      else 
        PUBLIC_IP=$(TOKEN=`curl -s -X PUT "http://169.254.169.254/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600"` && curl -s -H "X-aws-ec2-metadata-token: $TOKEN" http://169.254.169.254/latest/meta-data/public-ipv4)
      fi
      ;;
    digitalocean)
      PUBLIC_IP=$(curl -s http://169.254.169.254/metadata/v1/interfaces/public/0/ipv4/address)
      ;;
    scaleway)
      PUBLIC_IP=$(curl -s --local-port 1-1024 http://169.254.42.42/conf | grep PUBLIC_IP_ADDRESS | cut -d = -f 2)
      ;;
    azure)
      if [ "$LB_IMDS" = true ]; then
        PUBLIC_IP=$(curl -H "Metadata:true" --noproxy "*" "http://169.254.169.254:80/metadata/loadbalancer?api-version=2020-10-01&format=text" | jq -r '.loadbalancer.publicIpAddresses[0].frontendIpAddress')
      else
        PUBLIC_IP=$(curl -H Metadata:true "http://169.254.169.254/metadata/instance/network/interface/0/ipv4/ipAddress/0/publicIpAddress?api-version=2017-08-01&format=text")
      fi
      ;;
    *)
      ;;
  esac
fi

if [ "$1" = 'drachtio' ]; then
  shift

  while (( "$#" )); do
    case $1 in
    --cloud-deployment)
      MYARGS+=("--contact")
      MYARGS+=("sip:${LOCAL_IP}:${DRACHTIO_SIP_PORT:-5060};transport=udp,tcp")
      if [ -n "$PUBLIC_IP" ] && [ -z "$PRIVATE_IP_ONLY" ]; then
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
      MYARGS+=($1)
      ;;
  esac
  shift
done

if [[ -n "$WSS_PORT" ]]; then
  MYARGS+=("--contact")
  MYARGS+=("sips:${LOCAL_IP}:$WSS_PORT;transport=wss")
  if [[ -n "$PUBLIC_IP" && -z "$PRIVATE_IP_ONLY" ]]; then
    if [[ "$CLOUD" == "digitalocean" ]]; then
      MYARGS+=("--contact")
      MYARGS+=("sip:${PUBLIC_IP}:$WSS_PORT;transport=udp,tcp")
    else
      MYARGS+=("--external-ip")
      MYARGS+=("${PUBLIC_IP}")
    fi
  fi
fi

if [[ -n "$TLS_PORT" ]]; then
  MYARGS+=("--contact")
  MYARGS+=("sips:${LOCAL_IP}:$TLS_PORT;transport=tls")
  if [[ -n "$PUBLIC_IP" && -z "$PRIVATE_IP_ONLY" ]]; then
    if [[ "$CLOUD" == "digitalocean" ]]; then
      MYARGS+=("--contact")
      MYARGS+=("sip:${PUBLIC_IP}:$TLS_PORT;transport=tls")
    else
      MYARGS+=("--external-ip")
      MYARGS+=("${PUBLIC_IP}")
    fi
  fi
fi

exec drachtio "${MYARGS[@]}"
fi

exec "$@"