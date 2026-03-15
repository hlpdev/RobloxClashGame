#!/bin/bash
set -e

case "$1" in
  start)
    docker compose -f compose-dev.yml up --build -d
    ;;
  stop)
    docker compose -f compose-dev.yml down
    ;;
  restart)
    docker compose -f compose-dev.yml down
    docker compose -f compose-dev.yml up --build -d
    ;;
  logs)
    docker compose -f compose-dev.yml logs -f ${2:-server}
    ;;
  *)
    echo "usage: $0 [start|stop|restart|logs]"
    exit 1
    ;;
esac
