#!/usr/bin/env bash
# ONNX Runtime für CI holen: erst das GitHub-Release, sonst das offizielle
# NuGet-Paket (gleiche Bibliotheken + Header). Ergebnis: <ziel>/include, <ziel>/lib
# Aufruf: fetch-ort.sh <version> <plattform: linux-x64|osx-universal2> <ziel>
set -euo pipefail
ver="$1"; plat="$2"; dest="$3"
tmp="$(mktemp -d)"
url="https://github.com/microsoft/onnxruntime/releases/download/v${ver}/onnxruntime-${plat}-${ver}.tgz"
if curl -fsSL --retry 4 --retry-delay 8 --retry-all-errors -o "$tmp/ort.tgz" "$url" && tar xzf "$tmp/ort.tgz" -C "$tmp"; then
  rm -rf "$dest"; mv "$tmp/onnxruntime-${plat}-${ver}" "$dest"
  echo "ONNX Runtime $ver von GitHub"
  exit 0
fi
echo "GitHub-Download fehlgeschlagen – nehme NuGet"
curl -fsSL --retry 6 --retry-delay 10 --retry-all-errors -o "$tmp/ort.nupkg" "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/${ver}"
mkdir -p "$tmp/pkg" && (cd "$tmp/pkg" && unzip -q ../ort.nupkg)
rm -rf "$dest"; mkdir -p "$dest/include" "$dest/lib"
cp -a "$tmp/pkg/build/native/include/." "$dest/include/"
case "$plat" in
  linux-x64) cp -a "$tmp/pkg/runtimes/linux-x64/native/." "$dest/lib/" ;;
  osx*)      cp -a "$tmp/pkg/runtimes/osx-arm64/native/." "$dest/lib/" ;;
esac
# Datei so nennen, wie die Bibliothek sich selbst nennt (Install-Name / SONAME)
for f in "$dest"/lib/*.dylib "$dest"/lib/*.so; do
  [ -f "$f" ] || continue
  case "$f" in
    *.dylib) name="$(otool -D "$f" | tail -1)" ;;
    *.so)    name="$(objdump -p "$f" | awk '/SONAME/{print $2}')" ;;
  esac
  name="$(basename "${name:-$f}")"
  [ -e "$dest/lib/$name" ] || cp "$f" "$dest/lib/$name"
done
ls -la "$dest/lib"
echo "ONNX Runtime $ver von NuGet"
