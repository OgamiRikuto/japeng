# TARGET	= jpen

# SRCDIR = src
# INCDIR = include

# # LINK (*.o)
# LD	= clang
# LDFLAGS = -Wall

# # COMPILE (*.c)
# CC	= clang
# CCFLAGS = -Wall -I$(INCDIR) -I. -g
# CCTEMPS	= *.o *.s *.i *.bc

# LEX	= flex
# YAC	= bison --yacc -v

# SRCS = $(wildcard $(SRCDIR)/*.c)
# OBJS = y.tab.o main.o $(SRCS:$(SRCDIR)/%.c=%.o)

# DEFS	= defs.h
# REXP	= rex.l
# LEXC	= lex.yy.c
# SYNS	= parser.y
# YACC	= y.tab.c
# YACH 	= y.tab.h

# all: $(TARGET)

# $(TARGET): $(OBJS)
# 	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

# $(OBJS): $(DEFS)

# lex.yy.o : $(LEXC) $(YACH)
# 	@$(CC) $(CCFLAGS) -c $< -o $@

# y.tab.o: $(YACC)
# 	@$(CC) $(CCFLAGS) -c $< -o $@

# %.o: $(SRCDIR)/%.c
# 	@$(CC) $(CCFLAGS) -c $< -o $@

# main.o: main.c
# 	@$(CC) $(CCFLAGS) -c $< -o $@

# $(LEXC): $(REXP) $(DEFS)
# 	@$(LEX) $(REXP)

# $(YACC): $(SYNS) $(LEXC) $(DEFS)
# 	@$(YAC) $(SYNS)

# clean:
# 	@rm -f $(TARGET) $(OBJS) $(LEXC) $(YACC) $(TMP) $(CCTEMPS) y.output

# .PHONY: all clean

TARGET  = jpen

CC      = clang
CFLAGS  = -Wall -g -Icommon/include -Iparser/include -Iparser/src
LD      = clang
LDFLAGS = -Wall

LEX     = flex
YAC     = bison -d -v

# エントリポイント
PARSER_MAIN = parser/parser-main.o

# parser/src/*.c（ast.c, parse.c 等）を自動取得
PARSER_SRCS = $(filter-out parser/src/y.tab.c parser/src/lex.yy.c, $(wildcard parser/src/*.c))
PARSER_OBJS = $(PARSER_SRCS:.c=.o) parser/src/y.tab.o

# common/src は含めず、parser 関連のみをリンク
OBJS        = $(PARSER_MAIN) $(PARSER_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
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

# 汎用ルール
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -f parser/src/y.tab.c parser/src/y.tab.h parser/src/y.output
	rm -f parser/src/lex.yy.c
	rm -f parser/*.o parser/src/*.o

.PHONY: all clean
