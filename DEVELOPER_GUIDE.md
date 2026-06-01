# Mylang Developer Guide

This guide covers everything you need to write, structure, and compile Mylang (`.mlg`) programs.

---

## 🚀 Getting Started

### 0. Compiling the compiler from source

You'll need GCC (version ≥ 9) or Clang.

```bash
# Never ignore the warnings — especially in C!
gcc ./mylang.c -o mylang -std=c11 -Wall -Wextra -lpthread
```

### 1. The `.mlg` compilation process

Mylang has two backends:

| Flag | Output | What it does |
|------|--------|--------------|
| *(none)* | `app.mlgb` | Portable bundle, run with `--run` |
| `--backend c` | native binary | Compiles through GCC/Clang |

```bash
# C backend — produces a real native executable
mylang main.mlg my_app --backend c

# VM bundle — portable, no GCC needed to distribute
mylang main.mlg my_app --backend vm   # default
./mylang --run my_app.mlgb
```

### 2. Cross-compilation targets

```bash
mylang main.mlg --backend c --target win      # Windows (.exe, requires mingw)
mylang main.mlg --backend c --target lin      # Linux
mylang main.mlg --backend c --target mac      # macOS (requires clang + SDK)
mylang main.mlg --backend c --target android  # Android (requires NDK clang)
```

### 3. Recommended project layout

```
/project
├── main.mlg          # Entry point — contains fn main()
├── utils.mlg         # Utility functions
├── auth.mlg          # Auth helpers
└── network.mlg       # HTTP/WS logic
```

---

## 🔗 Module system

```mlg
from utils import greet, calculate_area
from auth import *       // wildcard: import everything
```

Imported functions are injected into the calling module's scope before compilation. Circular imports are prevented automatically.

---

## ✨ Language reference

### Types

| Keyword | C equivalent | Notes |
|---------|-------------|-------|
| `int`   | `int`       | |
| `float` | `float`     | |
| `double`| `double`    | |
| `bool`  | `bool`      | `true` / `false` |
| `string`| `char*`     | Heap-allocated |
| `json`  | `MlgJson*`  | See JSON section |
| `file`  | `MlgFile*`  | See File I/O section |
| `task`  | `MlgTask*`  | Async task handle |
| `void`  | `void`      | Return type only |

### Variables

```mlg
string name = "Alice";
int count = 0;
float ratio = 3.14;
bool active = true;
```

### Functions

```mlg
int add(int a, int b) {
    return a + b;
}

// Reference parameters (pass-by-pointer)
void increment(ref int val) {
    val = val + 1;
}

int n = 10;
increment(n);  // n is now 11
```

### String interpolation

Use `$"..."` with `{expr}` placeholders:

```mlg
string user = "Bob";
int score = 42;
print($"Player {user} scored {score} points!");
```

### Control flow

```mlg
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}

while (i < 10) {
    i = i + 1;
}

for (int i = 0; i < 5; i++) {
    print($"i = {i}");
}
```

---

## 🖨️ Print

```mlg
print($"Hello, {name}!");   // interpolated
print(some_int);            // any type works
```

---

## 📁 File I/O

```mlg
// Write a file
file f = file("data.txt", "write");
f.set(0, "first line");
f.set(1, "second line");
f.append("third line");
f.save();

// Read a file
file r = file("data.txt", "read");
string line = r[0];         // index operator
string same = r.line(0);    // method form — identical

// Delete
f.delete(false);   // delete contents only
f.delete(true);    // permanently remove from disk
```

---

## 🗂️ JSON

```mlg
string raw = "{\"name\": \"Alice\", \"score\": \"99\"}";
json obj = json_parse(raw);

// Getters
string name  = obj.get("name");
int    score = obj.get_int("score");
float  ratio = obj.get_float("ratio");

// Setter
obj.set("score", "100");

// Serialise back to string
string out = json_stringify(obj);
print(out);
```

---

## ⚡ Async / Await

Every function in Mylang can be launched as a background thread using the `async` keyword. The call returns a `task` handle; `await` blocks until the thread completes and returns the result.

```mlg
async int fetch_score(int user_id) {
    // ... do slow work ...
    return 42;
}

// Launch in background
task t = async fetch_score(7);

// ... do other work here ...

// Block and collect result
int result = await t;
print($"Score: {result}");
```

**Rules:**
- `async` precedes the return type in the function definition.
- `await expr` unwraps a `task` to its return value.
- Multiple tasks can run concurrently — launch them all, then `await` each.

```mlg
async int compute(int n) { return n * n; }

task t1 = async compute(3);
task t2 = async compute(4);
task t3 = async compute(5);

int r1 = await t1;   // 9
int r2 = await t2;   // 16
int r3 = await t3;   // 25
print($"{r1} {r2} {r3}");
```

---

## 🔒 Password utilities

Uses FNV-1a hashing. Suitable for toy/learning projects — **not** recommended for production auth (use bcrypt/argon2 there).

