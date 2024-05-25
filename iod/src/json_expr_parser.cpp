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

Parser::Parser(std::istream &is_,
                std::function<void(char, Parser::TokenType kind)> cb_,
                std::function<void(std::string, Parser::TokenType kind)> cb2_)
    : next(), is(is_), cb(cb_), cb2(cb2_) {}

Parser::Parser(const char *&input,
                std::function<void(char, Parser::TokenType kind)> cb_,
                std::function<void(std::string, Parser::TokenType kind)> cb2_)
    : next(), input(input), cb(cb_), cb2(cb2_) {}

bool Parser::is_eof() {
    if (is) {
        return is->eof();
    }
    else {
        return *(*input) == '\0';
    }
}

char Parser::pull() {
    if (is) {
        return std::char_traits<char>::to_char_type(is->get()); 
    }
    else {
        return *(*input)++;
    }
}

void Parser::scan() {
    do {
        next = pull();
    } while (isspace(next));
}

void Parser::run() {
    scan();
    root();
    if (!is_eof()) {
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
    while ( (is && !is->eof() ) || (input && next != '\0') ) {
        if (next == '[') {
            cb(next, TokenType::subs_begin);
            scan();
            key();
            if (next != ']') {
                std::cerr << "member: " << next << std::endl;
                throw std::runtime_error("parse error");
            }
            cb(next, TokenType::subs_end);
            scan();
        }
        else {
            while (next == '.') {
                cb(next, TokenType::introducer);
                scan();
                var();
            }
            if (!is_eof() && next != '[' && next != '.') {
                std::cerr << "member: " << next << std::endl;
                //throw std::runtime_error("syntax error");
                return; // This is the end of the JSON path
            }
        }
    }
}

void Parser::var() {
    if (isalpha(next)) {
        std::stringstream ss;
        while (!is_eof() && (isalpha(next) || isdigit(next) || next == '_')) {
            ss << next;
            next = pull();
        }
        value = ss.str();
        cb2(value, TokenType::var);
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
        while (!is_eof() && (isalpha(next) || isdigit(next) || next == '_')) {
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
