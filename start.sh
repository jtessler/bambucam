#!/bin/sh
set -e

# ENV defaults
DEVICE_IP="${DEVICE_IP}"
DEVICE_ID="${DEVICE_ID}"
ACCESS_CODE="${ACCESS_CODE}"
PORT="${PORT}"
KEEP_ALIVE="${KEEP_ALIVE}"   # true = bleibt an, false = Container exit bei Fehler
PING_INTERVAL="${PING_INTERVAL}" # Sekunden zwischen Pings


# Funktion zum Maskieren, nur letzte 2 Zeichen anzeigen
# mask() {
#     local val="$1"
#     local len=${#val}
#     if [ $len -le 2 ]; then
#         echo "$val"
#     else
#         mask_len=$((len-2))
#         masked=$(printf '%*s' "$mask_len" '' | tr ' ' '*')
#         echo "${masked}${val: -2}"
#     fi
# }

# Ausgabe aller ENV-Werte beim Start
echo "=== STARTING bambucam container ==="
echo "DEVICE_IP      = $DEVICE_IP"
echo "DEVICE_ID      = $DEVICE_ID"
echo "ACCESS_CODE    = $ACCESS_CODE"
echo "PORT           = $PORT"
echo "KEEP_ALIVE     = $KEEP_ALIVE"
echo "PING_INTERVAL  = $PING_INTERVAL"
echo "=================================="

while true; do
    # Prüfen, ob der Printer erreichbar ist
    if nc -z -w2 "$DEVICE_IP" "6000"; then
        echo "$(date '+%F %T') - printer available, starting bambucam"
        # Bambucam starten und auf Exit warten
        ./bambucam "$DEVICE_IP" "$DEVICE_ID" "$ACCESS_CODE" "$PORT" || true
        EXIT_CODE=$?
        echo "$(date '+%F %T') - bambucam exited with code $EXIT_CODE"
    else
        echo "$(date '+%F %T') - Printer not reachable (offline?)"
        EXIT_CODE=1
    fi

    if [ "$KEEP_ALIVE" != "true" ]; then
        echo "KEEP_ALIVE=false → Shutdown container"
        exit $EXIT_CODE
    fi

    echo "Waiting $PING_INTERVAL seconds before retrying..."
    sleep $PING_INTERVAL
done