CC      = clang
CFLAGS  = -Wall -O3 -flto -g \
          -Icommon/include \
          -Icompiler/include \
          -Icompiler/parser/include \
          -Icompiler/parser/src \
          -Icompiler/src \
          -Ivm/include
LD      = clang
LDFLAGS = -Wall -O3 -flto

# CFLAGS = -Wall -g -O0 
#           -Icommon/include \
#           -Icompiler/include \
#           -Icompiler/parser/include \
#           -Icompiler/parser/src \
#           -Icompiler/src \
#           -Ivm/include
# LDFLAGS = -Wall -g -O0

LEX     = flex
YAC     = bison -d -v

# 生成するバイナリ
TARGET_COMPILER = jpenc         # コンパイラ本体
TARGET_RUN      = jpen          # VM ランタイム
TARGET_PARSE    = jpen-parse    # パーサー単体テスト

BUILTIN_CD      = lib/

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib/japeng

# ==============================================================================
# 1. ソースコードの収集
# ==============================================================================

# common (共通基盤)
COMMON_SRCS = $(wildcard common/src/*.c)
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# vm (純粋な実行エンジン)
VM_SRCS     = $(wildcard vm/src/*.c) $(wildcard vm/src/builtin_functions/*.c)
VM_OBJS     = $(VM_SRCS:.c=.o)

# compiler コア
COMPILER_SRCS = $(wildcard compiler/src/*.c)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)

# parser 共通基盤 (前処理・整形・AST構築補助)
PARSER_BASE_SRCS = \
	compiler/parser/src/ast.c \
    compiler/parser/src/parse.c \
    compiler/parser/src/ja_preprocessor.c \
    compiler/parser/src/preprocessor.c \
    compiler/parser/src/formatter.c \
    compiler/parser/src/refine.c
PARSER_BASE_OBJS = $(PARSER_BASE_SRCS:.c=.o)

# Flex (字句解析器)
LEX_GEN_C   = compiler/parser/src/lex.yy.c
LEX_OBJS    = $(LEX_GEN_C:.c=.o)

# ==============================================================================
# 2. パーサー実装の切り替え (手書き vs Bison)
# ==============================================================================
# デフォルトは手書きパーサー (hand)
PARSER_TYPE ?= hand

# A. 手書き再帰下降パーサー (デフォルト)
HAND_PARSER_SRCS = \
    compiler/parser/src/parser.c \
    compiler/parser/src/parser-dict.c
HAND_PARSER_OBJS = $(HAND_PARSER_SRCS:.c=.o)

# B. Bison レガシーパーサー (いつでも削除可能)
BISON_GEN_C = compiler/parser/src/y.tab.c
BISON_GEN_H = compiler/parser/src/y.tab.h
BISON_OBJS  = compiler/parser/src/y.tab.o

# 切り替えロジック
ifeq ($(PARSER_TYPE), bison)
    PARSER_IMPL_OBJS = $(BISON_OBJS)
else
    PARSER_IMPL_OBJS = $(HAND_PARSER_OBJS)
endif

PARSER_OBJS = $(PARSER_BASE_OBJS) $(PARSER_IMPL_OBJS) $(LEX_OBJS)

# ==============================================================================
# 3. 実行バイナリのリンク構成
# ==============================================================================
COMPILER_MAIN_OBJ = compiler-main.o
COMPILER_BIN_OBJS = $(COMPILER_MAIN_OBJ) $(COMPILER_OBJS) $(PARSER_OBJS) $(COMMON_OBJS)

VM_MAIN_OBJ       = vm-main.o
VM_BIN_OBJS       = $(VM_MAIN_OBJ) $(VM_OBJS) $(COMMON_OBJS)

PARSER_MAIN_OBJ   = compiler/parser/src/parser-main.o
PARSER_BIN_OBJS   = $(PARSER_MAIN_OBJ) $(COMPILER_OBJS) $(PARSER_OBJS) $(COMMON_OBJS)

# ==============================================================================
# 4. ビルドターゲット
# ==============================================================================

# 通常ビルド (手書きパーサー)
all: $(TARGET_COMPILER) $(TARGET_RUN) builtins

# パーサー単体テスト (jpen-parse)
parse: $(TARGET_PARSE)

# Bison版でビルドしたい場合はこれを叩く
bison:
	@$(MAKE) PARSER_TYPE=bison all

bison-parse:
	@$(MAKE) PARSER_TYPE=bison parse

# リンク
$(TARGET_COMPILER): $(COMPILER_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

$(TARGET_RUN): $(VM_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

$(TARGET_PARSE): $(PARSER_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

# ==============================================================================
# 5. 生成・コンパイルルール
# ==============================================================================

# Flex 生成
$(LEX_GEN_C): compiler/parser/src/rex.l
	@$(LEX) -o $@ $<

# ------------------------------------------------------------------------------
# [Bison セクション] 
# ------------------------------------------------------------------------------
# $(BISON_GEN_C) $(BISON_GEN_H): compiler/parser/src/parser.y
# 	@$(YAC) -o $(BISON_GEN_C) $<

# compiler/parser/src/y.tab.o: $(BISON_GEN_C)
# 	@$(CC) $(CFLAGS) -c $< -o $@
# ------------------------------------------------------------------------------

# 汎用ルール
%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@

# 組み込みクラスのコンパイル
builtins: $(TARGET_COMPILER) $(BUILTIN_CD)
	@echo "Compiling built-in classes to .bcd..."
	@./$(TARGET_COMPILER) -d lib/

clean:
	@rm -f $(TARGET_PARSE) $(TARGET_COMPILER) $(TARGET_RUN)
	@rm -f compiler/parser/src/y.tab.c compiler/parser/src/y.tab.h compiler/parser/src/y.output
	@rm -f compiler/parser/src/lex.yy.c
	@rm -f *.o compiler/parser/src/*.o compiler/src/*.o vm/src/*.o vm/src/builtin_functions/*.o common/src/*.o
	@rm -rf .je_cache */.je_cache */*/.je_cache

.PHONY: all clean parse bison bison-parse builtins
