# === Stage 1: Build the statically linked binary ===
FROM alpine:latest AS builder

RUN apk add --no-cache g++ make curl musl-dev

WORKDIR /app

COPY seat_adjustment.cpp .

# Download json.hpp
RUN mkdir -p nlohmann && \
    curl -L -o nlohmann/json.hpp https://github.com/nlohmann/json/releases/latest/download/json.hpp

# Build a **statically linked** binary
RUN g++ -static -std=c++17 -O2 -pthread -I. -o seat_adjustment seat_adjustment.cpp

# === Stage 2: Final minimal image (scratch) ===
FROM scratch

# Copy only the statically linked binary
COPY --from=builder /app/seat_adjustment /seat_adjustment

EXPOSE 60001 44821

ENTRYPOINT ["/seat_adjustment"]

