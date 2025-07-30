# -------- STAGE 1: Builder (Alpine with g++) --------
FROM --platform=linux/arm64 alpine:3.19 as builder

# Install only required packages for build
RUN apk add --no-cache g++ libc-dev

WORKDIR /app

# Copy your C++ source and headers
COPY seat_adjustment.cpp .
COPY include/ ./include/

# Build statically-linked, optimized, and stripped binary
RUN g++ -std=c++17 -O2 -static seat_adjustment.cpp -o seat_adjustment -I./include && \
    strip seat_adjustment

# Create dependency size report
RUN echo "Binary size (bytes): $(stat -c %s seat_adjustment)" > /dependency_sizes.txt && \
    apk info -s g++ libc-dev >> /dependency_sizes.txt

# -------- STAGE 2: Final minimal container (scratch) --------
FROM --platform=linux/arm64 scratch

WORKDIR /app

# Copy only the static binary and report
COPY --from=builder /app/seat_adjustment .
COPY --from=builder /dependency_sizes.txt .

# Expose the port used by your server (60001 for Android client)
EXPOSE 60001 44821

# Run the binary
CMD ["./seat_adjustment"]