```mlg
string pw     = "hunter2";
string hashed = password_hash(pw);        // "mlg$fnv1a$..."

bool ok  = password_verify("hunter2", hashed);   // true
bool bad = password_verify("wrong",   hashed);   // false
```

---

## 🌐 HTTP client

### GET request

```mlg
string response = http_get("example.com", "/api/data", 80);
print(response);   // full raw HTTP response including headers
```

### POST request *(new)*

```mlg
string body     = "{\"user\":\"alice\",\"pass\":\"secret\"}";
string response = http_post("api.example.com", "/login", 80, body);
print(response);
```

**Note:** Both `http_get` and `http_post` return the **raw HTTP response** including status line and headers. Use `http_parse_body` to extract just the body.

---

## 🌐 HTTP server

### Simple one-shot server

```mlg
http_server_once(8080, "Hello from Mylang!");
```

### Persistent server loop

```mlg
int server = http_server_start(8080);
print("Listening on :8080");

while (true) {
    int client  = http_accept(server);
    string req  = http_read_request(client);

    string method = http_parse_method(req);
    string path   = http_parse_path(req);

    if (path == "/hello") {
        http_send_response(client, "text/plain", "Hello, world!");
    } else if (path == "/json") {
        http_send_response(client, "application/json", "{\"ok\":true}");
    } else {
        http_send_response(client, "text/plain", "404 Not Found");
    }

    http_close(client);
}
```

### Request parsing helpers *(new)*

These parse a raw HTTP request string (as returned by `http_read_request`):

| Function | Returns | Example |
|----------|---------|---------|
| `http_parse_method(req)` | HTTP verb | `"GET"`, `"POST"` |
| `http_parse_path(req)` | URL path (no query) | `"/api/users"` |
| `http_parse_query(req)` | Query string | `"page=2&sort=asc"` |
| `http_parse_body(req)` | Request body | `"{\"name\":\"Bob\"}"` |
| `http_parse_header(req, "Content-Type")` | Named header value | `"application/json"` |

```mlg
int server = http_server_start(3000);

while (true) {
    int client = http_accept(server);
    string req = http_read_request(client);

    string method = http_parse_method(req);
    string path   = http_parse_path(req);
    string ct     = http_parse_header(req, "Content-Type");
    string body   = http_parse_body(req);

    print($"{method} {path}  Content-Type: {ct}");

    if (path == "/echo") {
        http_send_response(client, "application/json", body);
    } else {
        http_send_response(client, "text/plain", "OK");
    }

    http_close(client);
}
```

### POST + JSON: full API example

```mlg
from utils import *

int server = http_server_start(4000);
print("API server on :4000");

while (true) {
    int client = http_accept(server);
    string req  = http_read_request(client);
    string path = http_parse_path(req);
    string body = http_parse_body(req);

    if (path == "/register") {
        json payload = json_parse(body);
        string user  = payload.get("username");
        string pw    = payload.get("password");
        string hash  = password_hash(pw);

        // In a real app: persist user + hash to a file or DB
        json resp = json_parse("{}");
        resp.set("ok", "true");
        resp.set("user", user);
        http_send_response(client, "application/json", json_stringify(resp));

    } else if (path == "/login") {
        json payload  = json_parse(body);
        string pw     = payload.get("password");
        string stored = "$stored_hash_from_db";  // load from storage in practice
        bool   valid  = password_verify(pw, stored);

        json resp = json_parse("{}");
        if (valid) {
            resp.set("ok", "true");
        } else {
            resp.set("ok", "false");
            resp.set("error", "invalid credentials");
        }
        http_send_response(client, "application/json", json_stringify(resp));
    } else {
        http_send_response(client, "text/plain", "Not Found");
    }

    http_close(client);
}
```

---

## 🔌 WebSocket server

```mlg
int server = websocket_server_start(9000);
print("WebSocket server on :9000");

while (true) {
    // websocket_accept performs the HTTP→WS upgrade handshake
    int client = websocket_accept(server);
    if (client < 0) {
        print("Handshake failed");
    } else {
        websocket_send_text(client, "Welcome!");
        websocket_close(client);
    }
}
```

For a **quick single-connection** test:

```mlg
int sock = websocket_server_once(9000);   // blocks until one connection
websocket_send_text(sock, "Hello over WS!");
websocket_close(sock);
```

---

## 💡 Best practices

1. **Modules** — split large programs into focused `.mlg` files and use `from … import`.
2. **Type everything** — Mylang is strongly typed; declare types explicitly to catch bugs at compile time.
3. **Use `--backend c`** for real programs — the VM backend is for quick portable bundles only.
4. **`async` + `await` for I/O** — wrap blocking calls (HTTP requests, file reads) in async functions so the rest of your program stays responsive.
5. **`http_parse_*` over manual string slicing** — always use the parse helpers rather than trying to cut up raw request strings by hand.
6. **FNV-1a passwords are for learning** — for anything real, add a bcrypt or argon2 library via the C backend's native linking.
7. **This is a toy language** — it compiles to C under the hood. Enjoy it for what it is, and don't expect it to replace Python, C++, or Go.
