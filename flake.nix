{
  description = "librnt C library";

  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.gccStdenv.mkDerivation {
            pname = "librnt";
            version = "0.1.0";
            src = self;

            strictDeps = true;
            enableParallelBuilding = true;
            makeFlags = [
              "CC=${pkgs.gccStdenv.cc}/bin/${pkgs.gccStdenv.cc.targetPrefix}cc"
              "PREFIX=$(out)"
            ];

            meta = {
              description = "librnt C library";
              license = pkgs.lib.licenses.lgpl21Only;
              platforms = systems;
            };
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          gcc = pkgs.gccStdenv.cc;
          clangTools = pkgs.symlinkJoin {
            name = "clang-tools-gcc";
            paths = [ pkgs.clang-tools ];
            nativeBuildInputs = [ pkgs.makeWrapper ];
            postBuild = ''
              rm $out/bin/clangd
              makeWrapper ${pkgs.clang-tools}/bin/clangd $out/bin/clangd \
                --prefix PATH : ${gcc}/bin \
                --add-flags "--query-driver=${gcc}/bin/${gcc.targetPrefix}gcc"
            '';
          };
        in
        {
          default = (pkgs.mkShell.override { stdenv = pkgs.gccStdenv; }) {
            packages = with pkgs; [
              gnumake
              clangTools
            ];
          };
        });
    };
}
