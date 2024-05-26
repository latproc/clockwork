#include <cJSON.h>
#include <iostream>
#include <sstream>
#include <string>
#include <boost/context/fiber.hpp>
#include "json_expr_parser.h"

// Boost.Context reference: https://www.boost.org/doc/libs/1_84_0/libs/context/doc/html/context/ff.html

// Grammar: { } => 0 or more, ( ) => group, | => or
// expr = root | root member
// member = { ( "[" key "]" | "." var ) }
// root = "$"
// var  = alpha { alpha | digit | "_"}
// key = "*" | var | number

Parser::Parser(InputStream &is_,
                std::function<void(char, Parser::TokenType kind)> cb_,
                std::function<void(std::string, Parser::TokenType kind)> cb2_)
    : next(), is(is_), cb(cb_), cb2(cb2_) {}

char Parser::pull() {
    return is.get();
}

void Parser::scan() {
    do {
        next = pull();
    } while (isspace(next));
}

void Parser::run() {
    scan();
    root();
    if (!is.eof()) {
        member();
    }
}

std::ostream & Parser::display(std::ostream & out, Parser::TokenType kind) {
     switch(kind) {
         case TokenType::expr: out << "expr"; break;
         case TokenType::root: out << "root"; break;
         case TokenType::introducer: out << "introducer"; break;
         case TokenType::member: out << "member"; break;
         case TokenType::var: out << "var"; break;
         case TokenType::subs_begin: out << "subs_begin"; break;
         case TokenType::key: out << "key"; break;
         case TokenType::subs_end: out << "subs_end"; break;
         case TokenType::index: out << "index"; break;
         case TokenType::wildcard: out << "wildcard"; break;
     };
     return out;
}

std::ostream & operator<<(std::ostream & out, Parser::TokenType kind) {
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
            key();
            if (next != ']') {
                std::cerr << "member: " << next << std::endl;
                throw std::runtime_error("parse error");
            }
            is.mark();
            cb(next, TokenType::subs_end);
            scan();
        }
        else {
            while (next == '.') {
                cb(next, TokenType::introducer);
                scan();
                var();
            }
            if (!is.eof() && next != '[' && next != '.') {
                is.reset();
                return; // This is the end of the JSON path
            }
        }
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
        cb2(value, TokenType::var);
        is.mark();
        if (next == ' ') { scan(); }
    }
    else {
        std::cerr << "var: " << next << std::endl;
        throw std::runtime_error("parse error");
    }
}

void Parser::key() {
    if (isdigit(next)) {
        cb(next, TokenType::index);
        scan();
    }
    else if (isalpha(next)) {
        std::stringstream ss;
        while (!is.eof() && (isalpha(next) || isdigit(next) || next == '_')) {
            ss << next;
            next = pull();
        }
        value = ss.str();
        cb2(value, TokenType::key);
        if (next == ' ') { scan(); }
    }
    else if (next == '*') {
        cb(next, TokenType::wildcard);
        scan();
    }
    else {
        std::cerr << "key: " << next << std::endl;
        throw std::runtime_error("parsing failed");
    }
}
