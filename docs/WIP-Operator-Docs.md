# THIS IS A WIP - THIS IS A WIP - THIS IS A WIP

Todo: `+=, -=, *=, /=, %=, ++, --, .. (slice), | (union), | (fallback operator, and doubles as bitwise or on context), ^, <<, >>, ~, ~=` etc (What is etc?).

Below is a table with most operators available in Vivi. `(foo)` represents something that is optional, `<bar>` represents something that is required.

## Variable Declaration

| Operator | Function                       | Example usage              | Additional notes                                                                                                       |
|----------|--------------------------------|----------------------------|------------------------------------------------------------------------------------------------------------------------|
| `:=`     | Declares a mutable variable    | `(type) <name> := (value)` | Assumes the `(type)` based on `(value)`, if `(type)` is not provided. If `(value)` is not provided, it will be `null`. |
| `::`     | Declares an immutable variable | `(type) <name> := (value)` | Same as the above `:=`, but attempting to reassign this variable will result in an error.                              |
| `=`      | Reassigns a mutable variable   | `<name> = (type) (value)`  | Will result in an error if `<name>` has already been assigned to an immutable variable.                                |

## Basic Operators

| Operator | Function    | Example usage | Additional notes |
|----------|-------------|---------------|------------------|
| `in`     | Placeholder | Placeholder   | Placeholder      |
| `not`    | Placeholder | Placeholder   | Placeholder      |
| `!`      | Placeholder | Placeholder   | Placeholder      |
| `and`    | Placeholder | Placeholder   | Placeholder      |
| `&&`     | Placeholder | Placeholder   | Placeholder      |
| `or`     | Placeholder | Placeholder   | Placeholder      |
| `\|\|`   | Placeholder | Placeholder   | Placeholder      |

## Comparison

| Operator | Function            | Example usage       | Additional notes                                         |
|----------|---------------------|---------------------|----------------------------------------------------------|
| `!=`     | Placeholder         | Placeholder         | Placeholder                                              |
| `==`     | Placeholder         | Placeholder         | Placeholder                                              |
| `<`      | Placeholder         | Placeholder         | Placeholder                                              |
| `<=`     | Placeholder         | Placeholder         | Placeholder                                              |
| `>`      | Placeholder         | Placeholder         | Placeholder                                              |
| `>=`     | Placeholder         | Placeholder         | Placeholder                                              |
| `? :`    | Ternary Conditional | `<foo>?<bar>:<bat>` | Returns `<bar>` if `<foo>` is true and `<bat>` if false. |

## Math
| Operator | Function    | Example usage | Additional notes |
|----------|-------------|---------------|------------------|
| `+`      | Placeholder | Placeholder   | Placeholder      |
| `-`      | Placeholder | Placeholder   | Placeholder      |
| `*`      | Placeholder | Placeholder   | Placeholder      |
| `/`      | Placeholder | Placeholder   | Placeholder      |
| `%`      | Placeholder | Placeholder   | Placeholder      |

# Special Operators
| Operator | Function            | Example usage | Additional notes |
|----------|---------------------|---------------|------------------|
| `&`      | Placeholder         | Placeholder   | Placeholder      |
| `^`      | Placeholder         | Placeholder   | Placeholder      |
