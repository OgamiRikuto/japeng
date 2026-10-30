CC      = clang
CFLAGS  = -Wall -O3 -flto -g -Icommon/include -Iparser/include -Iparser/src -Icompiler/include -Ivm/include
LD      = clang
LDFLAGS = -Wall -O3 -flto

# CFLAGS = -Wall -g -O0 -Icommon/include -Iparser/include -Iparser/src -Icompiler/include -Ivm/include
# LDFLAGS = -Wall -g -O0

LEX     = flex
YAC     = bison -d -v

# 生成するバイナリ
TARGET_PARSE = jpen-parse       # 既存: パーサー単体用
TARGET_COMPILER = jpenc
TARGET_RUN   = jpen   # 新規: AST -> Compiler -> VM 統合実行用

BUILTIN_CD      = lib/builtins.cd

# インストール先設定
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib/japeng

# ----------------------------------------------------
# 各ディレクトリのソースとオブジェクト
# ----------------------------------------------------

# parser
PARSER_SRCS = $(filter-out parser/src/y.tab.c parser/src/lex.yy.c, $(wildcard parser/src/*.c))
PARSER_OBJS = $(PARSER_SRCS:.c=.o) parser/src/y.tab.o

# common (全ソース)
COMMON_SRCS = $(wildcard common/src/*.c)
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# compiler
COMPILER_SRCS = $(wildcard compiler/src/*.c)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)

COMPILER_MAIN = compile-main.o
COMPILER_BIN_OBJS = $(COMPILER_MAIN) $(PARSER_OBJS) $(COMPILER_OBJS) $(COMMON_OBJS)

VM_MAIN     = vm-main.o
VM_BIN_OBJS = $(VM_MAIN) $(VM_OBJS) $(COMMON_OBJS)

# vm (dispatch サブディレクトリも含む)
VM_SRCS = $(wildcard vm/src/*.c) $(wildcard vm/src/builtin_functions/*.c)
VM_OBJS = $(VM_SRCS:.c=.o)

# ----------------------------------------------------
# リンク対象の振り分け
# ----------------------------------------------------

# 1. 既存パーサーテスト (parser/parser-main.c + symbol.c のみ)
PARSER_MAIN      = parser/parser-main.o
PARSER_ONLY_OBJS = $(PARSER_MAIN) $(PARSER_OBJS) common/src/symbol.o common/src/object.o common/src/error.o common/src/builtin_convert.o common/src/bigint.o common/src/literal.o

# 2. 統合テスト (test_cat.c + 全モジュール)
RUN_MAIN = main.o
RUN_OBJS = $(RUN_MAIN) $(PARSER_OBJS) $(COMPILER_OBJS) $(VM_OBJS) $(COMMON_OBJS)

# ----------------------------------------------------
# ビルドターゲット
# ----------------------------------------------------

all: $(TARGET_PARSE) $(TARGET_COMPILER) $(TARGET_RUN) builtins

debug: 

# 既存のパーサー単体バイナリ
$(TARGET_PARSE): $(PARSER_ONLY_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

# コンパイラ単体ビルド
$(TARGET_COMPILER): $(COMPILER_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

# # 今回の AST -> Compiler -> VM 統合バイナリ
# $(TARGET_RUN): $(RUN_OBJS)
# 	@$(LD) $(LDFLAGS) -o $@ $^

$(TARGET_RUN): $(VM_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

# 1. Bison: parser.y -> y.tab.c, y.tab.h
parser/src/y.tab.c parser/src/y.tab.h: parser/src/parser.y
	@$(YAC) -o parser/src/y.tab.c $<

# 2. Flex: rex.l -> lex.yy.c (y.tab.h の生成後に実行)
parser/src/lex.yy.c: parser/src/rex.l parser/src/y.tab.h
	@$(LEX) -o $@ $<

# 3. parser.y が "lex.yy.c" を include しているため、コンパイル前に生成を保証
parser/src/y.tab.o: parser/src/y.tab.c parser/src/lex.yy.c
	@$(CC) $(CFLAGS) -c $< -o $@

# 汎用コンパイルルール
%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@

# 統合テストのワンアクション実行
run: $(TARGET_RUN)
	@./$(TARGET_RUN) test_code/test.je

builtins: $(TARGET_COMPILER) $(BUILTIN_CD)
	@echo "Compiling built-in classes to .bcd..."
	@./$(TARGET_COMPILER) $(BUILTIN_CD)

install: all
	@echo "Installing JapEng to $(PREFIX)..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(LIBDIR)
	@install -m 755 $(TARGET_COMPILER) $(DESTDIR)$(BINDIR)/$(TARGET_COMPILER)
	@install -m 755 $(TARGET_RUN)      $(DESTDIR)$(BINDIR)/$(TARGET_RUN)
	@install -m 644 lib/.je_cache/*.bcd $(DESTDIR)$(LIBDIR)/
	@echo "Installation complete."

uninstall:
	@echo "Uninstalling JapEng from $(PREFIX)..."
	@rm -f $(DESTDIR)$(BINDIR)/$(TARGET_COMPILER)
	@rm -f $(DESTDIR)$(BINDIR)/$(TARGET_RUN)
	@rm -rf $(DESTDIR)$(LIBDIR)
	@echo "Uninstall complete."

clean:
	@rm -f $(TARGET_PARSE) $(TARGET_COMPILER) $(TARGET_RUN)
	@rm -f parser/src/y.tab.c parser/src/y.tab.h parser/src/y.output
	@rm -f parser/src/lex.yy.c
	@rm -f main.o compile-main.o vm-main.o parser/*.o parser/src/*.o
	@rm -f common/src/*.o
	@rm -f compiler/src/*.o
	@rm -f vm/src/*.o vm/src/builtin_functions/*.o
	@rm -rf .je_cache */.je_cache */*/.je_cache

.PHONY: all clean run
