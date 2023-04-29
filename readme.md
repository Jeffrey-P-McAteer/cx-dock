
# ~~C~~ Rust X11 Dock (~~CX-Dock~~ RX-Dock)

# Build

```bash
cargo build --release
```

# Run

```bash
cargo run --release
```

# Performance testing

```bash
cargo install flamegraph
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid

cargo flamegraph ; firefox flamegraph.svg

```
