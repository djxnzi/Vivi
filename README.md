# <img src="vivi_icon.png" width="40" alt="Vivi Icon"> Vivi (.vivi)

![GitHub commit activity](https://img.shields.io/github/commit-activity/m/djxnzi/vivi?style=for-the-badge) ![GitHub License](https://img.shields.io/github/license/djxnzi/vivi?style=for-the-badge)

> Vivi is still in alpha and only works on Linux atm. Windows builds are planned, but aren't priority

Vivi is intended to be a hackable modern metaprogramming language with reflective preprocessing. It's a gradually typed, embeddable scripting lang with optional type annotation and a lightweight core. Vivi is interpreted, can interpret itself, and runs linearly. Its #proctime phase works like #comptime, giving you full reflective access to the program before execution. It also has grammar extension through a #phrase system.

Vivi is NOT
```
- Garbage collected.
- A virtual machine, virtual environment, or JIT enabled interpreter.
- Fully interpreted or compiled, high/low, statically typed, etc. Opt in to what you need.
- A language that has traditional macros. Generate it with #proctime.
- An object oriented language. No objects, no this, but you can extend it with #phrase
- A language with a fully featured core library.
- A language with an unmodifiable core library.
- A language that locks you into itself, you can interop.
- Rigid or locked down, you can hack it.
- Free of opinions, but it is also a lang that doesn't get in your way.
- Finished and fully functional.
```

For a getting started guide and the documentation, please see the [wiki](https://github.com/djxnzi/Vivi/wiki)
