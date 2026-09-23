CC      = clang
CFLAGS  = -Wall -g -Icommon/include -Iparser/include -Iparser/src -Icompiler/include -Ivm/include
LD      = clang
LDFLAGS = -Wall

LEX     = flex
YAC     = bison -d -v

# 生成するバイナリ
TARGET_PARSE = jpen       # 既存: パーサー単体用
TARGET_RUN   = jpen-run   # 新規: AST -> Compiler -> VM 統合実行用

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

# vm (dispatch サブディレクトリも含む)
VM_SRCS = $(wildcard vm/src/*.c) $(wildcard vm/src/builtin_functions/*.c)
VM_OBJS = $(VM_SRCS:.c=.o)

# ----------------------------------------------------
# リンク対象の振り分け
# ----------------------------------------------------

# 1. 既存パーサーテスト (parser/parser-main.c + symbol.c のみ)
PARSER_MAIN      = parser/parser-main.o
PARSER_ONLY_OBJS = $(PARSER_MAIN) $(PARSER_OBJS) common/src/symbol.o

# 2. 統合テスト (test_cat.c + 全モジュール)
RUN_MAIN = test_cat.o
RUN_OBJS = $(RUN_MAIN) $(PARSER_OBJS) $(COMPILER_OBJS) $(VM_OBJS) $(COMMON_OBJS)

# ----------------------------------------------------
# ビルドターゲット
# ----------------------------------------------------

all: $(TARGET_PARSE) $(TARGET_RUN)

# 既存のパーサー単体バイナリ
$(TARGET_PARSE): $(PARSER_ONLY_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# 今回の AST -> Compiler -> VM 統合バイナリ
$(TARGET_RUN): $(RUN_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# 1. Bison: parser.y -> y.tab.c, y.tab.h
parser/src/y.tab.c parser/src/y.tab.h: parser/src/parser.y
	$(YAC) -o parser/src/y.tab.c $<

# 2. Flex: rex.l -> lex.yy.c (y.tab.h の生成後に実行)
parser/src/lex.yy.c: parser/src/rex.l parser/src/y.tab.h
	$(LEX) -o $@ $<

# 3. parser.y が "lex.yy.c" を include しているため、コンパイル前に生成を保証
parser/src/y.tab.o: parser/src/y.tab.c parser/src/lex.yy.c
	$(CC) $(CFLAGS) -c $< -o $@

# 汎用コンパイルルール
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 統合テストのワンアクション実行
run: $(TARGET_RUN)
	./$(TARGET_RUN) test_code/test.je

clean:
	rm -f $(TARGET_PARSE) $(TARGET_RUN)
	rm -f parser/src/y.tab.c parser/src/y.tab.h parser/src/y.output
	rm -f parser/src/lex.yy.c
	rm -f test_cat.o parser/*.o parser/src/*.o
	rm -f common/src/*.o
	rm -f compiler/src/*.o
	rm -f vm/src/*.o vm/src/builtin_functions/*.o

.PHONY: all clean run
