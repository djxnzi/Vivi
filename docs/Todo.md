# Todo

## Directives

### Inline
`#inline` is direct ast splicing, similar to #macro. Expands at call site during proctime, no runtime checking.

### Proctime
`#proctime` is pure preprocessor eval, `:=` is discarded on exit, `::` survives into runtime. Reflection works differently here. Emission is all compile time.

This is how it should look when it's ready.
```
#proctime {
  pi := 3.1415926535
  radius := 10.0
  circumference := 2 * pi * radius     // proctime local
  CIRC :: circumference                // exported to runtime
}
print("{CIRC}")                        // 62.83185307
```

### Note
Simple `#pragma message` style directive.

### Targeted imports
`#import func1, func2 from "lib.vivi"`

### FFI/C
`#ffi` and `#c` binding. Gonna take forever to build this right.

```
// add.c
int add(int a, int b) { return a + b; }

// main.vivi
#ffi "add.c" as t
#c i32 t.add :: (i32 a, i32 b)         // interop requires immutability
t.add(1, 2)                            // 3
```

Ideally with `#proctime` reflection we should be able to autobind.

### Transparent asm passthrough
Likely a fasm implementation, not architecture dependent. Transparent means using raw vivi values inside the `#asm` block.

```
asm_x := 1
#asm {
    mov eax, asm_x      ; Use vivi values accessible from embedded asm
}
```

## Array Additions

### Swizzling
Odin style glsl array swizzling. `t.xxx` stuff.

### Math ops
```
a := [1, 2, 3]
b := [10, 20, 30]
c := a + b // 11, 22, 33

// Lua style, copy nonexistant values
a := [1, 2]
b := [1, 2, 3]
c := a + b // [2, 3, 3]
```

### Structural equality checks
Identical structs/arrays don't match. Currently values_equal returns false in values.c.

### `copies` keyword
The `copies` keyword is used to copy and extend, kind of like a constructor.
```
struct Vec2 {
    x: 0,
    y: 0
}

struct Vec3 copies Vec2 {
    z: 0 // extends with z
}
```

## Control Flow

### Targeted break/continue
Jai flavored `break i; continue j;`

### Typed enums
```
i32 enum Names {
    Tom,
    Dick,
    Harry
}

u32 enum UnsignedNames {
    Tom,
    Dick,
    Harry
}
```

### Implicit for loop iterators (i/ix)
Also Jai flavored. I think Jai uses it/it_value, but i/ix is simpler.
```
enum Names {
    Tom,
    Dick,
    Harry
}

for Names {
    print(i)        // index 0, 1, 2
    print(ix)       // index value Tom, Dick, harry
}
// Same as
for i in Names {
    print(i)        // index 0, 1, 2
    print(Names.i)  // index value Tom, Dick, harry
}
```

### Defer blocks - also fix environment capture
Top level defers have no scope, that means imports can't defer.
```
defer {
    // defer everything in here rather than just passing one function into defer
}
```

## Meta

### File-level top level locals
Per file top level locals. Modules with top level local definitions don't bleed out past the #import.
```
eg, test.vivi:
local v := 0

main.vivi
#import "test.vivi"
v := "hi"
```
This addition means no conflict on v because v was discarded after it passed outside of test.vivi

### Pass varargs around
Pass varargs (`argv`) into other functions, ie `print(argv)` from inside another function. Right now they just get dropped between calls. Should be an easy fix.

### Fix hoisting & import depth
Hoisting is root script and stdlib only right now. There's no recursion into imports, so any normal program with multiple files is buggy right now. Top level code on imports also needs to fire during import as well and filter into the final AST as expected.

### Fix type width signing
This one is just a hasn't been implemented yet thing. Declaration assigns the proper width, but nothing checks for it properly on conversion so we get weird stuff like ``w == u8 and w == i64`` equaling true. Truncation is also broken because that also doesn't check for widths, so casting and width checking is fake right now.

