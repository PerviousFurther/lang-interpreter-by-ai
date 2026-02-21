# Language Reference

This document is the complete reference for the language implemented by this interpreter.

---

## Table of Contents

1. [Lexical Structure](#lexical-structure)
2. [Statement Terminators](#statement-terminators)
3. [Comments](#comments)
4. [Modules](#modules)
5. [Variables](#variables)
6. [Templates](#templates)
7. [Functions](#functions)
8. [Patterns](#patterns)
9. [Control Flow](#control-flow)
10. [Expressions & Operators](#expressions--operators)
11. [Value Semantics](#value-semantics)
12. [Built-in Types](#built-in-types)
13. [Built-in Functions](#built-in-functions)
14. [Complete Examples](#complete-examples)

---

## Lexical Structure

### Identifiers

Identifiers start with a letter or underscore, followed by any combination of letters, digits, and underscores.

```
myVar   _tmp   Point   i32   HTTP2Server
```

### Integer Literals

Decimal integer literals consist of one or more digits.

```
0   42   1000
```

### Float Literals

Float literals include a decimal point.

```
3.14   0.5   100.0
```

### String Literals

String literals are enclosed in either single or double quotes. Both are equivalent and produce an immutable `literal_string`.

```
"hello, world"
'hello, world'
```

### Keywords

The following identifiers are reserved keywords:

```
fn  var  pat  import  pub  as  of
for  while  switch  case  default  break  yield  return
copy  move  null
static  const  constexpr
```

---

## Statement Terminators

Both **semicolons** (`;`) and **newlines** serve as statement terminators and are interchangeable — with one important rule: a newline is only treated as a terminator when it *can* end a statement.

A newline is **not** a terminator when:

- Inside `(…)`, `[…]`, or `{…}` — you may freely break lines inside these delimiters.
- After a `.` — a newline is allowed before the next member name (method chaining).
- After an operator that requires a right-hand side.

```
var result = someObject
    .method()    // OK — newline after . is allowed
    .another()

var x = (
    a + b        // OK — inside parentheses
)
```

---

## Comments

Line comments start with `//` and continue to the end of the line.
Block comments are delimited by `/*` and `*/`.

```
// This is a line comment

/* This is a
   block comment */
```

---

## Modules

Files (`.lang`) and directories can each be treated as modules. A directory is a module whose public members are the public declarations of its files.

### pub

The `pub` keyword marks a declaration as publicly visible outside its containing module or pattern.

```
pub var version:i32 = 1

pub fn greet(name:string) {
    print("Hello, ", name)
}
```

### import

```
import path[.path] [as <alias>]
    [ of [{] name [as <alias>] , … [}] ]
```

- `path.path` — dot-separated path to the module (maps to `path/path.lang`).
- `as <alias>` — give the module or imported item a local alias; only the alias is usable after that.
- `of` — import specific names from a module.
  - Without braces: exactly one name may follow.
  - With braces `{…}`: any number of names, comma-separated (trailing comma allowed).

When a module is imported, all its **declarative code** (`fn`, `var`, `pat` declarations) is parsed; logic code inside function bodies is deferred until first call.

```
import math
import math as m
import math of sqrt
import math of { sin, cos, tan as tangent }
import collections.list as List of { Node, push }
```

---

## Variables

```
var[<TemplateDecl>] name[:[Type][:[attributes…]]] [= [<Type>](…)]
```

### Basic declaration

```
var x:i32           // declared but not yet initialised; type is i32
var y:i32 = 42      // declared and initialised
var z = 3.14        // type inferred from initialiser (f64)
```

When a variable is declared without an initialiser it **must** have a type annotation. The first assignment to it acts as initialisation: the interpreter tries a constructor first, then a conversion function.

### Attributes and `::` (double colon)

Variable and parameter declarations have the form `name:Type:attrs`. Attributes are optional; when they are omitted there is only a single `:`. When the **type** is also omitted but attributes are still required, the two adjacent `:` merge into `::`:

```
var x:i32           // type only (single colon)
var x:i32::const    // type + attributes
var x::const = 42   // type omitted, attributes (::); type inferred from initialiser
```

When `::` is used without a type annotation the initialiser (`= …`) **must** be present, because the type cannot be inferred otherwise — the parser reports an error if it is missing.

Supported attributes: `static`, `const`, `constexpr`.

The same rule applies to function parameters and function-level attributes:

```
fn show(msg:string::const) { … }          // param: type + const attribute
fn compute(x::const):(result:i32) { … }   // param: type omitted, const attribute
fn pure(x:i32):(result:i32)::constexpr {  // function-level constexpr attribute
    result = x * x
}
```

### Multiple declaration

A single `var` statement may declare several variables separated by commas (the comma suppresses newline-as-terminator inside the list):

```
var a = foo(), b = bar(), c:i32
```

### Tuple unpacking

Ordered unpacking (must match tuple length):

```
var x:i32, y:i32 = some_tuple
```

Named or positional unpacking with braces (trailing comma allowed):

```
var { first = [0], last = [2], } = my_tuple   // positional
var { x = x_field, y = y_field }  = point_val // named fields
```

---

## Templates

Templates add compile-time parameters to `fn`, `var`, and `pat` declarations.

```
<Param[:<type|constrain|var>[:<num>]][=default], …>
```

- `Param` — a plain type template parameter (e.g. `T`).
- `Param:Kind` — a **value** template parameter of the specified kind (e.g. `N:i32` means N must be a compile-time constant of type `i32`, `T:var` means T is any variable). The entire `:Kind` annotation can be omitted; without it `Param` is a plain type parameter.
- `Param:type:num` — value parameter with variadic count `num`.
- `Param::` — type omitted, variadic (the `::` is the two colons with type absent).
- `Param::num` — type omitted, variadic with fixed count `num`.
- `=default` — default value.

```
fn<T> identity(x:T):(result:T) {
    result = x
}

fn<T, U> pair(a:T, b:U):(first:T, second:U) {
    first = a
    second = b
}

pat<T> Box {
    pub var value:T
}

var<T:i32> zero:T = T(0)
```

---

## Functions

```
fn[<TemplateDecl>] name(param:Type[:attrs…] …) [:(ret_name:RetType, …)[: attrs…]] {…}
```

### Parameters

Each parameter is `name:Type`. Attributes (`static`, `const`, `constexpr`) may follow another `:`. By default parameters are **passed by reference**; see [Value Semantics](#value-semantics) for `copy`/`move`.

```
fn add(a:i32, b:i32) { … }
fn show(msg:string::const) { … }
```

### Return values

Return values are **tuples**. The return type annotation is written as `:(name:Type, …)` — a colon followed directly by a parenthesised list of `name:Type` pairs. When there are no return values the annotation is omitted.

The named return variables are **automatically defined as local variables** inside the function body, initialised to `null`. Assigning to them inside the body sets the return value. When the function reaches the end of its body (or a bare `return`), those variables are automatically gathered into a named tuple and returned — **no explicit `return` statement is needed**.

```
// No return value
fn greet(name:string) {
    print("Hello, ", name)
}

// Single return value — assign to the named variable
fn square(x:i32):(result:i32) {
    result = x * x
}

// Multiple return values
fn divmod(a:i32, b:i32):(quotient:i32, remainder:i32) {
    quotient = a / b
    remainder = a - (a / b) * b
}
```

Accessing return values by name:

```
var s = square(7)
print(s.result)    // 49

var d = divmod(17, 5)
print(d.quotient)   // 3
print(d.remainder)  // 2
```

A bare `return` exits the function early and returns the current values of the named variables:

```
fn first_positive(a:i32, b:i32):(result:i32) {
    result = 0
    a > 0 ? result = a : null
    a > 0 ? return : null   // early exit with current result
    result = b
}
```

An explicit `return (name: val)` tuple literal may also be used for early exit with a specific value:

```
fn abs_val(x:i32):(result:i32) {
    x < 0 ? return (result: -x) : null
    result = x
}
```

### Attributes on functions

Function-level attributes follow a second `:` after the return annotation:

```
fn<T> max_val(a:T, b:T):(result:T)::constexpr {
    result = a > b ? a : b
}
```

Supported function attributes: `static`, `const`, `constexpr`.

### Methods and special functions

Inside a `pat` scope, functions may use reserved names:

| Name         | Role |
|--------------|------|
| `"construct"` | Custom constructor |
| `"destruct"`  | Custom destructor |
| `"copy"`      | Unary — copy semantics |
| `"move"`      | Unary — move semantics |

### Custom operators

Operator names are quoted strings. The interpreter determines prefix, postfix, or binary placement from attributes (defaults follow C++ conventions for standard operators):

```
fn "+"(a:Vec2, b:Vec2):(result:Vec2) {
    result = Vec2(a.x + b.x, a.y + b.y)
}

// User-defined operator
fn "+>"(a:i32, b:i32):(result:i32) {
    result = a + b + 1
}

var r = 3 +> 4   // calls fn "+>"
```

### Nested declarations

Function bodies may contain `var`, `fn`, and `pat` declarations. Marking them `pub` makes them accessible as `function_name.member`:

```
fn make_counter():(get:function, inc:function) {
    var count:i32 = 0
    pub fn get():(result:i32) { result = count }
    pub fn inc() { count = count + 1 }
    get = get
    inc = inc
}
```

---

## Patterns

Patterns are the language's struct-like types.

```
pat[<TemplateDecl>] Name[:[Base[|…]][:attrs…]] {…}
```

### Basic pattern

```
pat Point {
    pub var x:f64
    pub var y:f64
}

var p = Point(1.0, 2.0)
print(p.x)   // 1.0
print(p.y)   // 2.0
```

### Inheritance via composition

Patterns may inherit from one or more base patterns using `|`. The composed result is an anonymous `compose<…>` type; it may be converted back to any of its base-pattern references. Memory layout: leftmost base comes first (lowest address).

```
pat Animal {
    pub var name:string
}

pat Pet:Animal {
    pub var owner:string
}

pat Dog:Animal|Pet {
    pub var breed:string
}
```

### Pattern scope contents

Inside a `pat` body you may write:

- `pub var` / `var` — fields
- `pub fn` / `fn` — methods
- `pat` — nested patterns
- Logic code (executed when an instance is created)

### Constructor and destructor

```
pat Counter {
    pub var value:i32

    fn "construct"(start:i32) {
        value = start
    }

    fn "destruct"() {
        print("Counter destroyed, final value: ", value)
    }
}

var c = Counter(10)
```

---

## Control Flow

The language has **no `if` or `else` keywords**. Conditional logic is expressed through the `?:` optional expression, `switch`, and loop guards.

### Optional expression (`?:`)

```
condition ? value_if_true : value_if_false
```

This is an expression; it has a type of `builtin_optional<TrueType, FalseType>`. The `:` branch is optional — omitting it gives a `builtin_optional<TrueType>` (may be `null`).

```
var abs_x = x < 0 ? -x : x
var msg = is_error ? "error" : "ok"
```

### For loop

```
for (element : range) [:[Type][:[attrs…]]] {…}
```

Iterates `element` over `range`. The loop is a value of type `for_loop<index>` — a subtype of `builtin_scope<index, RangeType, Ret=…>`.

If the body contains `yield`, the loop produces a value (a `variant` of yielded values, or a specific type if annotated):

```
// Simple iteration
for (x : numbers) {
    print(x)
}

// Collecting values via yield
var doubled = for (x : numbers) {
    yield x * 2
}
```

### While loop

```
[while (cond)] {…} [while (cond)]
```

- `while (cond) {…}` — test-before (standard while).
- `{…} while (cond)` — test-after (do-while equivalent).

Like the for loop, the body may `yield` to produce a value.

```
var n:i32 = 1
while (n <= 10) {
    print(n)
    n = n + 1
}

// do-while style
{
    print("at least once")
} while (false)
```

### Switch

```
switch (tag) [:[Type][:attrs]] {
    case val0: [{] … [}] break
    case val1: [{] … [}] break
    default:   [{] … [}] break
}
```

`tag` and all `case` values must be of the same hashable, equality-comparable type. The `switch` body supports optional braces around each case. The result type is `switch_scope<index>`.

```
switch (status) {
    case 0: {
        print("ok")
    } break
    case 1: {
        print("warning")
    } break
    default: {
        print("error")
    } break
}
```

### Break and yield

- `break` — exit the nearest enclosing loop or switch.
- `yield expr` — emit a value from a loop body (the loop then evaluates to a collection of yielded values).

### Return

`return` with no arguments exits a function early, returning the current values of the named return variables as a tuple. An explicit `return (name: val, …)` tuple can also be given for an early exit with specific values. Named return variables are collected automatically when the function body finishes — in most cases no `return` is needed at all.

```
fn clamp(x:i32, lo:i32, hi:i32):(result:i32) {
    result = x < lo ? lo : (x > hi ? hi : x)
}
```

---

## Expressions & Operators

### Standard operators

| Precedence (high→low) | Operators |
|------------------------|-----------|
| Unary prefix | `-` `!` `~` |
| Multiplicative | `*` `/` `%` |
| Additive | `+` `-` |
| Shift | `<<` `>>` |
| Bitwise AND | `&` |
| Bitwise XOR | `^` |
| Bitwise OR | `\|` |
| Comparison | `<` `>` `<=` `>=` |
| Equality | `==` `!=` |
| Logical AND | `&&` |
| Logical OR | `\|\|` |
| Conditional | `?:` |
| Assignment | `=` |

### Assignment

```
x = expr
```

Assignment is an expression; its value is the assigned value.

### Member access

```
obj.member
obj
  .method()   // newline before member is allowed
```

### Subscript

```
container[index]
```

### Call

```
fn_name(arg0, arg1)
obj.method(arg)
```

Arguments are passed **by reference** by default. Use `copy` or `move` to pass by value (see [Value Semantics](#value-semantics)).

### Tuple literal

A parenthesised, comma-separated list of expressions forms a tuple. Elements may be named:

```
(1, 2, 3)                          // unnamed 3-tuple
(x: 1.0, y: 2.0)                  // named 2-tuple (ntuple)
(quotient: a / b, remainder: a % b)
```

A named tuple can be implicitly converted to an unnamed one; the reverse requires explicit unpacking.

### Scope as expression

A `{…}` block is itself an expression of type `builtin_scope`. It may `yield` a value:

```
var answer = {
    var tmp = 6 * 7
    yield tmp
}
```

### Template instantiation

```
Identifier<TypeArg, …>(args…)
```

---

## Value Semantics

By default, all function arguments are passed **by reference** — no copy is made.

To pass by value, prefix the argument at the **call site**:

- `copy expr` — passes a copy of the value (invokes `"copy"` if defined, otherwise performs a shallow copy).
- `move expr` — moves the value (invokes `"move"` if defined; the original variable becomes invalid).

```
fn take_ownership(v:Buffer) { … }

var buf = Buffer(1024)
take_ownership(copy buf)   // buf is still valid after the call
take_ownership(move buf)   // buf must not be used after this
```

---

## Built-in Types

### `null`

Both a type and a value. Assigning `null = expr` discards the expression's value.

```
null = side_effect_fn()
var x:null              // a variable that is always null
```

### `type`

`type` is the built-in reflection type. Declaring a variable with `:type` captures the **type** of the initialiser expression rather than its value. The resulting `type` value carries meta-information about the captured type.

```
var t:type = 42         // t holds the type of 42
print(t.name)           // i64

var t2:type = "hello"
print(t2.name)          // string
```

`type` values expose the following members:

| Member | Description |
|--------|-------------|
| `.name` | The type name as a `string` |
| `.is_pat` | `true` when the type is a pattern (struct-like) type |
| `.fields` | Named tuple of field names (non-empty for pattern types) |

```
pat Vec2 {
    pub var x:f64
    pub var y:f64
}

var v = Vec2(1.0, 2.0)
var tv:type = v
print(tv.name)      // Vec2
print(tv.is_pat)    // true
var f = tv.fields
print(f.x)          // x
print(f.y)          // y
```

`type` is also available as a regular function for use in expressions:

```
var t = type(some_value)
print(t.name)
```

### Scalar types

| Alias | Description |
|-------|-------------|
| `i8`  | 8-bit signed integer |
| `i16` | 16-bit signed integer |
| `i32` | 32-bit signed integer |
| `i64` | 64-bit signed integer |
| `u8`  | 8-bit unsigned integer |
| `u16` | 16-bit unsigned integer |
| `u32` | 32-bit unsigned integer |
| `u64` | 64-bit unsigned integer |
| `f32` / `float32` | 32-bit floating point |
| `f64` / `float64` | 64-bit floating point |

Use `import integer` to load all integer type definitions, `import float` for float types.

### `string<CharType=i8>`

A dynamic, mutable string. Literals `'…'` and `"…"` produce an immutable `literal_string`.

```
import string

var s:string = "hello"
```

### `tuple<…>` / `ntuple<…>`

A fixed-length, heterogeneous sequence of values.

- **Unnamed tuple** (`tuple`): accessed by position `[0]`, `[1]`, …
- **Named tuple** (`ntuple`): accessed by name or position; can be converted to an unnamed tuple.

Tuples are the underlying type of function parameters and return values.

### `variant<Types…>`

A tagged union; holds exactly one of the listed types at a time.

```
import variant

var v:variant<i32, string> = 42
```

### `builtin_scope<index, Types::, Ret=null>`

Every `{…}` block is an instance of a unique `builtin_scope` type (the `index` distinguishes different block sites). Scopes are callable — invoking a scope runs its body. After `import scope`, the user-accessible base type `scope` (without template parameters) is available for storing any scope.

### `builtin_optional<Type0, Type1=null, Cond=null, Ext=null>`

The result type of a `?:` expression. After `import optional`, the `optional<Type0, Type1=null>` base type is available.

### `for_loop<index>`

The type of a `for (…) {…}` expression. Sub-type of `builtin_scope<index, RangeType, Ret=…>`.

### `while_loop<index>`

The type of a `[while(…)] {…} [while(…)]` expression. Sub-type of `builtin_scope<index, bool, Ret=…>`.

### `switch_scope<index>`

The type of a `switch (…) {…}` expression. Sub-type of `builtin_scope<index, TagType, Vals::, Ret=…>`.

---

## Built-in Functions

These functions are available in the global scope without any import.

| Name | Signature | Description |
|------|-----------|-------------|
| `print` | `(vals…)` | Print values separated by spaces, followed by a newline |
| `println` | `(vals…)` | Alias for `print` |
| `input` | `([prompt:string]):(string)` | Read a line from stdin |
| `int` | `(val):(i64)` | Convert to integer |
| `float` | `(val):(f64)` | Convert to float |
| `string` | `(val):(string)` | Convert to string |
| `bool` | `(val):(bool)` | Convert to boolean |
| `is_null` | `(val):(bool)` | Test for null |
| `is_int` | `(val):(bool)` | Test for integer type |
| `is_float` | `(val):(bool)` | Test for float type |
| `is_string` | `(val):(bool)` | Test for string type |
| `type_of` | `(val):(string)` | Return the type name as a string |
| `type` | `(val):(type)` | Return a `type` reflection value for `val` (same as `var t:type = val`) |
| `abs` | `(val):(typeof val)` | Absolute value |
| `sqrt` | `(val):(f64)` | Square root |
| `pow` | `(base, exp):(f64)` | Power |
| `floor` | `(val):(i64)` | Floor toward −∞ |
| `ceil` | `(val):(i64)` | Ceiling toward +∞ |
| `min` | `(a, b)` | Minimum of two values |
| `max` | `(a, b)` | Maximum of two values |
| `len` | `(val):(i64)` | Length of string or tuple |
| `substr` | `(s:string, start:i64, len:i64):(string)` | Substring |
| `concat` | `(vals…):(string)` | Concatenate strings |
| `assert` | `(cond[, msg:string])` | Abort with message if condition is false |

---

## Complete Examples

### Fibonacci

```
fn fib(n:i32):(result:i32) {
    n <= 1 ? result = n : null
    n > 1  ? result = fib(n - 1).result + fib(n - 2).result : null
}

for (i : 0..10) {
    print(fib(i).result)
}
```

### Linked list

```
pat Node {
    pub var value:i32
    pub var next:Node
}

fn push(head:Node, val:i32):(head:Node) {
    var n = Node(val, head)
    head = n
}

fn sum_list(node:Node):(total:i32) {
    var cur = node
    while (cur != null) {
        total = total + cur.value
        cur = cur.next
    }
}

var list:Node = null
list = push(list, 1).head
list = push(list, 2).head
list = push(list, 3).head
print(sum_list(list).total)   // 6
```

### Vector with custom operator

```
pat Vec2 {
    pub var x:f64
    pub var y:f64

    fn "construct"(px:f64, py:f64) {
        x = px
        y = py
    }
}

fn "+"(a:Vec2, b:Vec2):(result:Vec2) {
    result = Vec2(a.x + b.x, a.y + b.y)
}

fn dot(a:Vec2, b:Vec2):(result:f64) {
    result = a.x * b.x + a.y * b.y
}

var u = Vec2(1.0, 0.0)
var v = Vec2(0.0, 1.0)
var w = u + v
print(dot(w, w).result)   // 2.0
```

### Switch and optional

```
fn classify(n:i32):(label:string) {
    label = switch (n % 3) {
        case 0: { yield "fizz" } break
        case 1: { yield "one"  } break
        default: { yield "other" } break
    }
}

var c = classify(9)
print(c.label)   // fizz
```

### Multiple return values

```
fn minmax(a:i32, b:i32):(lo:i32, hi:i32) {
    lo = a < b ? a : b
    hi = a < b ? b : a
}

var m = minmax(7, 3)
print(m.lo)   // 3
print(m.hi)   // 7
```

### Module example

**math/vec.lang**
```
pub pat Vec3 {
    pub var x:f64
    pub var y:f64
    pub var z:f64
}

pub fn "+"(a:Vec3, b:Vec3):(result:Vec3) {
    result = Vec3(a.x + b.x, a.y + b.y, a.z + b.z)
}
```

**main.lang**
```
import math.vec of { Vec3, "+" as vec_add }

var a = Vec3(1.0, 2.0, 3.0)
var b = Vec3(4.0, 5.0, 6.0)
var c = vec_add(a, b)
print(c.x, c.y, c.z)   // 5 7 9
```
