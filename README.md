# Hey, Red

A multithreaded, in-memory data structure store written from scratch in C++17. Built to understand how Redis works under the hood — not by reading about it, but by building one from raw TCP sockets up.

Hey, Red supports strings, lists, sets, and sorted sets. It speaks the RESP wire protocol, persists data to disk via an append-only file, and handles tens of thousands of concurrent operations per second across 16 lock-striped shards.

---

## What's Inside

**Networking.** The server runs a single-threaded event loop using `select()` (the Reactor pattern). When a socket has data, the event loop hands it off to one of 8 worker threads via a shared task queue synchronized with condition variables. There is no thread-per-connection overhead.

**Storage.** All data lives in memory, distributed across 16 independent shards. Each shard owns its own hash map, its own read-write lock (`std::shared_mutex`), and its own LRU eviction list. Two threads writing to different shards never block each other.

**Persistence.** Every write operation is appended to `database.aof`. On restart, the AOF is replayed to reconstruct the database. A background rewrite command (`BGSAVE`) snapshots the current state and compacts the AOF without blocking the main server.

**Sorted Sets.** Rather than wrapping `std::map`, sorted sets are backed by a custom skip list implementation. Insertions, deletions, and score lookups are O(log N). The skip list was chosen over a red-black tree for its simpler implementation and its natural fit with concurrent access patterns.

**Pub/Sub.** Clients can subscribe to named channels and receive messages in real time. The pub/sub system uses its own dedicated lock, completely independent of the data shards.

---

## Performance

Benchmarked locally with 50 concurrent TCP clients pipelining 2,000,000 commands per phase. Disk I/O was decoupled to isolate CPU and memory throughput.

| Operation | Throughput | Notes |
|:---|---:|:---|
| PING | 266,184 ops/sec | Network + parser ceiling. No data touched. |
| GET | 232,623 ops/sec | Reads from sharded hash map with LRU promotion. |
| LPOP | 232,505 ops/sec | Pops from head of deque, deletes key if empty. |
| SADD | 213,684 ops/sec | Hash set insertion with duplicate detection. |
| SET | 158,593 ops/sec | Full write path: hash map insert, LRU update, AOF log. |
| ZADD | 154,098 ops/sec | Skip list insert with O(log N) traversal and rebalancing. |
| LPUSH | 152,124 ops/sec | Deque push + LRU + AOF. Same write overhead as SET. |
| Mixed | 149,877 ops/sec | 80% reads and 20% writes hitting the same keys simultaneously. |

The mixed workload is the most realistic test. 40 clients spam GET while 10 clients spam SET on overlapping keys, forcing constant read-write contention across shards. The server sustained 143k ops/sec without deadlocking.

---

## Supported Commands

**Strings**
- `SET key value` — Store a string value.
- `SETEX key value seconds` — Store with an expiration.
- `GET key` — Retrieve a string value.

**Lists**
- `LPUSH key value` — Push to the head of a list.
- `RPUSH key value` — Push to the tail of a list.
- `LPOP key` — Pop from the head.

**Sets**
- `SADD key member` — Add a member to a set.
- `SMEMBERS key` — List all members of a set.

**Sorted Sets**
- `ZADD key member score` — Insert a member with a score into a sorted set.
- `ZRANGE key start stop` — Retrieve members by rank (ascending score).

**Pub/Sub**
- `SUBSCRIBE channel` — Listen for messages on a channel.
- `PUBLISH channel message` — Send a message to all subscribers of a channel.

**Server**
- `DEL key` — Delete a key.
- `KEYS` — List all non-expired keys.
- `INFO` — Show key count, commands processed, and shard count.
- `BGSAVE` — Trigger a background AOF rewrite.
- `PING` — Health check.

---

## Building and Running

Requires a C++17 compiler and CMake 3.10+. Targets Windows (Winsock2).

```
mkdir build
cd build
cmake ..
cmake --build .
cd ..
```

Start the server from the project root (so `database.aof` is saved here):
```
.\build\heyred-server.exe
```

Connect with the CLI in a separate terminal:
```
.\build\heyred-cli.exe

127.0.0.1:8080> SET name john
+OK
127.0.0.1:8080> GET name
"john"
127.0.0.1:8080> ZADD scores alice 95
+OK
127.0.0.1:8080> ZRANGE scores 0 10
1) "alice"
```

Run the benchmark suite (start the server with `--benchmark` first):
```
.\build\heyred-server.exe --benchmark
.\build\heyred-benchmark.exe
```

---

## Architecture

```
                    TCP Clients
                        |
                   [ select() ]
                   Event Loop
                        |
              +---------+---------+
              |         |         |
           Worker    Worker    Worker    (8 threads)
              |         |         |
           [ RESP Protocol Parser ]
              |         |         |
    +---------+---------+---------+---------+
    |         |         |         |         |
  Shard 0  Shard 1  Shard 2  ...  Shard 15
    |         |         |         |         |
  HashMap  HashMap  HashMap     HashMap
  RW Lock  RW Lock  RW Lock     RW Lock
  LRU List LRU List LRU List   LRU List
              |
         [ AOF Writer ]
              |
        database.aof
```

Each shard is fully independent: its own hash map, its own reader-writer lock, its own LRU doubly-linked list. The shard for a given key is determined by `std::hash<std::string>(key) % 16`.

---

## Project Structure

```
server.cpp          Server entry point. Event loop, thread pool, signal handling.
protocol.h          RESP protocol parser and command dispatcher.
store.h             MiniRedis class. Sharding, locking, LRU, AOF, all data operations.
node.h              Node struct. Wraps key, value (variant), expiry, and LRU pointers.
skiplist.h          Skip list implementation for sorted sets.
miniredis-cli.cpp   Interactive command-line client with RESP encoding.
benchmark.cpp       Stress test suite. 50 clients, 2M commands per phase, 8 phases.
CMakeLists.txt      Build configuration.
```

---

## License

MIT
