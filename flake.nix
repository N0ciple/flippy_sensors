{
  description = "Environnement de développement avec uv";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShell = pkgs.mkShell {
          buildInputs = with pkgs; [
            python3
            python3Packages.uv
          ];
          # shellHook = ''
          # uv tool install ufbt
          # '';
          # --> run uvx ufbt instead
        };
      });
}