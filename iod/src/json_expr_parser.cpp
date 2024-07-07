#include "json_expr_parser.h"
#include <boost/context/fiber.hpp>
#include <cJSON.h>
#include <iostream>
#include <sstream>
#include <string>

// Boost.Context reference: https://www.boost.org/doc/libs/1_84_0/libs/context/doc/html/context/ff.html

// Grammar: { } => 0 or more, ( ) => group, | => or
// expr = root | root member
// member = { ( "[" (key | symbol) "]" | "." field ) }
// root = "$"
// var  = alpha { alpha | digit | "_"}
// field = var | number
// key = "*" | var | number
// symbol = "@" alpha { alpha | digit | "_"}

// Throughout the parser, the input stream gets marked whenever the input stream
// is at a potential end point for the expression. After marking, if the parser
// fails, the stream is reset and the parse is regarged as done.

Parser::Parser(InputStream &is_, std::function<void(char, Parser::TokenType kind)> cb_,
               std::function<void(size_t, Parser::TokenType kind)> cb_number_,
               std::function<void(std::string, Parser::TokenType kind)> cb_string_)
    : next(), is(is_), cb(cb_), cb_number(cb_number_), cb_string(cb_string_) {}

char Parser::pull() { return is.get(); }

void Parser::scan() {
    do {
        next = pull();
    } while (isspace(next));
}

void Parser::run() {
    scan();
    if (next == '$') {
        root();
        if (!is.eof()) {
            member();
        }
    }
    else if (next != '[') {
        if (next == '@') {
            symbol();
        }
        else {
            var();
        }
    }
    if (!is.eof()) {
        member();
    }
}

std::ostream &Parser::display(std::ostream &out, Parser::TokenType kind) {
    switch (kind) {
    case TokenType::expr:
        out << "expr";
        break;
    case TokenType::root:
        out << "root";
        break;
    case TokenType::introducer:
        out << "introducer";
        break;
    case TokenType::member:
        out << "member";
        break;
    case TokenType::var:
        out << "var";
        break;
    case TokenType::subs_begin:
        out << "subs_begin";
        break;
    case TokenType::key:
        out << "key";
        break;
    case TokenType::subs_end:
        out << "subs_end";
        break;
    case TokenType::index:
        out << "index";
        break;
    case TokenType::wildcard:
        out << "wildcard";
        break;
    case TokenType::symbol:
        out << "symbol";
        break;
    case TokenType::symbol_begin:
        out << "symbol_begin";
        break;
    };
    return out;
}

std::ostream &operator<<(std::ostream &out, Parser::TokenType kind) {
    return Parser::display(out, kind);
}

void Parser::root() {
    if (next == '$') {
        cb(next, TokenType::root);
        scan();
    }
    else {
        throw std::runtime_error("no root node");
    }
}

void Parser::member() {
    while (!is.eof()) {
        if (next == '[') {
            cb(next, TokenType::subs_begin);
            scan();
            if (next == '@') {
                symbol();
            }
            else {
                key();
            }
            if (next != ']') {
                throw std::runtime_error("parse error");
            }
            is.mark();
            cb(next, TokenType::subs_end);
            scan();
        }
        else {
            field();
            if (!is.eof() && next != '[' && next != '.') {
                is.reset();
                return; // This is the end of the JSON path
            }
        }
    }
}

void Parser::field() {
    while (next == '.') {
        cb(next, TokenType::introducer);
        scan();
        if (next == '@') {
           symbol();
        }
        else {
            var();
        }
        is.mark();
    }
}

void Parser::symbol() {
    cb(next, TokenType::symbol_begin);
    scan();
    // TODO: refactor.. this is a copy of var()
    std::stringstream ss;
    while (!is.eof() && (isalpha(next) || isdigit(next) || next == '_')) {
        ss << next;
        next = pull();
    }
    value = ss.str();
    cb_string(value, TokenType::symbol);
    is.mark();
    if (next == ' ') {
        scan();
    }
}

void Parser::var() {
    if (isalpha(next)) {
        std::stringstream ss;
        while (!is.eof() && (isalpha(next) || isdigit(next) || next == '_')) {
            ss << next;
            next = pull();
        }
        value = ss.str();
        cb_string(value, TokenType::var);
        is.mark();
        if (next == ' ') {
            scan();
        }
    }
    else {
        throw std::runtime_error("parse error");
    }
}

void Parser::number() {
    if (isdigit(next)) {
        size_t index = 0;
        while (!is.eof() && isdigit(next)) {
            index = index * 10 + next - '0';
            next = pull();
        }
        cb_number(index, TokenType::index);
    }
    else {
        throw std::runtime_error("parse error");
    }
}

void Parser::key() {
    if (isdigit(next)) {
        number();
    }
    else if (isalpha(next)) {
        std::stringstream ss;
        while (!is.eof() && (isalpha(next) || isdigit(next) || next == '_')) {
            ss << next;
            next = pull();
        }
        value = ss.str();
        cb_string(value, TokenType::key);
        if (next == ' ') {
            scan();
        }
    }
    else if (next == '*') {
        cb(next, TokenType::wildcard);
        scan();
    }
    else {
        throw std::runtime_error("parsing failed");
    }
}