### Proctime/Runtime reflection
Reflection functions live in `lib/core/reflection.vivi`, with an env accessor for #proctime scoping.

`emit()` and `eval()` mutate the running program, but it also needs things like `type_of()`, `type_name()`, `decls_of()` etc. Full reflection suite isn't sketched. It should be extremely powerful with `#inline` and `#ffi` for auto binding and code gen.

### Method chaining
Might already work, I forget if I added any depth limit on the check.
```
y := x.method1.method2
```

### `using` keyword
Strips a namespace for the scope it appears in. Usable everywhere. Top level, inside function headers, inside functions, inside loops, inside a defer, etc.
```
f :: () {
    using lib
    test()
}
```
 
### iota
Extended Go/APL iota's. They repeat the last operation on all subsequent lines until a new operation or the end of scope is hit.

Operates in mass decl blocks and needs to work in functions/loops over arrays and structs. NOT built for Go's enum's but it can double as that. This probably won't be how the actual implementation looks cus this is sketchy psuedocode and probably doesn't make sense to the back end.

```
i32 {
    read  := 1 << iota      // 00000001 = 1
    write                   // 00000010 = 2
    remove                  // 00000100 = 4
    admin := read | write | remove   // 7, new op breaks the iota
}

u32 {
    first := iota + 1       // 1
    second                  // 2
    third                   // 3
}

while i < 100 {
    x++ iota
    y                       // y increments alongside x
}
```
    
### _ discarded null declarations
Sugar for a null declaration, ignored and discarded by the compiler.
```
f32 _ := 1.0               // This is never set because a null name can't be set
```

### Hex literals
`0x10`, `0xA` etc. Currently no support.

### stdlib additions
json/net/async

### ext proctime linker & ext poller
An `ext` is a magic external asset linker type that binds to a struct. The back end can already build metadata for it, but some extra stuff needs to happen with it to make it useful. `ext` needs to always be set at proctime with `::`.

```
ext icon :: "assets/icon.png"
```

If an `ext` can't be bound, that's a fatal proctime error, like a missing .so in C. Needs to also have a hot reload mechanism for runtime updating, never relinking. Use a normal io call for dynamic stuff.

### stdlib overrides
`_base.vivi` can be replaced from a users project root, but has to be loaded as the first import.

## Infra

### CLI restructuring
```
vivi                                // no args enters REPL
vivi version                        // Prints Vivi info
vivi <target>                       // File path or . fires the interpreter
vivi build <target>                 // build a native binary from the bytecode IR
vivi lexer <target>                 // Prints the lexer
vivi ast <target>                   // Prints the AST
```

### Bytecode to Binary compilation
Vivi always lowers to bytecode regardless of if you run the interpreter or the compiler, and then from there it uses that intermediary to lower further into a binary Probably with qbe for now since LLVM is really complicated even though LLVM should be the goal for Vivi eventually because it's just better.
```
vivi build <target>                 // Defaults to native system target
vivi build <target> --linux         // 64 bit linux ELF
vivi build <target> --windows       // 64 bit windows EXE
vivi build <target> --mac           // Whatever mac uses, idk, i'm not gonna do this
vivi build <target> --wasm          // WASM compilation
```

### C embedding api
High level, built in whitelist/blacklist table.

### Blueprint Mode
Reflection detected on compilation outside `#proctime` trips RTTI style metadata baking, so live reflection survives inside a compiled binary.

### PAK and Virtual Filesystem
This one's for game dev and mod support. `vivi pak <dir>` compiles an entire directory into a mod pak, which is raw assets and bytecode.

A sandboxed vfs `#import "Vfs"` with runtime blacklists, arena isolation per pak, an ext hot reload watchdog, and a JIT deserializer for pack bytecode. This one's gonna be complicated and heavy.

### Build a proper test suite & benchmark system
This is annoying to do so I haven't done this yet.

### Bootstrap
Reimplement Vivi inside Vivi, keep as much existing stdlib code as possible during the transfer.
