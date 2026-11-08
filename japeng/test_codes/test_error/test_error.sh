#!/bin/bash

TARGET="./jpen"

if [ ! -x "$TARGET" ]; then
    TARGET="/jpen"
fi

if [ ! -x "$TARGET" ]; then
    echo -e "\033[1;31mエラー: 実行バイナリが見つかりません。\033[0m"
    exit 1
fi

echo "=========================================="
echo "          JapEng エラー自動テスト"
echo "=========================================="

passed=0
failed=0

# macOS でも 1 -> 2 -> ... -> 10 の順になるよう sort -V を使用
for file in $(ls case*.je 2>/dev/null | sort -V); do
    echo "------------------------------------------"
    echo -e "\033[1;34m[テスト]: $file\033[0m"

    # 概要（タイトル）の取得
    title=$(grep -m 1 "^//.*テスト" "$file" | sed 's|^//\s*||')
    if [ -n "$title" ]; then
        echo -e "  概要: $title"
    fi

    # 表記揺れ（期待される出力 / 期待出力 / 期待される結果 等）に対応し、
    # 前後の空白や \r (CRLF) を完全にトリムして抽出
    expected=$(awk '
        /期待.*(出力|結果)/ {
            # 1行で書かれている場合 (例: * 期待される出力: エラー: ...)
            if ($0 ~ /[:：]/) {
                sub(/^.*[:：][ \t]*/, "");
                sub(/[ \t]*\*\//, "");
                print;
                exit;
            }
            flag=1;
            next;
        }
        flag && /\*\// { flag=0 }
        flag {
            gsub(/^[ \t]*\*[ \t]*/, "");
            if (length) { print; exit }
        }
    ' "$file" | tr -d '\r' | sed 's/^[ \t]*//;s/[ \t]*$//')

    echo -e "\033[1;33m[期待]\033[0m $expected"

    # 実行して出力から \r を除去
    output=$($TARGET -ja "$file" 2>&1 | tr -d '\r' | sed $'s/\033\\[[0-9;]*m//g')

    # 判定
    if [ -n "$expected" ] && echo "$output" | grep -Fq "$expected"; then
        echo -e "\033[1;32m[判定] PASS\033[0m"
        ((passed++))
    else
        echo -e "\033[1;31m[判定] FAIL\033[0m"
        echo -e "\033[1;30m--- 実際の出力 ---\033[0m"
        echo "$output"
        echo -e "\033[1;30m-------------------\033[0m"
        ((failed++))
    fi
done

echo "=========================================="
echo -e "結果: \033[1;32m$passed 成功\033[0m / \033[1;31m$failed 失敗\033[0m"
echo "=========================================="
