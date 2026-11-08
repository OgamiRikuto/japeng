#!/usr/bin/env python3
import sys
import os
import subprocess
import argparse
import difflib
from pathlib import Path

GREEN = "\033[1;32m"
RED = "\033[1;31m"
YELLOW = "\033[1;33m"
CYAN = "\033[1;36m"
BOLD = "\033[1m"
RESET = "\033[0m"

class TestCase:
    def __init__(self, name: str, path: Path, is_dir_project: bool, is_ja: bool, entry_script: Path = None):
        self.name = name
        self.path = path
        self.is_dir_project = is_dir_project
        self.is_ja = is_ja
        self.entry_script = entry_script  # 実行対象スクリプト (.je / .jje)

        if is_dir_project:
            self.expected_file = path / "expected.out"
        else:
            self.expected_file = path.with_suffix(".expected")

        # .in 入力ファイルの自動探索
        self.in_file = None
        if is_dir_project:
            for candidate in ["test.in", "input.in", f"{path.name}.in"]:
                p = path / candidate
                if p.exists():
                    self.in_file = p
                    break
        else:
            p = path.with_suffix(".in")
            if p.exists():
                self.in_file = p

def discover_tests(base_dir: Path):
    tests = []
    
    for root, dirs, files in os.walk(base_dir):
        if ".je_cache" in root or "__pycache__" in root:
            continue
            
        rpath = Path(root)
        cd_files = [f for f in files if f.endswith((".cd", ".jcd"))]
        je_files = [f for f in files if f.endswith((".je", ".jje"))]
        
        # 1. .cd / .jcd を含むディレクトリプロジェクト
        if cd_files:
            is_ja = any(f.endswith((".jcd", ".jje")) for f in files)
            rel_name = str(rpath.relative_to(base_dir))
            
            # エントリースクリプトを特定 (main > test > 最初に見つかったスクリプト)
            entry = None
            for cand in ["main.je", "main.jje", "test.je", "test.jje"]:
                if cand in je_files:
                    entry = rpath / cand
                    break
            if not entry and je_files:
                entry = rpath / sorted(je_files)[0]
                
            tests.append(TestCase(rel_name, rpath, True, is_ja, entry))
            dirs.clear()
            continue

        # 2. 単体スクリプト
        for jf in je_files:
            fpath = rpath / jf
            is_ja = jf.endswith(".jje")
            rel_name = str(fpath.relative_to(base_dir))
            tests.append(TestCase(rel_name, fpath, False, is_ja, fpath))

    return sorted(tests, key=lambda t: t.name)

def normalize_output(text: str) -> str:
    lines = []
    for line in text.splitlines():
        if "Pure VM Time:" in line:
            line = line.split("Pure VM Time:")[0].rstrip()
        if "Compilation finished:" in line or "コンパイル完了:" in line:
            continue
        lines.append(line)
    return "\n".join(lines).strip() + "\n"

def run_pipeline(test: TestCase, jpenc_bin: Path, jpen_bin: Path, timeout_sec: int):
    target_arg = str(test.path) + ("/" if test.is_dir_project else "")

    # =========================================================================
    # ステップ 1: jpenc --clean で古いキャッシュを破棄
    # =========================================================================
    clean_cmd = [str(jpenc_bin), "--clean", target_arg]
    try:
        subprocess.run(
            clean_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout_sec
        )
    except subprocess.TimeoutExpired:
        return clean_cmd, -1, f"Clean timed out ({timeout_sec}s).\n"

    # =========================================================================
    # ステップ 2: jpenc による新規コンパイル
    # =========================================================================
    compile_cmd = [str(jpenc_bin)]
    compile_cmd.append("-ja")
    if test.is_dir_project:
        compile_cmd.extend(["-d", target_arg])
    else:
        compile_cmd.append(target_arg)

    try:
        c_res = subprocess.run(
            compile_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout_sec
        )
    except subprocess.TimeoutExpired:
        return compile_cmd, -1, f"Compile timed out ({timeout_sec}s).\n"

    # コンパイルエラー（構文・型エラー等）の場合はここで終了
    if c_res.returncode != 0:
        actual_output = normalize_output(c_res.stdout)
        snapshot = "[EXIT: ERROR]\n" + actual_output
        return compile_cmd, c_res.returncode, snapshot

    # 実行スクリプト (.je) が存在しないプロジェクトはコンパイル成功で終了
    if not test.entry_script:
        actual_output = normalize_output(c_res.stdout)
        snapshot = "[EXIT: OK]\n" + actual_output
        return compile_cmd, 0, snapshot

    # =========================================================================
    # ステップ 3: jpen による実行 (-d を使わずスクリプトファイルを直接指定)
    # =========================================================================
    run_cmd = [str(jpen_bin)]
    run_cmd.append("-ja")
    run_cmd.append(str(test.entry_script))  # ★ -d ではなく .je/.jje を直指定

    stdin_data = None
    if test.in_file and test.in_file.exists():
        stdin_data = test.in_file.read_text(encoding="utf-8")

    try:
        r_res = subprocess.run(
            run_cmd,
            input=stdin_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout_sec
        )
        actual_output = normalize_output(r_res.stdout)
        code = r_res.returncode
    except subprocess.TimeoutExpired:
        return run_cmd, -1, f"Runtime timed out ({timeout_sec}s).\n"

    status_header = f"[EXIT: {'OK' if code == 0 else 'ERROR'}]\n"
    snapshot = status_header + actual_output
    return run_cmd, code, snapshot

