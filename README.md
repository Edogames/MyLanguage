# Mylang Developer Guide

This guide provides developers with the knowledge needed to write, structure, and compile applications using the Mylang language (`.mlg`).

## 🚀 Getting Started

### 1. The Compilation Process

Mylang applications are compiled into standard executables using the `mylang` compiler.

**Basic Usage:**
To compile a single file, run:
```bash
mylang <main_file.mlg> [output_name(optional)]
```
*Example:*
```bash
mylang main.mlg my_app
# This will generate an executable named 'my_app'
```
```bash
mylang main.mlg
# This will generate an executable named 'app'
```

### 2. Project Structure

It is recommended to organize your code into modules (separate `.mlg` files) and use the `import` statement to manage dependencies.

```
/project_root
├── main.mlg       # Main entry point
├── utils.mlg      # Utility functions (e.g., logging, math)
└── network.mlg    # Networking logic
```

## 🔗 Module System: Importing Code

Mylang supports a robust module system that allows you to reuse code across different files.

### How to Import

Use the `import` statement at the top of your file. The compiler automatically handles the dependency resolution and prepends the imported functions into the current module's scope.

**Syntax:**
```mlg
from <file_name> import <module_name>;
```

**Example: `main.mlg` importing `utils.mlg`**

**`utils.mlg`:**
```mlg
// This file defines utility functions
string greet(string name) {
    return $"Hello, {name}!";
}

float calculate_area(float width, float height) {
    return width * height;
}
```

**`main.mlg`:**
```mlg
// Import the utilities module
from utils import greet, calculate_area // you cal also use '*' to import EVERYTHING

fn main() {
    // Functions from utils.mlg are now available directly
    string greeting = greet("Developer");
    print(greeting); // Output: Hello, Developer!

    float area = calculate_area(10.0, 20.0);
    print($"Area: {area}"); // Output: Area: 50.0
}
```

## ✨ Language Features and Capabilities

While the compiler handles the mechanics, the Mylang language itself is designed to be versatile. Here is an overview of its core features:

### 1. Unified Printing (`print`)
The `print` function provides a simple, unified way to output data of various types (strings, numbers, complex objects) to the console.

**Syntax:**
```mlg
print($"{value1} {value2}");
```

**Example:**
```mlg
string user_name = "Alice";
int user_age = 30;
print($"User: {user_name}, Age: {user_age}");
// Output: User: Alice, Age: 30
```

### 2. File Manipulation (I/O)
Mylang provides built-in functions for interacting with the file system, allowing programs to read configuration or save generated data.

**Key Functions:**
*Access*
*   `file[line-index: int] -> String` or `file.line(line-index: int) -> String`: Returns the specified line as a String text
*Manipulation*
*   `file(path: String) -> String-list`: Reads the entire content of a file into a string list.
*   `file.set(line-index: int, text: String)` or `file[index: int] = text: String`: Writes a string to a specified file path, overwriting existing content.
*   `file.append(text: String)`: Adds a new line with text
*   `file.delete(is-perma-remove: boolean)`: Deletes the file, and if `true` is passed, it'll permanently remove the loaded file

**Example:**
```mlg
// open the file in "write" mode
file m_file = file("test.txt", "w");

// set first line to be "Hello World!"
m_file.set(0, "Hello World!");

// actually write the file to disk
m_file.save();
```

### 3. JSON Handling
The language includes native support for JSON serialization and deserialization, making it ideal for API interaction and data exchange.

**Key Functions:**
*getters*
*   `obj.get("key")`: Gets a *data* by *key* from JSON
*   `obj.get_int("key")`: Gets specificly the **int** data from JSON
*   `obj.get_float("key")`: Gets specificly the **float** data from JSON
*   `obj.get_double("key")`: Gets specificly the **double** data from JSON
*Setter*
*   `obj.set("key", "value")`: Sets a value by *key*
*Encode/Decode*
*   `json_parse(json_string: String) -> Object`: Parses a JSON string into a usable Mylang object structure.
*   `json_stringify(object: Object) -> String`: Converts a Mylang object structure into a JSON formatted string.

**Example:**
```mlg
string json_input = "{\"name\": \"Bob\", \"score\": 95}";
let user_object = json_parse(json_input);

// Accessing fields
print($"User Name: {user_object.get(\"name\")}"); // Output: User Name: Bob

// Converting back to JSON-String
let new_json = json_stringify(user_object);
print("Re-serialized JSON: " + new_json);
```

## 💡 Best Practices

1.  **Use Modules:** Always break down large applications into smaller, focused modules. This improves readability and maintainability.
2.  **Type Safety:** Mylang is strongly typed. Always declare types (`String`, `Float`, `Int`, etc.) to catch errors at compile time.
3.  **Write while sober:** This is **NOT** an advanced programming language (like C, C++, Python, etc.), but rather (for now) a toy to play with, or to some extent streamline C-coding (remember, it turns itself into a C code before becoming an executable).
