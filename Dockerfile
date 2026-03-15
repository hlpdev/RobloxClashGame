FROM debian:trixie

RUN apt-get update && apt-get install -y gcc make

WORKDIR /app

COPY dependencies.txt .
RUN apt-get install -y $(cat dependencies.txt | tr '\n' ' ')

COPY . .
RUN make && mkdir -p /app/logs

CMD ["./server"]
