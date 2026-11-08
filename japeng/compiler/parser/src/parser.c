#include "parser.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>

#include "defs.h"
// Flex 側のシンボル
extern int yylex(void);
YYSTYPE yylval;
YYLTYPE yylloc;
Location current_ast_loc = {0};

const char* current_filename = "";

// =============================================================================
// 動的トークン配列の管理 (Flex から全トークンを一括で受け取る用)
// =============================================================================

void token_array_init(TokenArray* arr) {
    arr->tokens = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void token_array_push(TokenArray* arr, Token tok) {
    if (arr->count >= arr->capacity) {
        arr->capacity = (arr->capacity == 0) ? 256 : arr->capacity * 2;
        arr->tokens = (Token*)realloc(arr->tokens, sizeof(Token) * arr->capacity);
    }
    arr->tokens[arr->count++] = tok;
}

void token_array_free(TokenArray* arr) {
    if (arr->tokens != NULL) {
        free(arr->tokens);
        arr->tokens = NULL;
    }
    arr->count = arr->capacity = 0;
}

// =============================================================================
// パーサーの基本操作 API (先読み・消費・エラー同期)
// =============================================================================

void parser_init(Parser* p, Token* tokens, int count, const char* filename) {
    p->tokens     = tokens;
    p->count      = count;
    p->pos        = 0;
    p->filename   = filename;
    p->had_error  = false;
    p->panic_mode = false;
    if (count > 0) {
        current_ast_loc = tokens[0].loc;
    }
}

// 現在位置から distance 個先のトークンを取得 (範囲外なら末尾の EOF トークンを返す)
Token peek(Parser* p, int distance) {
    int target_idx = p->pos + distance;
    if (target_idx >= p->count) {
        return p->tokens[p->count - 1]; // 最後の EOF トークン
    }
    return p->tokens[target_idx];
}

// 現在位置から distance 個先のトークン種別 (type) を直接取得
int peek_type(Parser* p, int distance) {
    return peek(p, distance).type;
}

// 現在のトークンを消費して 1 つ進める
Token advance(Parser* p) {
    Token current = peek(p, 0);
    current_ast_loc = current.loc;
    if (p->pos < p->count - 1) {
        p->pos++;
    }
    return current;
}

// 直前に消費したトークンを取得する
Token previous(Parser* p) {
    if (p->pos == 0) return p->tokens[0];
    return p->tokens[p->pos - 1];
}

// 現在のトークンが expected_type か判定する (進めない)
bool check(Parser* p, int expected_type) {
    return peek_type(p, 0) == expected_type;
}

// 現在のトークンが expected_type なら消費して true、違えば進めずに false
bool match(Parser* p, int expected_type) {
    if (check(p, expected_type)) {
        advance(p);
        return true;
    }
    return false;
}

// 指定したトークンを期待して消費する。無ければエラーを発行してパニックモードへ
Token consume(Parser* p, int expected_type) {
    if (check(p, expected_type)) {
        return advance(p);
    }

    Token bad_tok = peek(p, 0);
    if (!p->panic_mode) {
        p->had_error  = true;
        p->panic_mode = true;
        // 実際のトークン表示名
        const char* actual = (bad_tok.symbol && (bad_tok.type == IDENTIFIER || bad_tok.type == CLASS_NAME))
                             ? bad_tok.symbol->chars
                             : parser_get_token_name(bad_tok.type, compile_lang);

        // 期待していたトークン表示名
        const char* expected = parser_get_token_name(expected_type, compile_lang);

        // compiler-error.c の構文エラー出力へ接続！
        error_syntax_at(bad_tok.loc, ERR_SYNTAX_UNEXPECTED_EXPECTING, actual, expected);
    }
    return bad_tok;
}

// 文法エラー発生時、次の文の開始位置 (セミコロンの直後や制御構文の先頭) まで読み飛ばす
void synchronize(Parser* p) {
    p->panic_mode = false;

    if (peek_type(p, 0) != TOK_EOF) {
        advance(p);
    }
    while (peek_type(p, 0) != TOK_EOF) {
        // 直前が ';' だったなら、すでに次の文の先頭に立っている
        if (previous(p).type == SEMICOLON) return;

        // 次の文の先頭になり得る主要なキーワードが来たら再開
        switch (peek_type(p, 0)) {
            case CLASS:
            case INTERFACE:
            case RETURN:
            case BREAK:
            case CONTINUE:
                return;
            default:
                break;
        }

        advance(p);
    }
}

// ==================
// パーサー本体
// ==================
static inline ASTNode* with_loc(ASTNode* node, Location loc) {
    if (node) node->loc = loc;
    return node;
}

ASTNode* parse_program(Parser* p)
{
    ASTNode* import_stmt = parse_import_stmt_list(p);

    if (import_stmt == NULL) {
        if (check(p, CLASS) || check(p, INTERFACE)) return parse_type_def_list(p);
        else return parse_statement_list(p);
    }

    ASTNode* next_node = NULL;
    if (check(p, CLASS) || check(p, INTERFACE)) next_node = parse_type_def_list(p);
    else next_node = parse_statement_list(p);
    return create_stmt_node(import_stmt, next_node);
}

ASTNode* parse_import_stmt_list(Parser* p)
{
    if (!check(p, IMPORT) && !check(p, FROM)) {
        return NULL;
    }
    ASTNode* root = parse_import_stmt(p);
    while(check(p, IMPORT) || check(p, FROM)) {
        ASTNode* next_node = parse_import_stmt(p);
        root = create_stmt_node(root, next_node);
    }
    return root;
}

ASTNode* parse_import_stmt(Parser* p)
{
    Symbol* path = NULL;
    ASTNode* class_name = NULL;
    ASTNode* alias = NULL;
    if (match(p, IMPORT)) {
        if (check(p, STRING)) path = (consume(p, STRING)).symbol;
        else if (check(p, CLASS_NAME)) {
            Token klass = consume(p, CLASS_NAME);
            class_name = create_identifier_node(klass.symbol);
        } else {
            consume(p, STRING);
             return NULL;
        }
        if (match(p, AS)) {
            Token klass = consume(p, CLASS_NAME);
            alias = create_identifier_node(klass.symbol);
        }
        consume(p, SEMICOLON);
        if (p->panic_mode) return NULL;
        return create_import_node(path, class_name, alias);
    } else if (match(p, FROM)) {
        path = (consume(p, STRING)).symbol;
        consume(p, IMPORT);
        Token klass = consume(p, CLASS_NAME);
        class_name = create_identifier_node(klass.symbol);
        if (match(p, AS)) {
            Token klass = consume(p, CLASS_NAME);
            alias = create_identifier_node(klass.symbol);
        }
        consume(p, SEMICOLON);
        if (p->panic_mode) return NULL;
        return create_import_node(path, class_name, alias);
    } else return NULL;
}

ASTNode* parse_type_def_list(Parser* p)
{
    if (!check(p, CLASS) && !check(p, INTERFACE)) return NULL;

    ASTNode* root = parse_type_def(p);
    while (check(p, CLASS) || check(p, INTERFACE)) {
        ASTNode* next_node = parse_type_def(p);
        root = create_stmt_node(root, next_node);
    }
    return root;
}

ASTNode* parse_type_def(Parser* p)
{
    ASTNode* klass = NULL;
    if (check(p, CLASS)) klass = parse_class_def(p);
    else if (check(p, INTERFACE)) klass = parse_interface_def(p);
    return klass;
}

ASTNode* parse_statement_list(Parser* p)
{
    ASTNode* root = parse_statement(p);
    while (!check(p, TOK_EOF) && !check(p, R_BLACKET) && !check(p, CLASS) && !check(p, INTERFACE)) {
        int prev_pos = p->pos;
        ASTNode* next_node = parse_statement(p);
        if (next_node != NULL) {
            root = (root == NULL) ? next_node : create_stmt_node(root, next_node);
        }
        if (p->pos <= prev_pos) {
            advance(p);
        }
    }
    return root;
}

ASTNode* parse_class_def(Parser* p)
{
    ASTNode* class_def = parse_class_decl(p);
    if (class_def == NULL) return NULL;
    consume(p, COLON);
    ASTNode* members = parse_member_list(p);
    class_def->class_def.members = members;
    if (p->panic_mode) return NULL;
    return class_def;
}

ASTNode* parse_class_decl(Parser* p)
{
    if (!match(p, CLASS)) return NULL;
    ASTNode* klass = parse_type(p);
    ASTNode* super = NULL;
    ASTNode* from = NULL;
    ASTNode* impl = NULL;
    if (match(p, BASED)) super = parse_type(p);
    if (match(p, FROM)) from = parse_type_list_comma(p);
    if (match(p, IMPLEMENTS)) impl = parse_type_list_comma(p);
    return create_classdef_node(false, klass, super, from, impl, NULL);
}

ASTNode* parse_interface_def(Parser* p)
{
    ASTNode* iface_def = parse_interface_decl(p);
    if (iface_def == NULL) return NULL;

    consume(p, COLON);
    ASTNode* members = parse_member_list(p);
    iface_def->class_def.members = members;
    if (p->panic_mode) return NULL;
    return iface_def;
}

ASTNode* parse_interface_decl(Parser* p)
{
    if (!match(p, INTERFACE)) return NULL;
    ASTNode* iface_type = parse_type(p);
    ASTNode* based_list = NULL;

    if (match(p, BASED)) 
        based_list = parse_type_list_comma(p);

    return create_classdef_node(true, iface_type, based_list, NULL, NULL, NULL);
}

ASTNode* parse_member_list(Parser* p)
{
    ASTNode* root = NULL;
    while (!check(p, TOK_EOF) && !check(p, CLASS) && !check(p, INTERFACE)) {
        int prev_pos = p->pos;
        ASTNode* member = parse_member(p);
        if (member != NULL) 
            root = (root == NULL) ? member : create_stmt_node(root, member);
        if (p->pos <= prev_pos) advance(p);
    }
    return root;
}

ASTNode* parse_member(Parser* p)
{
    bool is_static = match(p, STATIC);

    // 1. メンバーは必ず identifier_decl (名前: 型) から始まる
    ASTNode* decl = parse_identifier_decl(p);
    if (decl == NULL) {
        synchronize(p);
        return NULL;
    }

    ASTNode* member_node = NULL;

    // 2. 直後に 'is' または 'are' があれば代入初期化 (field_assignment)
    if (match(p, ASSIGN_IDENTIFIER)) {
        Token op_tok = previous(p); // 直前に消費した 'is' または 'are'
        ASTNode* val = parse_expr_list(p);
        ASTNode* msg = create_identifier_node(op_tok.symbol);
        ASTNode* send = create_send_node(decl, msg, val);

        // 代入初期化の末尾に委譲指定がある場合 
        if (match(p, FROM)) {
            Token from_tok = consume(p, CLASS_NAME);
            ASTNode* from_node = create_identifier_node(from_tok.symbol);
            member_node = create_fielddecl_node(is_static, decl->var_decl.identifier, decl->var_decl.type, from_node, send);
        } else {
            member_node = send;
        }
    } 
    // 3. なければ純粋なフィールド宣言
    else {
        ASTNode* from_node = NULL;
        if (match(p, FROM)) {
            Token from_tok = consume(p, CLASS_NAME);
            from_node = create_identifier_node(from_tok.symbol);
        }
        member_node = create_fielddecl_node(is_static, decl->var_decl.identifier, decl->var_decl.type, from_node, NULL);
    }

    consume(p, SEMICOLON);

    // static フラグの反映
    if (member_node != NULL && is_static) {
        if (member_node->kind == AST_FIELD_DECL) {
            member_node->field_decl.is_static = true;
        } else if (member_node->kind == AST_SEND && member_node->send.receiver && member_node->send.receiver->kind == AST_VAR_DECL) {
            member_node->send.receiver->var_decl.is_static = true;
        }
    }

    if (p->panic_mode) return NULL;
    return member_node;
}

ASTNode* parse_type(Parser* p)
{
    // 1. 可変長型
    if (match(p, ELLIPSIS)) {
        ASTNode* name = create_identifier_node(sym_ellipsis);
        return create_class_node(name, NULL);
    }

    // 2. 括弧で囲まれた型
    if (match(p, L_PAR)) {
        ASTNode* inner_type = parse_type(p);
        consume(p, R_PAR);
        return inner_type;
    }

    // 3. クラス名から始まる型
    if (check(p, CLASS_NAME)) {
        Token name_tok = consume(p, CLASS_NAME);
        ASTNode* name_node = create_identifier_node(name_tok.symbol);

        // 3-A. 関数型シグネチャ
        if (check(p, L_PAR)) {
            // 先読みしてジェネリクス括弧ではなく関数シグネチャか確認
            int d = 1;
            bool is_func_sig = false;
            while (peek_type(p, d) != TOK_EOF && peek_type(p, d) != R_PAR) {
                if (peek_type(p, d) == SEMICOLON) {
                    is_func_sig = true;
                    break;
                }
                d++;
            }

            if (is_func_sig) {
                consume(p, L_PAR);
                ASTNode* args = parse_type_list_commma_opt(p);
                consume(p, SEMICOLON);
                ASTNode* rets = parse_type_list_commma_opt(p);
                consume(p, R_PAR);
                return create_func_type_node(args, rets);
            }
        }

        // 3-B. ジェネリクス型引数
        if (check(p, CLASS_NAME) || check(p, L_PAR)) {
            ASTNode* type_args = parse_type_arg_list(p);
            return create_class_node(name_node, type_args);
        }

        // 3-C. 単独のクラス型
        return create_class_node(name_node, NULL);
    }

    consume(p, CLASS_NAME);
    return NULL;
}

ASTNode* parse_type_arg_list(Parser* p)
{
    ASTNode* root = NULL;
    while (check(p, CLASS_NAME) || (check(p, L_PAR) && peek_type(p, 1) == CLASS_NAME)) {
        ASTNode* item = NULL;
        if (match(p, L_PAR)) {
            item = parse_type(p);
            consume(p, R_PAR);
        } else {
            Token tok = consume(p, CLASS_NAME);
            item = create_class_node(create_identifier_node(tok.symbol), NULL);
        }

        root = (root == NULL) ? item : create_stmt_node(root, item);
    }
    return root;
}

ASTNode* parse_type_list_comma(Parser* p)
{
    ASTNode* root = parse_type(p);
    while (match(p, COMMA)) {
        ASTNode* next_node = parse_type(p);
        root = create_stmt_node(root, next_node);
    }
    return root;
}

ASTNode* parse_type_list_commma_opt(Parser* p)
{
    if (check(p, CLASS_NAME) || check(p, L_PAR) || check(p, ELLIPSIS)) {
        return parse_type_list_comma(p);
    }
    return NULL;
}

static bool is_control_stmt_node(ASTNode* node) {
    if (node == NULL) return false;

    if (node->kind == AST_STMT) {
        return is_control_stmt_node(node->stmt.left);
    }

    if (node->kind == AST_SEND && node->send.message != NULL) {
        Symbol* msg = node->send.message->identifier.name;
        if (msg == sym_if || msg == sym_repeat || sym_switch ||
            msg == sym_elif || msg == sym_else || msg == sym_each ||
            msg == sym_case || msg == sym_with) {
            return true;
        }
    }
    return false;
}

ASTNode* parse_statement(Parser* p)
{
    // 1. break / continue
    if (match(p, BREAK)) {
        consume(p, SEMICOLON);
        return create_break_node();
    }
    if (match(p, CONTINUE)) {
        consume(p, SEMICOLON);
        return create_continue_node();
    }

    // 2. return [expr_list]? ;
    if (match(p, RETURN)) {
        ASTNode* ret_vals = NULL;
        if (!check(p, SEMICOLON)) {
            ret_vals = parse_expr_list(p);
        }
        consume(p, SEMICOLON);
        return create_return_node(ret_vals);
    }

    // 3. 複数変数宣言 / 複数代入
    if (check(p, IDENTIFIER) && peek_type(p, 1) == COMMA) {
        ASTNode* multi_ids = parse_multi_identifier(p);

        // A. a, b: Type ... (型宣言付き)
        if (match(p, COLON)) {
            ASTNode* type_node = parse_type(p);
            ASTNode* decl = create_vardecl_node(multi_ids, type_node);

            // a, b: Integer is 1, 2;
            if (match(p, ASSIGN_IDENTIFIER)) {
                Token op = previous(p);
                ASTNode* vals = parse_expr_list(p);
                consume(p, SEMICOLON);
                return create_send_node(decl, create_identifier_node(op.symbol), vals);
            }
            // a, b: Integer;
            consume(p, SEMICOLON);
            return decl;
        }

        // B. 宣言なしの複数代入
        if (match(p, ASSIGN_IDENTIFIER)) {
            Token op = previous(p);
            ASTNode* vals = parse_expr_list(p);
            consume(p, SEMICOLON);
            return create_send_node(multi_ids, create_identifier_node(op.symbol), vals);
        }
    }

    // 4. 単一変数宣言
    if ((check(p, IDENTIFIER) || check(p, SP_IDENTIFIER)) && peek_type(p, 1) == COLON) {
        ASTNode* decl = parse_identifier_decl(p);

        // x: Integer is 10;
        if (match(p, ASSIGN_IDENTIFIER)) {
            Token op = previous(p);
            ASTNode* vals = parse_expr_list(p);
            consume(p, SEMICOLON);
            return create_send_node(decl, create_identifier_node(op.symbol), vals);
        }

        // x: Integer;
        consume(p, SEMICOLON);
        return decl;
    }

    // 5. 代入または通常の式文
    ASTNode* expr = parse_expression(p);
    ASTNode* stmt = NULL;

    // A. 直後に 'is' または 'are' がある場合 ➜ 代入文
    if (match(p, ASSIGN_IDENTIFIER)) {
        Token op_tok  = previous(p);
        ASTNode* vals = parse_expr_list(p);
        ASTNode* msg  = create_identifier_node(op_tok.symbol);
        stmt = create_send_node(expr, msg, vals);
        stmt->loc = expr->loc;
    } 
    // B. なければ通常の式文
    else stmt = expr;

    if (is_control_stmt_node(stmt)) {
        match(p, SEMICOLON); 
    } else {
        consume(p, SEMICOLON);
    }

    if (p->panic_mode) {
        synchronize(p);
        return NULL;
    }
    return stmt;
}

ASTNode* parse_identifier_decl(Parser* p)
{
    Token id_tok;
    if (check(p, IDENTIFIER)) {
        id_tok = consume(p, IDENTIFIER);
    } else if (check(p, SP_IDENTIFIER)) {
        id_tok = consume(p, SP_IDENTIFIER);
    } else {
        consume(p, IDENTIFIER);
        return NULL;
    }

    consume(p, COLON);
    ASTNode* type_node = parse_type(p);
    return create_vardecl_node(create_identifier_node(id_tok.symbol), type_node);
}

ASTNode* parse_multi_identifier(Parser* p)
{
    Token first = consume(p, IDENTIFIER);
    consume(p, COMMA);
    Token second = consume(p, IDENTIFIER);
    ASTNode* root = create_stmt_node(create_identifier_node(first.symbol), create_identifier_node(second.symbol));

    while (match(p, COMMA)) {
        Token next_tok = consume(p, IDENTIFIER);
        root = create_stmt_node(root, create_identifier_node(next_tok.symbol));
    }
    return root;
}

ASTNode* parse_expression(Parser* p)
{
    ASTNode* left = parse_message(p);

    // 二項演算子 (+, -, *, /, ==, !=, and, or など)
    while (check(p, SP_IDENTIFIER)) {
        Token op_tok = consume(p, SP_IDENTIFIER);
        ASTNode* op_node = create_identifier_node(op_tok.symbol);
        op_node->loc = op_tok.loc;

        ASTNode* right = parse_message(p);
        
        ASTNode* send = create_send_node(left, op_node, right);
        send->loc = op_tok.loc;
        left = send;
    }

    return left;
}

ASTNode* parse_receiver(Parser* p)
{
    if ((check(p, IDENTIFIER) || check(p, SP_IDENTIFIER)) && peek_type(p, 1) == COLON) {
        return parse_identifier_decl(p);
    }
    return parse_primary(p);
}

ASTNode* parse_message(Parser* p)
{
    // 1. 制御構文チェーンの判定: if(c)[...] や repeat[...]
    if (check(p, IDENTIFIER) && peek_type(p, 1) == L_PAR) {
        int d = 2;
        int paren_depth = 1;
        while (peek_type(p, d) != TOK_EOF) {
            if (peek_type(p, d) == L_PAR) paren_depth++;
            else if (peek_type(p, d) == R_PAR) {
                paren_depth--;
                if (paren_depth == 0) break; 
            }
            d++;
        }

        if (paren_depth == 0 && peek_type(p, d + 1) == L_BLACKET) {
            return parse_control_chain(p);
        }
    }

    // 2. 暗黙の self メソッド呼び出し: foo(...)
    ASTNode* node = NULL;
    if (check(p, IDENTIFIER) && peek_type(p, 1) == L_PAR) {
        Token msg_tok = consume(p, IDENTIFIER);
        Location call_loc = msg_tok.loc;
        consume(p, L_PAR);
        ASTNode* args = NULL;
        if (!check(p, R_PAR)) {
            args = parse_expr_list(p);
        }
        consume(p, R_PAR);

        ASTNode* self_node = create_identifier_node(sym_self);
        self_node->loc = call_loc;
        ASTNode* msg = create_identifier_node(msg_tok.symbol);
        msg->loc = call_loc;
        node = create_send_node(self_node, msg, args);
        node->loc = call_loc;
    } else {
        // 3. 通常のレシーバー取得
        node = parse_receiver(p);
    }

    // メソッド呼び出しやメソッドチェーンのループ
    while (true) {
        // A. ドット付きメソッド呼出: node . method(...) または node . + expr
        if (match(p, DOT)) {
            if (check(p, IDENTIFIER)) {
                Token msg_tok = consume(p, IDENTIFIER);
                ASTNode* msg_node = create_identifier_node(msg_tok.symbol);
                msg_node->loc = msg_tok.loc;

                ASTNode* args = NULL;
                if (match(p, L_PAR)) {
                    if (!check(p, R_PAR)) args = parse_expr_list(p);
                    consume(p, R_PAR);
                }
                if (check(p, L_BLACKET)) {
                    ASTNode* blk = parse_block(p);
                    args = (args == NULL) ? blk : create_stmt_node(args, blk);
                }
                ASTNode* send = create_send_node(node, msg_node, args);
                send->loc = msg_tok.loc;
                node = send;
            } else if (check(p, SP_IDENTIFIER)) {
                Token op_tok = consume(p, SP_IDENTIFIER);
                ASTNode* op_node = create_identifier_node(op_tok.symbol);
                op_node->loc = op_tok.loc;

                ASTNode* right = parse_expression(p);
                ASTNode* send = create_send_node(node, op_node, right);
                send->loc = op_tok.loc;
                node = send;
            }
            continue;
        }
        // B. ドットなしメソッド呼出: node method(...) または node method [block]
        if (check(p, IDENTIFIER)) {
            Token msg_tok = consume(p, IDENTIFIER);
            ASTNode* msg_node = create_identifier_node(msg_tok.symbol);
            msg_node->loc = msg_tok.loc;

            ASTNode* args = NULL;
            if (match(p, L_PAR)) {
                if (!check(p, R_PAR)) args = parse_expr_list(p);
                consume(p, R_PAR);
                ASTNode* send = create_send_node(node, msg_node, args);
                send->loc = msg_tok.loc; // ★
                node = send;
            } else if (check(p, L_BLACKET)) {
                args = parse_block(p);
                ASTNode* send = create_send_node(node, msg_node, args);
                send->loc = msg_tok.loc; // ★
                node = send;
            } else {
                ASTNode* send = create_send_node(node, msg_node, NULL);
                send->loc = msg_tok.loc; // ★
                node = send;
            }
            continue;
        }

        break;
    }

    return node;
}

ASTNode* parse_control_chain(Parser* p)
{
    Token id_tok = consume(p, IDENTIFIER);
    ASTNode* msg = create_identifier_node(id_tok.symbol);
    ASTNode* cond = NULL;

    if (match(p, L_PAR)) {
        if (!check(p, R_PAR)) {
            cond = parse_expr_list(p);
        }
        consume(p, R_PAR);
    }

    ASTNode* blk = parse_block(p);
    ASTNode* args = (cond == NULL) ? blk : create_stmt_node(cond, blk);

    ASTNode* self_node = create_identifier_node(sym_self);
    ASTNode* first_clause = create_send_node(self_node, msg, args);
    ASTNode* clauses = NULL;

    while (check(p, IDENTIFIER)) {
        Token next_id = peek(p, 0);
        bool is_clause_kw = (next_id.symbol == sym_elif || next_id.symbol == sym_else);

        if (!is_clause_kw) break;

        int next1 = peek_type(p, 1);
        if (next1 == L_PAR || next1 == L_BLACKET) {
            ASTNode* clause = parse_control_clause(p);
            if (clauses == NULL) {
                clauses = clause;
            } else {
                clauses = create_stmt_node(clauses, clause);
            }
        } else {
            break;
        }
    }

    if (clauses != NULL) {
        return create_stmt_node(first_clause, clauses);
    }
    return first_clause;
}

ASTNode* parse_control_clause(Parser* p)
{
    Token id_tok = consume(p, IDENTIFIER);
    ASTNode* msg = create_identifier_node(id_tok.symbol);
    ASTNode* cond = NULL;

    // '(' があれば条件式を取得 (else の場合は省略される)
    if (match(p, L_PAR)) {
        if (!check(p, R_PAR)) {
            cond = parse_expr_list(p);
        }
        consume(p, R_PAR);
    }

    ASTNode* blk = parse_block(p);
    ASTNode* args = (cond == NULL) ? blk : create_stmt_node(cond, blk);

    // 後続節はレシーバーが NULL
    return create_send_node(NULL, msg, args);
    
}

ASTNode* parse_primary(Parser* p)
{
    ASTNode* node = NULL;
    // 1. 単項演算子: -, +, ~, !
    if (check(p, SP_IDENTIFIER)) {
        Token op_tok = advance(p);
        ASTNode* operand = parse_primary(p);

        if (op_tok.symbol == sym_minus) {
            // 数値リテラルへの即時マイナス適用
            if (operand->kind == AST_LITERAL && 
               (operand->literal.kind == TOKEN_INTEGER || operand->literal.kind == TOKEN_FLOAT)) {
                char buf[1024];
                snprintf(buf, sizeof(buf), "-%s", operand->literal.raw->chars);
                Symbol* neg_raw = new_string(buf, (int)strlen(buf));
                node = create_literal_node(operand->literal.kind, neg_raw);
            } else {
                ASTNode* zero = create_literal_node(TOKEN_INTEGER, new_string("0", 1));
                node = create_send_node(zero, create_identifier_node(sym_minus), operand);
            }
        } else if (op_tok.symbol == sym_plus) {
            node = operand;
        } else if (op_tok.symbol == sym_bnot || op_tok.symbol == sym_not) {
            node = create_send_node(operand, create_identifier_node(op_tok.symbol), NULL);
        }
        return node;
    }

    // 2. リテラル
    if (check(p, INTEGER) || check(p, FLOAT) || check(p, FRACTION) ||
        check(p, BINARY) || check(p, HEX) || check(p, TRUE_LITERAL) ||
        check(p, FALSE_LITERAL) || check(p, NULL_LITERAL)) {
        node = parse_literal(p);
    }
    // 3. 文字列補間
    else if (check(p, STR_OPEN)) {
        node = parse_interpolated_string(p);
    }
    // 4. self
    else if (match(p, SELF)) {
        node = create_identifier_node(sym_self);
    }
    // 5. 識別子
    else if (match(p, IDENTIFIER)) {
        node = create_identifier_node(previous(p).symbol);
    }
    // 6. クラスインスタンス化: ClassName(...) または ClassName TypeArgs(...) または クラスオブジェクト単体
   else if (check(p, CLASS_NAME)) {
        Token name_tok = consume(p, CLASS_NAME);
        ASTNode* name_node = create_identifier_node(name_tok.symbol);
        ASTNode* type_args = NULL;

        if (check(p, CLASS_NAME)) {
            // 型引数の後ろにコンストラクタ呼び出し '(' があるか確認
            int d = 1;
            while (peek_type(p, d) != TOK_EOF && peek_type(p, d) != L_PAR && peek_type(p, d) != SEMICOLON) d++;
            if (peek_type(p, d) == L_PAR) {
                type_args = parse_type_arg_list(p);
            }
        }

        ASTNode* klass = create_class_node(name_node, type_args);

        // 引数リスト '(' ... ')' があればコンストラクタ呼び出し
        if (match(p, L_PAR)) {
            ASTNode* args = NULL;
            if (!check(p, R_PAR)) {
                args = parse_expr_list(p);
            }
            consume(p, R_PAR);
            node = create_construct_node(klass, args);
        } else {
            // 引数がなければクラス名ノード単体 (レシーバー等)
            node = (type_args != NULL) ? klass : name_node;
        }
    }
    // 7. ブロック [ ... ]
    else if (check(p, L_BLACKET)) {
        node = parse_block(p);
    }
    // 8. 丸括弧 ( expr )
    else if (match(p, L_PAR)) {
        node = parse_expression(p);
        consume(p, R_PAR);
    }

    // 添字アクセス: primary "[" expr "]" の左結合ループ
    while (match(p, L_BLACKET)) {
        ASTNode* index_expr = parse_expression(p);
        consume(p, R_BLACKET);
        node = create_index_access_node(node, index_expr);
    }

    return node;
}

ASTNode* parse_literal(Parser* p)
{
    Token tok = advance(p);
    switch (tok.type) {
        case INTEGER:       return create_literal_node(TOKEN_INTEGER, tok.symbol);
        case FLOAT:         return create_literal_node(TOKEN_FLOAT, tok.symbol);
        case FRACTION:      return create_literal_node(TOKEN_FRACTION, tok.symbol);
        case BINARY:        return create_literal_node(TOKEN_BINARY, tok.symbol);
        case HEX:           return create_literal_node(TOKEN_HEX, tok.symbol);
        case TRUE_LITERAL:  return create_literal_node(TOKEN_TRUE, tok.symbol);
        case FALSE_LITERAL: return create_literal_node(TOKEN_FALSE, tok.symbol);
        case NULL_LITERAL:  return create_literal_node(TOKEN_NULL, tok.symbol);
        default:            return NULL;
    }
}

ASTNode* parse_interpolated_string(Parser* p)
{
    consume(p, STR_OPEN);
    ASTNode* root = create_literal_node(TOKEN_STRING, new_string("", 0));

    while (!check(p, STR_CLOSE) && !check(p, TOK_EOF)) {
        if (check(p, STR_TEXT)) {
            Token text_tok = consume(p, STR_TEXT);
            ASTNode* text_node = create_literal_node(TOKEN_STRING, text_tok.symbol);

            // 先頭が空文字列なら置き換え、既に文字があれば + で結合 (最適化)
            if (root->kind == AST_LITERAL && root->literal.kind == TOKEN_STRING && root->literal.raw->length == 0) {
                root = text_node;
            } else {
                ASTNode* plus_op = create_identifier_node(sym_plus);
                root = create_send_node(root, plus_op, text_node);
            }
        } else if (match(p, STR_EXPR_OPEN)) {
            ASTNode* expr = parse_expression(p);
            consume(p, STR_EXPR_CLOSE);

            // expr.toString() ノードを合成して + で結合
            ASTNode* to_str_msg = create_identifier_node(intern_cstr("toString"));
            ASTNode* str_expr   = create_send_node(expr, to_str_msg, NULL);

            if (root->kind == AST_LITERAL && root->literal.kind == TOKEN_STRING && root->literal.raw->length == 0) {
                root = str_expr;
            } else {
                ASTNode* plus_op = create_identifier_node(sym_plus);
                root = create_send_node(root, plus_op, str_expr);
            }
        } else {
            break;
        }
    }

    consume(p, STR_CLOSE);
    return root;
}

ASTNode* parse_block(Parser* p)
{
    consume(p, L_BLACKET);

    ASTNode* args = NULL;
    ASTNode* rets = NULL;

    // arg 引数リスト ;
    if (match(p, ARG)) {
        args = parse_arg_decl_list(p);
        consume(p, SEMICOLON);
    }

    // ret 戻り値型リスト ;
    if (match(p, RET)) {
        rets = parse_type_list_comma(p);
        consume(p, SEMICOLON);
    }

    ASTNode* stmts = parse_statement_list_opt(p);
    consume(p, R_BLACKET);

    return create_block_node(args, rets, stmts);
}

ASTNode* parse_arg_decl_list(Parser* p)
{
    ASTNode* root = parse_arg_decl(p);
    while (match(p, COMMA)) {
        ASTNode* next_node = parse_arg_decl(p);
        root = create_stmt_node(root, next_node);
    }
    return root;
}

ASTNode* parse_arg_decl(Parser* p)
{
    // 1. 可変長引数: ...
    if (match(p, ELLIPSIS)) {
        ASTNode* name = create_identifier_node(sym_ellipsis);
        ASTNode* type = create_class_node(create_identifier_node(sym_ellipsis), NULL);
        return create_vardecl_node(name, type);
    }

    // 2. 通常引数
    Token id_tok;
    if (check(p, IDENTIFIER)) {
        id_tok = consume(p, IDENTIFIER);
    } else if (check(p, SP_IDENTIFIER)) {
        id_tok = consume(p, SP_IDENTIFIER);
    } else {
        consume(p, IDENTIFIER);
        return NULL;
    }

    consume(p, COLON);
    ASTNode* type_node = parse_type(p);
    ASTNode* decl = create_vardecl_node(create_identifier_node(id_tok.symbol), type_node);

    // デフォルト引数: name: Type is expr
    if (match(p, ASSIGN_IDENTIFIER)) {
        ASTNode* default_val = parse_expression(p);
        decl->var_decl.default_val = default_val;
    }

    return decl;
}

ASTNode* parse_expr_list(Parser* p)
{
    ASTNode* root = parse_expression(p);
    while (match(p, COMMA)) {
        ASTNode* next_node = parse_expression(p);
        root = create_stmt_node(root, next_node);
    }
    return root;
}

ASTNode* parse_statement_list_opt(Parser* p)
{
    if (check(p, R_BLACKET) || check(p, TOK_EOF)) return NULL;
    return parse_statement_list(p);
}

// Bison 互換エントリポイント (parse.c からそのまま呼ばれる)
int yyparse(void) {
    TokenArray arr;
    token_array_init(&arr);

    // 1. Flex から全トークンを一括収集
    int token_type;
    while ((token_type = yylex()) != 0) {
        Token tok;
        tok.type   = token_type;
        tok.loc    = current_ast_loc;

        // トークン種別に応じた値の退避
        if (token_type == INTEGER || token_type == FLOAT || 
            token_type == TRUE_LITERAL || token_type == FALSE_LITERAL || 
            token_type == NULL_LITERAL || token_type == BINARY || token_type == HEX) {
            tok.symbol = yylval.literal;
        } else {
            tok.symbol = yylval.symbol;
        }

        token_array_push(&arr, tok);
    }

    // printf("=== DUMP TOKENS (%d tokens) ===\n", arr.count);
    // for (int i = 0; i < arr.count; i++) {
    //     printf("[%3d] line:%-3d col:%-3d type:%-3d name:%-15s sym:%s\n",
    //            i,
    //            arr.tokens[i].loc.line,
    //            arr.tokens[i].loc.column,
    //            arr.tokens[i].type,
    //            parser_get_token_name(arr.tokens[i].type, compile_lang),
    //            arr.tokens[i].symbol ? arr.tokens[i].symbol->chars : "null");
    // }
    // printf("===============================\n\n");

    // 2. 終端 EOF トークンを追加
    Token eof_tok;
    eof_tok.type   = TOK_EOF;
    eof_tok.loc    = current_ast_loc;
    eof_tok.symbol = NULL;
    token_array_push(&arr, eof_tok);

    // 3. 手書きパーサーを実行
    Parser p;
    parser_init(&p, arr.tokens, arr.count, current_filename);
    ASTNode* ast = parse_program(&p);

    // 4. 結果を parse.c が待つグローバル変数へ格納
    current_parsed_ast = ast;

    // 5. 後始末
    token_array_free(&arr);

    if (p.had_error || ast == NULL) {
        return 1; // Bison の構文エラー終了コード
    }

    return 0; // 成功
}
