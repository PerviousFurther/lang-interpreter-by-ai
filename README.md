# lang-interpreter-by-ai

A tree-walking interpreter for a custom expression-oriented language, written in C11.
The language has no `if`/`else`, uses smart newline handling, pattern-based structs,
named return variables, first-class scopes, and a built-in reflection `type`.

---

## Building

### CMake (recommended)

```sh
cmake -S . -B build
cmake --build build
# binary: build/interpreter
ctest --test-dir build   # run tests
```

### Make

```sh
make             # builds bin/interpreter
make test        # runs three test scripts
make clean
```

### Requirements

- C11 compiler (GCC or Clang)
- CMake ≥ 3.15 (for CMake build)
- POSIX environment (uses `getline`-style I/O)

---

## Usage

```sh
./interpreter script.lang
```

---

## Language Reference

---

### Table of Contents

1. [Lexical rules](#1-lexical-rules)
2. [Statement terminators](#2-statement-terminators)
3. [Annotation syntax — `:` and `::`](#3-annotation-syntax----and-)
4. [Variables — `var`](#4-variables----var)
5. [Functions — `fn`](#5-functions----fn)
6. [Patterns — `pat`](#6-patterns----pat)
7. [Templates](#7-templates)
8. [Modules — `import` / `pub`](#8-modules----import--pub)
9. [Control flow](#9-control-flow)
10. [Expressions and operators](#10-expressions-and-operators)
11. [Value semantics](#11-value-semantics)
12. [Built-in types](#12-built-in-types)
13. [Built-in functions](#13-built-in-functions)
14. [Complete examples](#14-complete-examples)

---

## 1. Lexical rules

### Identifiers

Start with a letter or `_`, followed by letters, digits, or `_`.

```
myVar   _tmp   Point   i32   HTTP2Server
```

### Literals

```
42          // integer
3.14        // float (requires decimal point)
"hello"     // string (double-quoted)
'world'     // string (single-quoted, identical semantics)
null        // null literal / null type
```

### Custom operator names

Inside a `fn` declaration an operator name is a double-quoted string following `fn`:

```
fn "+"(a:Vec2, b:Vec2):(result:Vec2) { … }
fn "+>"(a:i32, b:i32):(result:i32)   { … }
```

### Keywords

```
fn  var  pat  import  pub  as  of
for  while  switch  case  default  break  yield  return
copy  move  null
static  const  constexpr
```

### Comments

```
// line comment
/* block comment */
```

---

## 2. Statement terminators

Both `;` and **newline** end a statement, with one important exception: a newline is
**not** a terminator when the parser knows the statement cannot be complete yet.

Newlines are **ignored** (treated as whitespace) when inside any of:

- `(…)` — parentheses
- `[…]` — brackets
- `{…}` — braces

and also immediately after a `.` (member access) or after a binary operator.

```
// All of the following are one statement each:
var result = someObject
    .method()
    .chain()

var x = (
    a + b +
    c
)
```

---

## 3. Annotation syntax — `:` and `::`

Variables, function parameters, and function return annotations all share the same
optional-part structure:

```
name : Type :: attrs
```

Each component is individually optional:

| Written form       | Meaning |
|--------------------|---------|
| `name : Type`      | type only |
| `name : Type :: attrs` | type + attributes |
| `name :: attrs`    | **type omitted**, attributes present — the two colons merge because the type slot is empty |
| `name ::`          | **both type and attrs omitted** — a trailing `::` that is syntactically allowed but semantically equivalent to writing nothing |
| `name`             | no type, no attrs (same as `name ::`) |

The trailing `::` form is a matter of user taste; the parser accepts it and the
semantics are identical to the bare `name` form.  Examples:

```
var v :: = 6          // :: with no type and no attrs; same as:  var v = 6
var v :: const = 6    // type omitted, const attribute
var v : i32 = 6       // type present, no attrs
var v : i32 :: const = 6  // type + const
```

Function parameters and function-level attributes follow the same pattern:

```
fn foo(arg :: const = 6) :: {}   // arg: no type, const attr, default 6
                                  // function: no return, no attrs, trailing :: (optional)
fn bar(arg : i32 :: const) : (result : i32) :: constexpr {
    result = arg * 2
}
```

Supported attribute keywords: `static`, `const`, `constexpr`.

---

## 4. Variables — `var`

Full form (all parts optional except `name`):

```
var [<TemplateDecl>] name [: Type] [:: attrs] [= expr]
```

Rules:
- **No initializer and no type** → parse error (type cannot be inferred).
- **`::` without type** → initializer `= expr` is **required**.
- **Trailing `::`** (no type, no attrs) is allowed; equivalent to no annotation.
- When the type annotation is the special word `type`, the variable stores a
  **reflection object** describing the type of the initializer (see §12).

### Examples

```
var x : i32               // declared, uninitialised; type is i32
var y : i32 = 42          // declared and initialised
var z = 3.14              // type inferred as f64
var pi : f64 :: const = 3.14159    // type + const attribute
var n :: const = 100      // type inferred (i64), const attribute
var n :: = 6              // trailing :: — same as:  var n = 6

var t : type = someValue  // t holds the *type* of someValue (reflection)
```

### Multiple declarations

A single `var` can declare several names separated by commas; the comma suppresses
newline-as-terminator inside the list:

```
var a = foo(), b = bar(), c : i32
```

### Tuple unpacking

```
// ordered (must match count)
var x : i32, y : i32 = my_tuple

// named / positional with braces (trailing comma allowed)
var { x = x_field, y = y_field } = point_val   // named fields
var { first = [0], last = [2],  } = my_tuple   // positional
```

---

## 5. Functions — `fn`

Full form:

```
fn [<TemplateDecl>] name (params) [: (ret_name : RetType, …)] [:: fn_attrs] { body }
```

- The parameter list uses the same `:` / `::` annotation rules as variables (§3).
- The return annotation is `:(name:Type, …)` — a parenthesised list of named,
  typed return variables.  It is omitted entirely when there are no return values.
- Function-level attributes follow `::`.  The `::` may appear with or without a
  preceding return-type annotation: `fn foo() :: {}` (no return, bare `::`) and
  `fn foo() : (r:i32) :: constexpr {}` are both valid.  The `::` may be omitted
  when there are no function-level attributes.

### Parameters

Each parameter follows the same annotation rules as variables: `name`, `name:Type`,
`name:Type::attrs`, or `name::attrs` (type omitted).

Parameters may have a **default value** written as `= expr` after the type/attrs.
When a caller omits trailing arguments, the interpreter evaluates each missing
parameter's default expression in the call environment.

```
fn add(a : i32, b : i32) { … }           // two typed params, no return
fn show(msg : string :: const) { … }     // param with const attribute
fn run(cb :: const) { … }               // param: type omitted, const attr
fn greet(name : string = "world") {      // parameter with default value
    print("Hello,", name)
}
fn nl_sig
(a : i32, b : i32) : (result : i32) { … } // newline between name and '(' is allowed
fn power(base :: const = 2, exp : i32 = 10) : (result : i32) {
    // base has no explicit type, inferred at call time; exp is i32
    result = base
}
```

Calling with and without defaults:

```
greet()           // Hello, world
greet("Alice")    // Hello, Alice
```

### Named return variables

Every name in the return annotation is automatically defined as a local variable
inside the body, initialised to `null`.  Assign to it to set the return value.
The function collects all named return variables into a named tuple when the body
finishes, or when a bare `return` is executed — **no explicit return statement
is needed**.

```
fn square(x : i32) : (result : i32) {
    result = x * x
}

fn divmod(a : i32, b : i32) : (quotient : i32, remainder : i32) {
    quotient  = a / b
    remainder = a - quotient * b
}

var s = square(7)
print(s.result)      // 49

var d = divmod(17, 5)
print(d.quotient)    // 3
print(d.remainder)   // 2
```

A bare `return` exits early, collecting the current values of the named return
variables.  An explicit tuple literal `return (name: val, …)` is also accepted
for an early exit with specific values:

```
fn abs_val(x : i32) : (result : i32) {
    x < 0 ? return (result: -x) : null
    result = x
}
```

### Function-level attributes

```
fn compute(x : i32) : (result : i32) :: constexpr {
    result = x * x
}
```

### Custom operators

```
fn "+" (a : Vec2, b : Vec2) : (result : Vec2) {
    result = Vec2(a.x + b.x, a.y + b.y)
}
fn "+>" (a : i32, b : i32) : (result : i32) {
    result = a + b + 1
}
var r = 3 +> 4
```

### Special method names (inside `pat` bodies)

| Name          | Role |
|---------------|------|
| `"construct"` | Constructor |
| `"destruct"`  | Destructor |
| `"copy"`      | Copy (unary) |
| `"move"`      | Move (unary) |

### Nested declarations

`fn`, `var`, and `pat` may be declared inside a function body.
Marking them `pub` exposes them as `function_name.member`:

```
fn make_counter() : (get : function, inc : function) {
    var count : i32 = 0
    pub fn get() : (result : i32) { result = count }
    pub fn inc()                  { count = count + 1 }
    get = get
    inc = inc
}
```

---

## 6. Patterns — `pat`

Patterns are the language's struct-like user-defined types.

Full form:

```
pat [<TemplateDecl>] Name [: Base [| Base2 …] [:: attrs]] { body }
```

### Basic pattern

```
pat Point {
    pub var x : f64
    pub var y : f64
}

var p = Point(1.0, 2.0)
print(p.x)    // 1.0
print(p.y)    // 2.0
```

Fields are declared with `var` inside the body.  `pub` makes them accessible from
outside; fields without `pub` are private.

### Constructor and destructor

```
pat Counter {
    pub var value : i32

    fn "construct"(start : i32) {
        value = start
    }

    fn "destruct"() {
        print("final value:", value)
    }
}

var c = Counter(10)
```

### Composition / inheritance

A pattern may list one or more base patterns separated by `|`.  The result is an
anonymous `compose<…>` type that can be converted to any of its base references.
Leftmost base occupies the lowest memory address and is initialised first.

```
pat Animal { pub var name : string }
pat Pet    : Animal { pub var owner : string }
pat Dog    : Animal | Pet { pub var breed : string }
```

### Pattern body contents

- `pub var` / `var` — fields
- `pub fn` / `fn` — methods
- `pat` — nested patterns
- Logic statements — executed when an instance is created

---

## 7. Templates

Templates add compile-time parameters to `fn`, `var`, or `pat`.

Syntax for one template parameter:

```
Param [: Kind [: num]] [= default]
```

| Form | Meaning |
|------|---------|
| `T` | Plain type parameter |
| `T : i32` | Value parameter whose type is `i32` (must be a constant) |
| `T : var` | Value parameter of any type |
| `T ::` | Variadic type parameter (type annotation omitted; `::` because type slot is empty) |
| `T :: num` | Variadic with fixed count `num` |
| `= default` | Default value |

When a variadic template parameter is the **last** template parameter, calls can
pass the expanded trailing arguments directly (no extra wrapping `()` required).
This rule refers to the **call site**:

```
some_fn<Head, Tail1, Tail2>(arg)   // direct expansion at call site
```

```
fn <T> identity(x : T) : (result : T) {
    result = x
}

fn <T, U> pair(a : T, b : U) : (first : T, second : U) {
    first  = a
    second = b
}

pat <T> Box {
    pub var value : T
}

var <N : i32> zero : i32 = N
```

---

## 8. Modules — `import` / `pub`

### `pub`

Marks a declaration as visible outside its module or pattern.

```
pub var version : i32 = 1
pub fn greet(name : string) { print("Hello,", name) }
```

### `import`

```
import path [. path …] [as alias]
    [of [{ name [as alias] , … }]]
```

- `path.path` maps to `path/path.lang`.
- `as alias` — only the alias is usable after the import.
- `of name` — import one specific name (no braces).
- `of { n1, n2 as alias, … }` — import several names (braces required; trailing comma allowed).

When a module is imported, its declarative code (`fn`, `var`, `pat`) is parsed
immediately; function bodies are deferred until the function is called.

```
import math
import math as m
import math of sqrt
import math of { sin, cos, tan as tangent }
import collections.list as List of { Node, push }
```

---

## 9. Control flow

The language has **no `if` or `else`**.  Branching uses `?:`, `switch`, and loops.

### Optional expression `?:`

```
condition ? value_when_true : value_when_false
```

The `:` branch is optional; omitting it gives `null` for the false case.

```
var abs_x = x < 0 ? -x : x
x > 0 ? print("positive") : null
```

### `for` loop

```
for (element : range) [: Type [:: attrs]] { body }
```

Iterates `element` over `range`.  If `range` is an integer `n`, iterates `0 .. n-1`.
If `range` is a tuple, iterates its elements.

The loop body may contain `yield` to produce a value from the loop.

```
for (x : 5) { print(x) }        // 0 1 2 3 4

var items = (10, 20, 30)
for (v : items) { print(v) }    // 10 20 30

var doubled = for (x : items) { yield x * 2 }
```

### `while` loop

```
[while (cond)] { body } [while (cond)]
```

- `while (cond) { … }` — standard while (test before body).
- `{ … } while (cond)` — do-while (test after body).

```
var n : i32 = 0
while (n < 5) {
    print(n)
    n = n + 1
}

{ print("once") } while (false)   // executes exactly once
```

### `switch`

```
switch (tag) [: Type [:: attrs]] {
    case val : [{ ] … [ }] break
    default  : [{ ] … [ }] break
}
```

`tag` and `case` values must be the same type.  Braces around each case body are optional.

```
switch (n % 3) {
    case 0: { print("fizz")  } break
    case 1: { print("one")   } break
    default: { print("other") } break
}
```

### `break` and `yield`

- `break` — exits the nearest loop or switch.
- `yield expr` — emits a value from a loop or scope; makes the enclosing construct
  evaluate to `expr`.

### `return`

- Bare `return` — exits a function early; named return variables are collected
  automatically.
- `return (name: val, …)` — early exit with an explicit tuple value.

---

## 10. Expressions and operators

### Operator precedence (highest to lowest)

| Level | Operators |
|-------|-----------|
| Unary prefix | `-`  `!`  `~`  `copy`  `move` |
| Multiplicative | `*`  `/`  `%` |
| Additive | `+`  `-` |
| Shift | `<<`  `>>` |
| Bitwise AND | `&` |
| Bitwise XOR | `^` |
| Bitwise OR | `\|` |
| Comparison | `<`  `>`  `<=`  `>=` |
| Equality | `==`  `!=` |
| Logical AND | `&&` |
| Logical OR | `\|\|` |
| Conditional | `?:` (lowest binary; parsed after all others) |
| Assignment | `=` |

### Assignment

`=` is right-associative and an expression (evaluates to the assigned value).

### Member access

```
obj . member
obj
  . method()   // newline before member is allowed
```

### Subscript (tuple element)

```
t[0]   // first element of tuple t
t[-1]  // negative indices wrap around
```

### Call

```
fn_name(arg0, arg1)
obj.method(arg)
```

### Tuple literals

A parenthesised, comma-separated list.  Elements may be named:

```
(1, 2, 3)                    // unnamed 3-tuple
(x: 1.0, y: 2.0)            // named 2-tuple
(quotient: a/b, remainder: a%b)
```

### Scope as expression

```
var answer = {
    var tmp = 6 * 7
    yield tmp          // answer = 42
}
```

### Template instantiation

```
Identifier<TypeArg, …>(args…)
```

---

## 11. Value semantics

Function arguments are passed **by reference** by default.

To pass by value, prefix the argument at the call site:

- `copy expr` — copy (invokes `"copy"` constructor if defined, otherwise shallow copy).
- `move expr` — move (invokes `"move"` constructor if defined; original becomes invalid).

```
fn take(v : Buffer) { … }

var buf = Buffer(1024)
take(copy buf)   // buf still valid
take(move buf)   // buf must not be used after this
```

---

## 12. Built-in types

### `null`

`null` is simultaneously the keyword for the null literal and a type name.

```
var x : null          // x is permanently null
null = side_effect()  // discard return value
```

### `type` — runtime reflection

Declaring a variable with annotation `:type` captures the **type** of the
initialiser expression rather than its value:

```
var t : type = 42          // t holds the i64 type
print(t.name)              // i64

var t2 : type = "hello"
print(t2.name)             // string
```

`type` values expose:

| Member | Type | Description |
|--------|------|-------------|
| `.name` | `string` | Type name |
| `.is_pat` | `bool` | `true` for pattern (struct) types |
| `.fields` | named tuple | Field names for pattern types |

```
pat Vec2 { pub var x : f64; pub var y : f64 }

var v  = Vec2(1.0, 2.0)
var tv : type = v
print(tv.name)       // Vec2
print(tv.is_pat)     // true
print(tv.fields.x)   // x
print(tv.fields.y)   // y
```

`type` can also be called as a function: `type(expr)` returns the same reflection
object as `var t:type = expr`.

### Scalar types

| Name | Size |
|------|------|
| `i8`, `i16`, `i32`, `i64` | 8 / 16 / 32 / 64-bit signed integer |
| `u8`, `u16`, `u32`, `u64` | 8 / 16 / 32 / 64-bit unsigned integer |
| `f32` / `float32` | 32-bit IEEE float |
| `f64` / `float64` | 64-bit IEEE float |

Use `import integer` to load all integer type definitions; `import float` for floats.

### `string<CharType=i8>`

Dynamic, mutable string.  String literals (`"…"` or `'…'`) produce an immutable
`literal_string`.

```
import string
var s : string = "hello"
```

### `tuple<…>` / `ntuple<…>`

Fixed-length heterogeneous sequence.

- **Unnamed tuple**: accessed by position `t[0]`, `t[1]`, …
- **Named tuple (`ntuple`)**: accessed by name or position; convertible to unnamed.

Tuples are used for function return values.

### `variant<Types…>`

Tagged union — holds exactly one of the listed types at a time.

```
import variant
var v : variant<i32, string> = 42
```

### `builtin_scope<index, Types::, Ret=null>`

Every `{…}` block is a unique `builtin_scope` type identified by its `index`.
Scopes are callable.  After `import scope`, the base type `scope` is available.

### `builtin_optional<Type0, Type1=null, Cond=null, Ext=null>`

Result type of `?:`.  After `import optional`, `optional<Type0, Type1=null>` is available.

### `for_loop<index>` / `while_loop<index>` / `switch_scope<index>`

Subtypes of `builtin_scope` produced by the respective control-flow constructs.

---

## 13. Built-in functions

All available without any import.

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `print` | `vals…` | `null` | Print all values space-separated, then newline |
| `println` | `vals…` | `null` | Alias for `print` |
| `input` | `prompt : string` (optional) | `string` | Read a line from stdin |
| `int` | `val` | `i64` | Convert to integer |
| `float` | `val` | `f64` | Convert to float |
| `string` | `val` | `string` | Convert to string |
| `bool` | `val` | `bool` | Convert to boolean |
| `is_null` | `val` | `bool` | Test whether value is null |
| `is_int` | `val` | `bool` | Test whether value is an integer |
| `is_float` | `val` | `bool` | Test whether value is a float |
| `is_string` | `val` | `bool` | Test whether value is a string |
| `type_of` | `val` | `string` | Return the type name as a plain string |
| `type` | `val` | `type` | Return a reflection `type` object |
| `abs` | `val` | same | Absolute value |
| `sqrt` | `val` | `f64` | Square root |
| `pow` | `base, exp` | `f64` | Power |
| `floor` | `val` | `i64` | Floor (toward −∞) |
| `ceil` | `val` | `i64` | Ceiling (toward +∞) |
| `min` | `a, b` | same | Smaller of two values |
| `max` | `a, b` | same | Larger of two values |
| `len` | `val` | `i64` | Length of string or tuple |
| `substr` | `s, start, len` | `string` | Substring |
| `concat` | `vals…` | `string` | Concatenate strings |
| `assert` | `cond [, msg]` | `null` | Abort if condition is false |

---

## 14. Complete examples

### Hello world

```
print("Hello, world!")
```

### Named return variables

```
fn square(x : i32) : (result : i32) {
    result = x * x
}

fn divmod(a : i32, b : i32) : (quotient : i32, remainder : i32) {
    quotient  = a / b
    remainder = a - quotient * b
}

print(square(7).result)       // 49

var d = divmod(17, 5)
print(d.quotient)             // 3
print(d.remainder)            // 2
```

### Pattern with constructor

```
pat Vec2 {
    pub var x : f64
    pub var y : f64

    fn "construct"(px : f64, py : f64) {
        x = px
        y = py
    }
}

fn "+" (a : Vec2, b : Vec2) : (result : Vec2) {
    result = Vec2(a.x + b.x, a.y + b.y)
}

fn dot(a : Vec2, b : Vec2) : (result : f64) {
    result = a.x * b.x + a.y * b.y
}

var u = Vec2(1.0, 0.0)
var v = Vec2(0.0, 1.0)
var w = u + v
print(dot(w, w).result)   // 2.0
```

### Conditional (no if/else)

```
fn clamp(x : i32, lo : i32, hi : i32) : (result : i32) {
    result = x < lo ? lo : (x > hi ? hi : x)
}
print(clamp(15, 0, 10).result)   // 10
print(clamp(-3, 0, 10).result)   // 0
```

### Switch

```
fn classify(n : i32) : (label : string) {
    label = switch (n % 3) {
        case 0:  { yield "fizz"  } break
        case 1:  { yield "one"   } break
        default: { yield "other" } break
    }
}
print(classify(9).label)   // fizz
```

### While loop

```
var n : i32 = 1
while (n <= 5) {
    print(n)
    n = n + 1
}
// 1 2 3 4 5
```

### Type reflection

```
pat Point {
    pub var x : f64
    pub var y : f64
}

var p  = Point(3.0, 4.0)
var tp : type = p
print(tp.name)           // Point
print(tp.is_pat)         // true
print(tp.fields.x)       // x
print(tp.fields.y)       // y
print(type(42).name)     // i64
```

### Attributes and `::` syntax

```
var pi : f64 :: const = 3.14159        // type + const
var n  :: const = 100                  // type inferred, const
var m  :: = 42                         // trailing :: (same as: var m = 42)

fn pure(x : i32) : (result : i32) :: constexpr {
    result = x * x
}
```

### Module example

**math/vec.lang**
```
pub pat Vec3 {
    pub var x : f64
    pub var y : f64
    pub var z : f64
}

pub fn "+" (a : Vec3, b : Vec3) : (result : Vec3) {
    result = Vec3(a.x + b.x, a.y + b.y, a.z + b.z)
}
```

**main.lang**
```
import math.vec of { Vec3, "+" as add }

var a = Vec3(1.0, 2.0, 3.0)
var b = Vec3(4.0, 5.0, 6.0)
var c = add(a, b)
print(c.x, c.y, c.z)   // 5 7 9
```
