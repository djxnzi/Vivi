# Vivi (.vivi)

> Vivi is still a WIP and in alpha, so a lot of this isn't true right now and will be eventually

Vivi is a hackable modern metaprogramming language with reflective preprocessing.

It is a gradually typed, embeddable scripting lang with optional type annotation and a lightweight core.

Vivi is interpreted, can interpret itself, and runs linearly. 

Its #proctime phase works like #comptime, giving you full reflective access to the program before execution

It also has grammar extension through a #phrase system

Vivi is NOT
- A language that has macros. Generate it with #proctime.
- An object oriented language. No objects, no this.
- A virtual machine, virtual environment, or bytecode enabled interpreter.
- A language with a fully featured core library. You extend it yourself, in Vivi.
- Rigid or locked down, you can hack any part of the language to suit your needs.
- A language that locks you into itself, you can interop.
- Free of opinions, but it is also a lang that doesn't get in your way.

Currently only working on linux atm and not finished (alpha). Run ``./build.sh`` to build, ``./build/vivi examples/showcase.vivi`` to run the showcase

to install the current version of vivi on nixos you add it to your flake and in configuration module.
flake.nix of your system config
`inputs.vivi.url = "github:djxnzi/vivi";`

in your configuration module (make sure `inputs` is passed via specialArgs)
`environment.systemPackages = [
  inputs.vivi.packages.${pkgs.system}.default
];`

---

Syntax Highlighting is available for [vscode](https://marketplace.visualstudio.com/items?itemName=JulianKylander.vivi) and [tree-sitter](https://github.com/jkylander/tree-sitter-vivi), courtesy of [jkylander](https://github.com/jkylander).
