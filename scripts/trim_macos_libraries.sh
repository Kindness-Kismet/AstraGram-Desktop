#!/usr/bin/env bash
set -euo pipefail

libraries_dir=${1:-Libraries}
test -d "$libraries_dir"
strip_tool=${STRIP:-strip}

before=$(du -sk "$libraries_dir" | cut -f1)

find "$libraries_dir" \
  '(' \
    '(' \
      ! '(' \
        -name '*.a' \
        -o -name '*.h' \
        -o -name '*.hpp' \
        -o -name '*.inc' \
        -o -name '*.cmake' \
        -o -name '*.pc' \
        -o -path '*/include/*' \
        -o -path '*/objects-*' \
        -o -path '*/cache_keys/*' \
        -o -path '*/patches/*' \
        -o -perm +111 \
      ')' \
      -type f \
    ')' \
    -o -empty \
  ')' \
  -delete

# Release 构建不会发布依赖调试信息，缓存前移除它以免挤占 Actions 配额。
# 遇到 strip 不支持的旧架构文件时跳过该文件，继续处理其余文件。
strip_failures=0
while IFS= read -r -d '' file; do
  if ! "$strip_tool" -S "$file"; then
    echo "::warning file=${file}::Skipping dependency file unsupported by strip"
    strip_failures=$((strip_failures + 1))
  fi
done < <(find "$libraries_dir" -type f \( -name '*.a' -o -name '*.o' \) -print0)
echo "Skipped strip files: $strip_failures"

after=$(du -sk "$libraries_dir" | cut -f1)
echo "Libraries cache: $((before * 1024)) -> $((after * 1024)) bytes"
