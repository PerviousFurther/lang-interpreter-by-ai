# lang-interpreter-by-ai

一个基于 **C11** 的树遍历解释器。本文档按 `src/parser.c` 的真实语法行为重写，目标是和当前解析器保持一致。

## 构建与运行

### Make

```bash
make
make test
```

生成二进制：`bin/interpreter`

### CMake

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

生成二进制：`build/interpreter`

### 运行脚本

```bash
./bin/interpreter tests/test_basic.txt
```

无参数运行会进入 REPL。

---

## 词法与分号规则

解析器配合词法器支持以下关键字：

- 声明：`fn` `var` `pat` `import` `pub`
- 控制流：`for` `while` `switch` `case` `default` `break` `yield` `return`
- 其他：`copy` `move` `null` `as` `of` `static` `const` `constexpr`

语句终止符：

- `;`
- 或满足条件时的换行（`TK_NEWLINE`）

换行在括号/中括号/花括号嵌套内不会产生语句终止 token。

---

## 顶层语句（按 parser）

`parse_stmt` 支持：

- `fn` 函数声明（可 `pub`）
- `var` 变量声明（可 `pub`）
- `pat` 模式声明（可 `pub`）
- `import` 导入声明（不可 `pub`）
- `for`、`while`、`switch`
- `break`、`yield [expr]`、`return [expr]`
- `{ ... }` 作用域块
- 普通表达式语句

---

## 声明语法

### 1) 变量声明

```text
var [<模板参数声明>] name
    [:: 属性]
    [ : 类型 [:: 属性] ]
    [= expr]
```

解析器允许并支持：

- `var a = 10`
- `var a:i32 = 10`
- `var a::const = 10`
- `var a:: = 10`（允许裸 `::`）

注意：若使用 `name::...` 且没有 `= expr`，解析器会报错（无法推断类型）。

### 2) 函数声明

```text
fn [<模板参数声明>] name_or_"custom_op"(参数列表)
   [ : 返回类型 | :(命名返回列表) ]
   [:: 函数属性]
   { ... }
```

参数支持：

- `copy` / `move` 前缀
- `name`
- `name:type`
- `name::attrs`
- `name:type::attrs`
- 默认值：`= expr`

示例：

```lang
fn add(a:i32, b:i32):(result:i32) {
    result = a + b
}

fn with_default(x::const = 50) {
    print(x)
}

fn doubled(x:i32):(result:i32):: {
    result = x * 2
}
```

### 3) 模式声明（pat）

```text
pat [<模板参数声明>] Name
    [ : Base1 | Base2 | ... ]
    [:: 属性]
    [{ ... }]
```

示例：

```lang
pat Point {
    pub var x:f64
    pub var y:f64
}
```

### 4) import

```text
import module.path [as alias] [of {item [as alias], ...}]
```

---

## 控制流语法

### for

```text
for (ident : expr) [: ...可选注解占位...] { ... }
```

### while

支持两种形式（解析器都接受）：

```text
while (cond) { ... }
{ ... } while (cond)
```

### switch

```text
switch (expr) [: ...可选注解占位...] {
    case expr: [ { ... } | ... ] [break]
    default:   [ { ... } | ... ] [break]
}
```

---

## 表达式语法（核心）

### primary

- 字面量：整数/浮点/字符串/`null`
- 标识符
- 圆括号表达式
- 元组：`(a, b)`、命名元组元素：`(name: expr, ...)`
- 作用域块：`{ ... }`
- 模板实例起始：`<Type, ...>`

### postfix

- 成员访问：`a.b`
- 调用：`f(x, y)`
- 索引：`arr[i]`
- 模板实例：`ident<Type>(...)`（带回溯尝试）

### unary

- `-expr` `!expr` `~expr`
- `copy expr` `move expr`

### binary 优先级（低 -> 高）

1. `||`
2. `&&`
3. `|`
4. `^`
5. `&`
6. `== !=`
7. `< > <= >=`
8. `<< >>`
9. `+ -`
10. `* / %`

此外：

- 赋值 `=`：最低、右结合
- 三元 `?:`：在二元表达式后解析

---

## 当前测试样例

- `tests/test_basic.txt`
- `tests/test_functions.txt`
- `tests/test_patterns.txt`
- `tests/test_dcolon.txt`

这些样例可作为 parser 支持语法的最小可运行参考。
