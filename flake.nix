{
  description = "Vivi language";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});
    in {
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "vivi";
          version = "0.1.0";
          src = self;

          # the makefile already uses g++, stdenv provides it
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin $out/lib
            cp build/vivi $out/bin/
            cp -r lib/* $out/lib/        # gives $out/lib/core/io.vivi etc.
            runHook postInstall
          '';
        };
      });

      apps = forAll (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.system}.default}/bin/vivi";
        };
      });

      overlays.default = final: prev: {
        vivi = self.packages.${final.system}.default;
      };

      devShells = forAll (pkgs: {
        default = pkgs.mkShell { packages = [ pkgs.gnumake pkgs.gcc ]; };
      });
    };
}
