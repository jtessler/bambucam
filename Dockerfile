FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

ENV DEVICE_IP="printer-ip"
ENV DEVICE_ID="printer-serial-number"
ENV ACCESS_CODE="printer-access-code"
ENV PORT=8082

# true = retry to reach printer if its offline, false = shutdown
ENV KEEP_ALIVE=true

# the interval (seconds) the printer is checked until available
ENV PING_INTERVAL=10

RUN apt-get update && apt-get install -y --no-install-recommends \
    libmicrohttpd12 \
    libjpeg62-turbo \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN mkdir plugins

# copy the executable and plugins
COPY ./bambucam .
COPY ./plugins ./plugins

# set plugin path
ENV PLUGIN_PATH=/app/plugins

EXPOSE ${PORT}


COPY start.sh .
RUN chmod +x start.sh
ENTRYPOINT ["./start.sh"]