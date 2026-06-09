TARGET	= jpen

SRCDIR = src
INCDIR = include

# LINK (*.o)
LD	= clang
LDFLAGS = -Wall

# COMPILE (*.c)
CC	= clang
CCFLAGS = -Wall -I$(INCDIR) -I. -g
CCTEMPS	= *.o *.s *.i *.bc

LEX	= flex
YAC	= bison --yacc

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = y.tab.o main.o $(SRCS:$(SRCDIR)/%.c=%.o)

DEFS	= defs.h
REXP	= rex.l
LEXC	= lex.yy.c
SYNS	= parser.y
YACC	= y.tab.c
YACH 	= y.tab.h

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(OBJS): $(DEFS)

lex.yy.o : $(LEXC) $(YACH)
	@$(CC) $(CCFLAGS) -c $< -o $@

y.tab.o: $(YACC)
	@$(CC) $(CCFLAGS) -c $< -o $@

%.o: $(SRCDIR)/%.c
	@$(CC) $(CCFLAGS) -c $< -o $@

main.o: main.c
	@$(CC) $(CCFLAGS) -c $< -o $@

$(LEXC): $(REXP) $(DEFS)
	@$(LEX) $(REXP)

$(YACC): $(SYNS) $(LEXC) $(DEFS)
	@$(YAC) $(SYNS)

clean:
	@rm -f $(TARGET) $(OBJS) $(LEXC) $(YACC) $(TMP) $(CCTEMPS) 

.PHONY: all clean
