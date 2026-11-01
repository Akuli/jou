TODO: Rewrite this AI chat response into an actual design document




# Architecture Design: Deterministic Record & Replay System for Language Server

## 1. Executive Summary

Debugging Language Server Protocol (LSP) implementations is notoriously difficult due to non-deterministic asynchronous inputs, file system mutation outside the editor, non-repeatable input framing (stdin chunking), and time-dependent behaviors (debouncing).

This document outlines the architecture for a **deterministic event capture and replay system**. By isolating the language server’s core logic into a pure state machine driven by explicit event inputs, we enable 100% reproducible execution runs without requiring full workspace pre-snapshots or complex multithreading synchronization.

---

## 2. Core Architecture Pattern

The system follows an **Imperative Shell, Functional Core** design. The core engine is modeled strictly as a deterministic function:

$$\text{CoreState}_{t+1}, \text{StdoutBytes} = f(\text{CoreState}_t, \text{StdinChunk}, \text{VirtualFS})$$

```
                   ┌─────────────────────────────────────────┐
                   │               Stdin Thread              │
                   │    (Reads raw bytes into FIFO queue)    │
                   └────────────────────┬────────────────────┘
                                        │
                                        ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                            Replay / Record Layer                            │
 ├──────────────────────────────────────┬──────────────────────────────────────┤
 │             RECORD MODE              │             REPLAY MODE              │
 │  1. Write updated file snapshot      │  1. Populate Virtual FS cache from   │
 │     to log (if modified on disk).    │     replay log record.               │
 │  2. Write raw stdin chunk to log.    │  2. Assert zero real disk I/O.       │
 │  3. Pass inputs to Functional Core.  │  3. Feed chunk to Functional Core.   │
 └──────────────────┬───────────────────┴──────────────────┬───────────────────┘
                    │                                      │
                    ▼                                      ▼
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                               Functional Core                               │
 │                                                                             │
 │  • Handles LSP Content-Length framing & JSON-RPC parsing                    │
 │  • Pure business logic & semantic analysis                                  │
 │  • Reads files ONLY via the Virtual FS interface                            │
 └─────────────────────────────────────────────────────────────────────────────┘

```

---

## 3. Key Design Decisions

### 3.1. Single Timeline & Strict Event Ordering

To maintain causality, the event log is a single, strictly ordered sequence of events. When a file on disk changes and is accessed, its updated content must be logged **before** the incoming stdin chunk that triggered or depended on that read.

**Record Log Event Sequence:**

1. **`FileSnapshot` Event:** `(Path, ContentHash, RawBytes | ReadError)`
* *Triggered when the core attempts to read a file from disk whose content or error state has changed since the last logged snapshot.*


2. **`StdinChunk` Event:** `(ByteCount, RawBytes)`
* *Contains all pending raw bytes drained from the stdin queue in a single batch.*



### 3.2. Record vs. Replay Behavior

| Feature | Record Mode | Replay Mode |
| --- | --- | --- |
| **Stdin Source** | Active OS `stdin` stream (drained asynchronously via a reader thread). | Pre-recorded `StdinChunk` events from the `.replay` file. |
| **Disk Access** | Intercepted by Virtual FS wrapper. Checks disk stats/timestamps, fetches real content, updates cache, and appends `FileSnapshot` to log if modified. | **Strictly prohibited.** Virtual FS serves content directly from the recorded snapshots. Any un-cached file read throws a fatal assertion error. |
| **Timing & Debouncing** | Managed in the imperative shell outside the functional core. | Timers are bypassed/virtualized; input chunks are executed as fast as the CPU allows. |
| **LSP Framing** | Deferred to the Functional Core; raw byte streams are recorded directly. | Feeds exact byte chunks to test core buffer behavior and framing edge cases. |

---

## 4. Subsystem Specifications

### 4.1. File System Abstraction (Virtual FS)

* All external filesystem reads (`read_to_string`, stat checks, configuration lookups) are collapsed into a single logical read operation behind a clean abstraction.
* Smart caching (e.g., throttling disk `stat` calls based on elapsed time) lives entirely outside the replay mechanism in the live I/O layer.
* Replay mode only sees the resolved outcome: *"File X contained Content Y at step Z"*.

### 4.2. Stdin Queue & Framing

* A background thread continually reads available bytes from standard input into a thread-safe FIFO queue.
* The main loop drains all currently available bytes from the queue into a single chunk buffer, logs the `StdinChunk` event, and passes it to the core.
* Content-Length header parsing and JSON-RPC message framing are handled inside the Functional Core. This allows the replay system to reproduce transport-level parser bugs and boundary chunking issues.

---

## 5. CLI Interface & Test Harness

The executable exposes CLI flags to switch operating modes seamlessly:

```bash
# Record an active session to a log file
$ my-langserver --record=/tmp/bug_repro.replay

# Replay a recorded session for debugging
$ my-langserver --replay=/tmp/bug_repro.replay

# Use as a regression test fixture in CI
$ my-langserver --replay=tests/fixtures/issue_123.replay --assert-diagnostics

```

---

Is there any specific detail (such as log serialization format or trait interfaces) you'd like to adjust or expand upon before putting this into your design doc?