def main():
    parser = argparse.ArgumentParser(description="JapEng Test Suite Runner")
    parser.add_argument("--record", "-record", "-r", action="store_true", help="Record outputs as snapshots")
    parser.add_argument("--test", "-test", "-t", "--check", "-check", action="store_true", help="Verify against snapshots")
    parser.add_argument("--filter", "-filter", "-f", type=str, default="", help="Filter tests by name")
    parser.add_argument("--bin", "-bin", "-b", type=str, default="./jpen", help="Path to jpen (default: ./jpen)")
    parser.add_argument("--compiler", "-c", type=str, default="./jpenc", help="Path to jpenc (default: ./jpenc)")
    parser.add_argument("--dir", "-dir", "-d", type=str, default="test_codes", help="Test directory (default: test_codes)")
    parser.add_argument("--timeout", "-timeout", type=int, default=5, help="Timeout in seconds (default: 5)")
    args = parser.parse_args()

    jpen_bin = Path(args.bin).resolve()
    jpenc_bin = Path(args.compiler).resolve()
    test_dir = Path(args.dir).resolve()

    if not jpenc_bin.exists() or not jpen_bin.exists():
        print(f"{RED}Error: Binaries not found. (jpenc: {jpenc_bin.exists()}, jpen: {jpen_bin.exists()}){RESET}")
        print("Run 'make' first.")
        sys.exit(1)

    all_tests = discover_tests(test_dir)
    if args.filter:
        all_tests = [t for t in all_tests if args.filter in t.name]

    # モード 1: オプションなし (全実行・結果垂れ流し)
    if not args.test and not args.record:
        print(f"{BOLD}=== JapEng Test Suite (Run-Only: {len(all_tests)} tests) ==={RESET}\n")
        for i, test in enumerate(all_tests, 1):
            cmd, code, output = run_pipeline(test, jpenc_bin, jpen_bin, args.timeout)
            status_color = GREEN if code == 0 else RED
            in_info = f" {CYAN}(stdin: {test.in_file.name}){RESET}" if test.in_file else ""
            
            print(f"{BOLD}[{i}/{len(all_tests)}] {test.name}{RESET}{in_info} ➜ {status_color}[EXIT: {code}]{RESET}")
            if output:
                body = "\n".join(output.splitlines()[1:])
                if body.strip():
                    for line in body.splitlines():
                        print(f"    {line}")
            print()
        return

    # モード 2: --record モード
    if args.record:
        print(f"{BOLD}=== JapEng Test Suite (Record Mode: {len(all_tests)} tests) ==={RESET}\n")
        for test in all_tests:
            cmd, code, output = run_pipeline(test, jpenc_bin, jpen_bin, args.timeout)
            test.expected_file.write_text(output, encoding="utf-8")
            status = "OK" if code == 0 else "ERROR"
            in_info = f" (stdin: {test.in_file.name})" if test.in_file else ""
            print(f"[{YELLOW}RECORDED{RESET}] {test.name} [{status}] -> {test.expected_file.name}{in_info}")
        print(f"\n{GREEN}Successfully recorded {len(all_tests)} snapshot(s).{RESET}")
        return

    # モード 3: --test モード
    print(f"{BOLD}=== JapEng Test Suite (Verification Mode: {len(all_tests)} tests) ==={RESET}\n")
    passed = 0
    failed = 0

    for test in all_tests:
        cmd, code, actual_snapshot = run_pipeline(test, jpenc_bin, jpen_bin, args.timeout)

        if not test.expected_file.exists():
            print(f"[{RED}FAIL{RESET}] {test.name}")
            print(f"    Expected snapshot not found: {test.expected_file.name}")
            print(f"    (Run with -record to generate it)")
            failed += 1
            continue

        expected_snapshot = test.expected_file.read_text(encoding="utf-8")

        if actual_snapshot == expected_snapshot:
            in_info = f" {CYAN}(stdin: {test.in_file.name}){RESET}" if test.in_file else ""
            print(f"[{GREEN}PASS{RESET}] {test.name}{in_info}")
            passed += 1
        else:
            print(f"[{RED}FAIL{RESET}] {test.name}")
            diff = difflib.unified_diff(
                expected_snapshot.splitlines(keepends=True),
                actual_snapshot.splitlines(keepends=True),
                fromfile="expected",
                tofile="actual"
            )
            for line in "".join(diff).splitlines():
                print(f"    {line}")
            failed += 1

    print(f"\n{BOLD}Summary:{RESET} {GREEN}{passed} Passed{RESET}, {RED}{failed} Failed{RESET}, {len(all_tests)} Total")
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()
