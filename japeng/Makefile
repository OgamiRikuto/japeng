CC       = clang
LEX      = flex

GLFW_INC = -Ithird_party/glfw/include
GLFW_LIB = third_party/glfw/lib/libglfw3.a -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore

CFLAGS   = -Wall -O3 -flto -g \
           -DGL_SILENCE_DEPRECATION \
           $(GLFW_INC) \
           -Icommon/include \
           -Icompiler/include \
           -Icompiler/parser/include \
           -Icompiler/parser/src \
           -Icompiler/src \
           -Ivm/include
LDFLAGS  = -Wall -O3 -flto
LD       = clang

# CFLAGS  = -Wall -O1 -g -fno-omit-frame-pointer -fsanitize=address \
#           -DGL_SILENCE_DEPRECATION \
#           $(GLFW_INC) \
#           -Icommon/include \
#           -Icompiler/include \
#           -Icompiler/parser/include \
#           -Icompiler/parser/src \
#           -Icompiler/src \
#           -Ivm/include
# LDFLAGS = -Wall -O1 -g -fsanitize=address

# 生成するバイナリ
TARGET_COMPILER = jpenc         # コンパイラ本体
TARGET_RUN      = jpen          # VM ランタイム
TARGET_PARSE    = jpen-parse    # パーサー単体テスト

BUILTIN_CD      = lib/builtins.cd lib/Fraction.cd

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
VM_SRCS     = $(wildcard vm/src/*.c) \
              $(wildcard vm/src/builtin_functions/*.c) \
              $(wildcard vm/src/builtin_functions/std/*.c) \
              $(wildcard vm/src/builtin_functions/gui/*.c)
VM_OBJS     = $(VM_SRCS:.c=.o)

# compiler コア
COMPILER_SRCS = $(wildcard compiler/src/*.c)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)

# Flex (字句解析器)
LEX_GEN_C   = compiler/parser/src/lex.yy.c
LEX_OBJS    = $(LEX_GEN_C:.c=.o)

# parser 共通基盤 (parser-main.c と自動生成の lex.yy.c は除外して重複を防ぐ)
PARSER_MAIN_C    = compiler/parser/src/parser-main.c
PARSER_ALL_C     = $(wildcard compiler/parser/src/*.c)
PARSER_BASE_SRCS = $(filter-out $(PARSER_MAIN_C) $(LEX_GEN_C), $(PARSER_ALL_C))
PARSER_OBJS      = $(PARSER_BASE_SRCS:.c=.o) $(LEX_OBJS)

# ==============================================================================
# 2. 実行バイナリのリンク構成
# ==============================================================================
COMPILER_MAIN_OBJ = compiler-main.o
COMPILER_BIN_OBJS = $(COMPILER_MAIN_OBJ) $(COMPILER_OBJS) $(PARSER_OBJS) $(COMMON_OBJS)

VM_MAIN_OBJ       = vm-main.o
VM_BIN_OBJS       = $(VM_MAIN_OBJ) $(VM_OBJS) $(COMMON_OBJS)

PARSER_MAIN_OBJ   = $(PARSER_MAIN_C:.c=.o)
PARSER_BIN_OBJS   = $(PARSER_MAIN_OBJ) $(COMPILER_OBJS) $(PARSER_OBJS) $(COMMON_OBJS)

# ==============================================================================
# 3. ビルドターゲット
# ==============================================================================

all: $(TARGET_COMPILER) $(TARGET_RUN) builtins

parse: $(TARGET_PARSE)

$(TARGET_COMPILER): $(COMPILER_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

$(TARGET_RUN): $(VM_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^ $(GLFW_LIB)

$(TARGET_PARSE): $(PARSER_BIN_OBJS)
	@$(LD) $(LDFLAGS) -o $@ $^

# ==============================================================================
# 4. 生成・コンパイルルール
# ==============================================================================

$(LEX_GEN_C): compiler/parser/src/rex.l
	@$(LEX) -o $@ $<

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@

builtins: $(TARGET_COMPILER) $(BUILTIN_CD)
	@echo "Compiling built-in classes to .bcd..."
	@./$(TARGET_COMPILER) $(BUILTIN_CD)

clean:
	@rm -f $(TARGET_PARSE) $(TARGET_COMPILER) $(TARGET_RUN)
	@rm -f $(LEX_GEN_C)
	@rm -f *.o compiler/parser/src/*.o compiler/src/*.o common/src/*.o \
	       vm/src/*.o vm/src/builtin_functions/*.o vm/src/builtin_functions/*/*.o
	@rm -rf .je_cache */.je_cache */*/.je_cache

.PHONY: all clean parse builtins
