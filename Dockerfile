# === Stage 1: Build statically linked binary ===
FROM alpine:latest AS builder

RUN apk add --no-cache g++ make curl musl-dev

WORKDIR /app

COPY seat_adjustment.cpp .

RUN mkdir -p nlohmann && \
    curl -L -o nlohmann/json.hpp https://github.com/nlohmann/json/releases/latest/download/json.hpp

# Build static binary
RUN g++ -static -std=c++17 -O2 -pthread -I. -o seat_adjustment seat_adjustment.cpp


# === Stage 2: Final image ===
FROM scratch

# ✅ REQUIRED build args (propagated from CodeBuild)
ARG BUILD_TIME
ARG COMMIT_ID

# ✅ REQUIRED metadata (forces new SHA)
LABEL build_time=${BUILD_TIME}
LABEL commit_id=${COMMIT_ID}

COPY --from=builder /app/seat_adjustment /seat_adjustment

EXPOSE 60001 44821

ENTRYPOINT ["/seat_adjustment"]
