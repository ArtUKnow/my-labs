#!/bin/sh
set -eu
cd "$(dirname "$0")"
address='http://127.0.0.1:8000'
if curl --noproxy '*' -fsS "$address/api/health" 2>/dev/null | grep -q 'mai-labs'; then
    open "$address"
    exit 0
fi
printf '%s\n' 'Подготавливаю программу…'
make -j4
./run.sh web &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true' EXIT HUP INT TERM
attempt=0
while [ "$attempt" -lt 50 ]; do
    if curl --noproxy '*' -fsS "$address/api/health" 2>/dev/null | grep -q 'mai-labs'; then
        open "$address"
        printf '%s\n' 'Страница лабораторных открыта. Оставьте это окно открытым.'
        wait "$server_pid"
        exit 0
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        wait "$server_pid"
        exit 1
    fi
    sleep 0.1
    attempt=$((attempt + 1))
done
printf '%s\n' 'Сервер не запустился. Проверьте сообщения выше.' >&2
exit 1
