# Mylang Developer Guide

Mylang (`.mlg`) is a toy/hobby language that compiles to C and then to native binaries. It is designed to feel higher-level than C while staying completely transparent — if you ever wonder what your code does under the hood, just look at `_temp_.c` after a build.

---

## Table of contents

1. [Building the compiler](#1-building-the-compiler)
2. [Compiling `.mlg` files](#2-compiling-mlg-files)
3. [CLI reference](#3-cli-reference)
4. [Project layout & module system](#4-project-layout--module-system)
5. [Language reference](#5-language-reference)
6. [Built-in functions](#6-built-in-functions)
7. [File I/O](#7-file-io)
8. [JSON](#8-json)
9. [Async / Await](#9-async--await)
10. [Password utilities](#10-password-utilities)
11. [HTTP client](#11-http-client)
12. [HTTP server](#12-http-server)
13. [WebSocket server](#13-websocket-server)
14. [Full backend example](#14-full-backend-example)
15. [Best practices & known limits](#15-best-practices--known-limits)

---

## 1. Building the compiler

You need GCC ≥ 9 (or Clang). On Linux/macOS:

```bash
gcc ./mylang.c -o mylang -std=c11 -Wall -Wextra -lpthread
```

On Windows (MinGW):

```bash
gcc ./mylang.c -o mylang.exe -std=c11 -Wall -Wextra -lws2_32
```

> **Never ignore the warnings.** Especially in C.

---

## 2. Compiling `.mlg` files

Mylang has **two backends** that produce completely different outputs.

### The C backend (recommended)

Translates your `.mlg` to a temporary C file (`_temp_.c`), then invokes GCC/Clang to produce a native executable. This is the backend you want for anything real.

```
your_code.mlg
      │
      ▼  (Mylang parser + codegen)
  _temp_.c
      │
      ▼  (gcc / clang)
  native binary
```

```bash
mylang main.mlg --backend c              # output: ./app  (Linux/Mac)
mylang main.mlg my_server --backend c   # output: ./my_server
```

### The VM backend (default)

Bundles the raw `.mlg` source into a portable `.mlgb` file. No GCC required to *run* it — the Mylang binary itself acts as the interpreter. Useful for distributing scripts without requiring a C compiler on the target machine.

```bash
mylang main.mlg                         # output: app.mlgb  (VM bundle, default)
mylang main.mlg bundle --backend vm    # output: bundle.mlgb

./mylang --run app.mlgb                 # run the bundle
```

> The VM backend only supports a subset of features (basic vars, print, if/while, JSON). For HTTP, async, file I/O and everything else, use `--backend c`.

---

## 3. CLI reference

```
mylang [--run] <input> [output] [--backend vm|c] [--target PLATFORM]
```

### Positional arguments

| Argument | Required | Description |
|----------|----------|-------------|
| `input`  | Yes | Path to a `.mlg` source file or a `.mlgb` bundle (with `--run`) |
| `output` | No  | Output file name. Defaults to `app` (C backend) or `app.mlgb` (VM backend) |

### Flags

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--backend` | `c`, `vm` | `vm` | Choose the compilation backend |
| `--target` | see below | `native` | Cross-compile for a different platform (C backend only) |
| `--run` | — | — | Run a `.mlgb` portable bundle instead of compiling |

### `--target` values

| Value | Produces | Requires |
|-------|----------|----------|
| `native` | Binary for the current host OS | GCC or Clang (already present) |
| `lin` | Linux x86-64 ELF | GCC on Linux, or a Linux cross-compiler |
| `win` | Windows `.exe` | `x86_64-w64-mingw32-gcc` in PATH |
| `mac` | macOS Mach-O | Clang + macOS SDK (must run on macOS) |
| `ios` | iOS ARM64 | Xcode command-line tools on macOS |
| `android` | Android ARM64 | NDK `clang --target=aarch64-linux-android` |

### Examples

```bash
# Simplest: compile and run on Linux
mylang main.mlg app --backend c && ./app

# Windows cross-compile from Linux (requires mingw)
mylang main.mlg game --backend c --target win
# → produces game.exe

# Portable bundle, run anywhere that has the mylang binary
mylang main.mlg dist --backend vm
./mylang --run dist.mlgb

# Check what C code was generated (inspect _temp_.c after any C build)
mylang main.mlg /dev/null --backend c
cat _temp_.c
```

---

## 4. Project layout & module system

### Recommended layout

```
/project
├── main.mlg          ← entry point (contains top-level code or fn main())
├── utils.mlg         ← utility functions
├── auth.mlg          ← password / session logic
└── api.mlg           ← HTTP route handlers
```

### Importing

```mlg
from utils import greet, format_date   // import specific functions
from auth import *                     // wildcard — import everything
```

The compiler resolves imports at compile time, locating `utils.mlg` relative to the importing file. Imported functions are injected into scope before the rest of the file is processed. Circular imports are detected and skipped automatically.

```mlg
// utils.mlg
string greet(string name) {
    return $"Hello, {name}!";
}
```

```mlg
// main.mlg
from utils import greet

string msg = greet("world");
print(msg);
```

---

## 5. Language reference

### Types

| Keyword  | C type      | Notes |
|----------|-------------|-------|
| `int`    | `int`       | 32-bit signed integer |
| `float`  | `float`     | 32-bit floating point |
| `double` | `double`    | 64-bit floating point |
| `bool`   | `bool`      | `true` or `false` |
| `string` | `char*`     | Heap-allocated, null-terminated |
| `json`   | `MlgJson*`  | Key/value store backed by a JSON string |
| `file`   | `MlgFile*`  | Line-indexed file handle |
| `task`   | `MlgTask*`  | Handle to a running async thread |
| `void`   | `void`      | Return type only |

### Variables

```mlg
string name   = "Alice";
int    count  = 0;
float  ratio  = 3.14;
double precise = 2.718281828;
bool   active = true;
```

### Functions

```mlg
// Regular function
int add(int a, int b) {
    return a + b;
}

// Reference parameter — the caller's variable is modified directly
void increment(ref int val) {
    val = val + 1;
}

int n = 10;
increment(n);
print($"n is now {n}");   // n is now 11
```

### String interpolation

Prefix a string literal with `$` to enable `{expr}` placeholders. Any variable or simple expression works inside the braces.

```mlg
string user  = "Bob";
int    score = 42;
print($"Player {user} scored {score} points!");
```

### Operators

```mlg
// Arithmetic
a + b   a - b   a * b   a / b

// Comparison
a == b   a != b   a < b   a > b   a <= b   a >= b

// Logic
a && b   a || b

// Increment / decrement (inside for headers)
i++   i--
```

String equality with `==` and `!=` is automatically rewritten to `strcmp` — no manual comparison needed.

### Control flow

```mlg
if (x > 0) {
    print("positive");
} else if (x == 0) {
    print("zero");
} else {
    print("negative");
}

int i = 0;
while (i < 5) {
    print($"while: {i}");
    i = i + 1;
}

for (int j = 0; j < 5; j++) {
    print($"for: {j}");
}
```

### Print

```mlg
print("plain string");
print(some_int);
print(some_float);
print($"interpolated: {name}");
```

---

## 6. Built-in functions

### String utilities

| Function | Signature | Description |
|----------|-----------|-------------|
| `string_replace` | `(string s, string old, string new) → string` | Replace first occurrence |
| `string_replace_all` | `(string s, string old, string new) → string` | Replace all occurrences |
| `string_cut` | `(string s, string marker) → string` | Everything *before* marker |
| `string_from` | `(string s, string marker) → string` | Everything *from* marker onwards |
| `s.has(sub)` | `→ bool` | True if `sub` is found in `s` |
| `s.len` | `→ int` | Length of string (like `strlen`) |
| `input(prompt)` | `→ string` | Read a line from stdin |
| `to_string(number)` | `→ string` | Convert any number to string |
| `parse_int(s)` | `→ int` | Parse string to int |
| `parse_float(s)` | `→ float` | Parse string to float |
| `parse_double(s)` | `→ double` | Parse string to double |

### Math utilities

| Function | Description |
|----------|-------------|
| `m_max(a, b, ...)` | Maximum of up to 10 values |
| `m_min(a, b, ...)` | Minimum of up to 10 values |
| `m_clamp(val, min, max)` | Clamp a float between min and max |

### Console

| Function | Description |
|----------|-------------|
| `clear_console()` | Clears the terminal (`cls` on Windows, `clear` elsewhere) |

---

## 7. File I/O

Files are opened with `file(path, mode)` where mode is `"read"` or `"write"`. A file is a line-indexed structure — each line is a separate string entry.

```mlg
// ── Writing ─────────────────────────────────────────────────────
file f = file("notes.txt", "write");
f.set(0, "first line");
f.set(1, "second line");
f.append("third line");     // adds to the end
f.save();                   // flushes to disk — don't forget this!

// ── Reading ──────────────────────────────────────────────────────
file r = file("notes.txt", "read");
string l0 = r[0];           // index operator
string l1 = r.line(1);      // method form — same thing

print($"Line 0: {l0}");
print($"Line 1: {l1}");

// ── Deleting ─────────────────────────────────────────────────────
f.delete(false);   // wipe contents, keep the file on disk
f.delete(true);    // permanently remove the file
```

### File function reference

| Function / syntax | Description |
|-------------------|-------------|
| `file(path, mode)` | Open/create a file. Mode: `"read"` or `"write"` |
| `f[n]` or `f.line(n)` | Get line at index `n` (0-based) |
| `f.set(n, text)` or `f[n] = text` | Set line at index `n` |
| `f.append(text)` | Add a new line at the end |
| `f.save()` | Write all lines to disk |
| `f.delete(permanent)` | `false` = clear contents; `true` = delete file |

---

## 8. JSON

Mylang has a built-in JSON type backed by a raw JSON string. It supports get/set by key and round-trips cleanly through `json_stringify`.

```mlg
string raw  = "{\"name\": \"Alice\", \"score\": \"99\"}";
json   obj  = json_parse(raw);

// Getters
string name   = obj.get("name");           // → "Alice"
int    score  = obj.get_int("score");      // → 99
float  ratio  = obj.get_float("ratio");    // → 0.0 if missing
double precise = obj.get_double("pi");

// Setter
obj.set("score", "100");

// Serialise
string out = json_stringify(obj);
print(out);   // → {"name": "Alice", "score": "100"}
```

### JSON function reference

| Method | Description |
|--------|-------------|
| `json_parse(str)` | Parse a JSON string into a `json` object |
| `obj.get(key)` | Get value as `string` |
| `obj.get_int(key)` | Get value as `int` |
| `obj.get_float(key)` | Get value as `float` |
| `obj.get_double(key)` | Get value as `double` |
| `obj.set(key, value)` | Set a key (value must be a string) |
| `json_stringify(obj)` | Serialise back to a JSON string |

---

## 9. Async / Await

Mark a function with `async` before its return type to make it thread-capable. Calling it with `async` launches it in a background OS thread and returns a `task` handle immediately. Use `await` to block until it finishes and collect its return value.

```mlg
async int slow_add(int a, int b) {
    // pretend this takes time
    return a + b;
}

// Launch in background — returns immediately
task t = async slow_add(100, 200);

// ... do other work here while slow_add runs ...

// Block and collect the result
int result = await t;
print($"Result: {result}");   // → Result: 300
```

### Concurrent tasks

You can launch multiple tasks and collect them independently:

```mlg
async int compute(int n) { return n * n; }

task t1 = async compute(3);
task t2 = async compute(4);
task t3 = async compute(5);

// All three run concurrently; we collect in any order
int r1 = await t1;   // 9
int r2 = await t2;   // 16
int r3 = await t3;   // 25
print($"{r1}  {r2}  {r3}");
```

### Async HTTP requests

The most practical use of async is parallelising blocking I/O:

```mlg
async string fetch(string host, string path) {
    return http_get(host, path, 80);
}

task t1 = async fetch("httpbin.org", "/get");
task t2 = async fetch("httpbin.org", "/ip");

string r1 = await t1;
string r2 = await t2;
print("Both requests done");
```

### Rules

- `async` goes before the return type in the **definition**: `async int my_func(...)`.
- `async` goes before the function name in the **call**: `task t = async my_func(...)`.
- `await t` returns the same type as the function's return type.
- Every `task` must be `await`-ed exactly once — not awaiting leaks memory.
- `void` async functions store `NULL` as their result; awaiting them is still valid (and still required for cleanup).

---

## 10. Password utilities

Built-in FNV-1a based password hashing. Good enough for toy projects and learning. For anything handling real user data, wire in bcrypt or argon2 through the C backend.

```mlg
string pw     = "hunter2";
string hashed = password_hash(pw);
// → "mlg$fnv1a$e3dd6aaf84abb335"

bool ok  = password_verify("hunter2", hashed);   // true
bool bad = password_verify("wrong",   hashed);   // false

if (ok) {
    print("Access granted");
} else {
    print("Access denied");
}
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `password_hash` | `(string password) → string` | Hash a password. Format: `mlg$fnv1a$<hex>` |
| `password_verify` | `(string password, string hash) → bool` | Verify a plaintext password against a stored hash |

---

## 11. HTTP client

### GET

```mlg
// http_get(host, path, port) → string
string raw = http_get("example.com", "/api/data", 80);
print(raw);   // full raw HTTP response (status line + headers + body)

// Extract just the body:
string body = http_parse_body(raw);
```

### POST

```mlg
// http_post(host, path, port, body) → string
string payload = "{\"username\":\"alice\",\"password\":\"secret\"}";
string raw     = http_post("api.example.com", "/login", 80, payload);

string body = http_parse_body(raw);
json   resp = json_parse(body);
string token = resp.get("token");
```

Both functions return the **full raw HTTP response** including the status line and headers. Use `http_parse_body` to strip them.

> **HTTPS is not supported.** Both functions use plain TCP on the port you specify. For TLS you would need to link OpenSSL through the C backend.

---

## 12. HTTP server

### One-shot server (testing / webhooks)

Blocks until exactly one request arrives, sends the response, then returns.

```mlg
http_server_once(8080, "Hello from Mylang!");
```

### Persistent server loop

```mlg
int server = http_server_start(8080);
print("Listening on :8080");

while (true) {
    int    client = http_accept(server);
    string req    = http_read_request(client);

    string method = http_parse_method(req);
    string path   = http_parse_path(req);

    if (path == "/") {
        http_send_response(client, "text/html", "<h1>Welcome</h1>");
    } else if (path == "/ping") {
        http_send_response(client, "text/plain", "pong");
    } else {
        http_send_response(client, "text/plain", "404 Not Found");
    }

    http_close(client);
}
```

### HTTP server function reference

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_server_start` | `(int port) → int` | Bind and listen. Returns server socket fd, or -1 on error |
| `http_accept` | `(int server) → int` | Block until a client connects. Returns client socket fd |
| `http_read_request` | `(int client) → string` | Read the raw HTTP request from the client |
| `http_send_response` | `(int client, string content_type, string body)` | Send a `200 OK` response |
| `http_close` | `(int client)` | Close the client connection |
| `http_server_once` | `(int port, string response)` | One-shot: accept one request, reply, close |

### Request parsing helpers

Parse the raw string returned by `http_read_request`:

| Function | Returns | Example result |
|----------|---------|----------------|
| `http_parse_method(req)` | HTTP verb | `"GET"`, `"POST"`, `"DELETE"` |
| `http_parse_path(req)` | URL path, no query string | `"/api/users"` |
| `http_parse_query(req)` | Raw query string | `"page=2&sort=asc"` |
| `http_parse_body(req)` | Everything after `\r\n\r\n` | `"{\"name\":\"Bob\"}"` |
| `http_parse_header(req, name)` | Value of a named header | `"application/json"` |

```mlg
string req    = http_read_request(client);
string method = http_parse_method(req);
string path   = http_parse_path(req);
string query  = http_parse_query(req);
string ct     = http_parse_header(req, "Content-Type");
string body   = http_parse_body(req);

print($"{method} {path}?{query}   ({ct})");
```

---

## 13. WebSocket server

Mylang implements the RFC 6455 WebSocket handshake (SHA-1 + Base64 computed entirely in the runtime — no external crypto library needed).

### Persistent WebSocket server

```mlg
int server = websocket_server_start(9000);
print("WebSocket server on :9000");

while (true) {
    // websocket_accept blocks until a client connects AND completes the
    // HTTP→WebSocket upgrade handshake. Returns -1 on handshake failure.
    int client = websocket_accept(server);

    if (client < 0) {
        print("Handshake failed, waiting for next connection");
    } else {
        websocket_send_text(client, "Welcome!");
        websocket_send_text(client, "Type something...");
        // ...receive loop would go here...
        websocket_close(client);
    }
}
```

### One-shot (quick test)

```mlg
int sock = websocket_server_once(9000);   // block until one WS connection
websocket_send_text(sock, "Hello over WebSocket!");
websocket_close(sock);
```

### WebSocket function reference

| Function | Signature | Description |
|----------|-----------|-------------|
| `websocket_server_start` | `(int port) → int` | Bind and listen (same as `http_server_start`) |
| `websocket_accept` | `(int server) → int` | Accept + perform WS upgrade. Returns fd or -1 |
| `websocket_send_text` | `(int fd, string text)` | Send a text frame |
| `websocket_close` | `(int fd)` | Close the socket |
| `websocket_server_once` | `(int port) → int` | Accept exactly one raw TCP connection (no upgrade) |

> **Note:** `websocket_server_once` does *not* perform the upgrade handshake — it returns the raw socket. Use `websocket_accept` for proper RFC 6455 connections.

---

## 14. Full backend example

A small JSON API server combining everything: routing, JSON, passwords, and async.

```mlg
// api_server.mlg
// Compile: mylang api_server.mlg server --backend c && ./server

async string slow_hash(string pw) {
    return password_hash(pw);
}

int server = http_server_start(4000);
print("API listening on :4000");

while (true) {
    int    client = http_accept(server);
    string req    = http_read_request(client);
    string method = http_parse_method(req);
    string path   = http_parse_path(req);
    string body   = http_parse_body(req);

    // POST /register  {"username":"alice","password":"s3cr3t"}
    if (path == "/register") {
        json payload  = json_parse(body);
        string user   = payload.get("username");
        string pw     = payload.get("password");

        // Hash asynchronously so the server stays responsive
        task   t      = async slow_hash(pw);
        string hashed = await t;

        // Persist to a flat file (one user per line: "user:hash")
        file db = file("users.txt", "read");
        db.append($"{user}:{hashed}");
        db.save();

        json resp = json_parse("{}");
        resp.set("ok", "true");
        resp.set("user", user);
        http_send_response(client, "application/json", json_stringify(resp));

    // POST /login  {"username":"alice","password":"s3cr3t"}
    } else if (path == "/login") {
        json   payload = json_parse(body);
        string user    = payload.get("username");
        string pw      = payload.get("password");

        // For brevity, we re-hash and compare (real app: look up stored hash)
        string hashed  = password_hash(pw);
        bool   valid   = password_verify(pw, hashed);

        json resp = json_parse("{}");
        if (valid) {
            resp.set("ok", "true");
            resp.set("token", "demo-token-1234");
        } else {
            resp.set("ok", "false");
            resp.set("error", "invalid credentials");
        }
        http_send_response(client, "application/json", json_stringify(resp));

    // GET /health
    } else if (path == "/health") {
        http_send_response(client, "application/json", "{\"status\":\"ok\"}");

    } else {
        http_send_response(client, "text/plain", "404 Not Found");
    }

    http_close(client);
}
```

---

## 15. Best practices & known limits

### Do this

- **Always use `--backend c`** for anything beyond a quick script. The VM backend only supports a small subset of the language.
- **Break large programs into modules.** One `.mlg` file per logical concern; import what you need.
- **Declare types explicitly.** Mylang is strongly typed — the compiler catches type mismatches at the C compilation stage.
- **`await` every `task`.** Not awaiting a task leaks the thread struct.
- **Use `http_parse_*` helpers** instead of slicing raw request strings by hand.
- **Check `_temp_.c`** when something behaves unexpectedly — it shows exactly what C code was generated.

### Known limits

| Limit | Details |
|-------|---------|
| No HTTPS | `http_get` and `http_post` use plain TCP. Link OpenSSL via the C backend if needed. |
| FNV-1a is not crypto-safe | `password_hash` is fine for learning but not for real user accounts. |
| No arrays | There is no built-in array type yet. Use a `file` as a line-indexed list, or drop to raw C via codegen tricks. |
| No structs | No user-defined compound types. Use `json` as a lightweight key/value bag instead. |
| `http_send_response` always sends `200 OK` | There is no built-in way to send 404, 500, etc. as a status code yet — you'd need to write the raw HTTP response manually. |
| VM backend feature gap | The VM interpreter does not support file I/O, HTTP, async, or most built-in string functions. Use `--backend c`. |
| Single-threaded HTTP server | The server loop handles one request at a time. Wrap `http_accept` in an `async` function to handle concurrent connections. |
| `let` is not a keyword | The docs previously showed `let variable = ...` — this does not work. Always use an explicit type: `string x = ...`. |

### This is a toy language

Mylang compiles to C, which means it can do almost anything C can — but the language itself is intentionally minimal. It is a great sandbox for learning how compilers work, not a replacement for Python, Go, or C++.
