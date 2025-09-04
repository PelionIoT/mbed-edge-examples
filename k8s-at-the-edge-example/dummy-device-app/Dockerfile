# Build stage
FROM rust:1.83 as builder

WORKDIR /app

# Install build dependencies
RUN apt-get update && apt-get install -y \
    pkg-config \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy manifests
COPY Cargo.toml ./Cargo.toml
COPY src ./src
COPY migrations ./migrations
COPY .cargo ./.cargo

# Build the application
RUN cargo build --release

# Runtime stage
FROM debian:bookworm-slim

# Install ca-certificates (already included in most images)
# If needed, we can install it, but let's try without first

WORKDIR /app

# Copy the binary from builder stage
COPY --from=builder /app/target/release/dummy-device-app .

# Copy migrations
COPY --from=builder /app/migrations ./migrations

# Create a non-root user
RUN useradd -r -s /bin/false app
RUN chown -R app:app /app
USER app

EXPOSE 3000

CMD ["./dummy-device-app"] 