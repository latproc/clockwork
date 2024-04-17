#pragma once
#include <ostream>
#include <sstream>
#include <functional>

// A JSON expression is an expression that can be applied to a JSON
// document to extract a value or a subdocument.
//
// This implemetation only supports single character member names and
// single digit array indices.
class Parser {
 
    // Grammar: { } => 0 or more, ( ) => group, | => or
    // expr = root | root member 
    // member = { ( "[" key "]" | "." var ) }
    // root = "$"
    // var  = alpha
    // key = "*" | var || number

  public:
    enum class TokenType { expr, root, introducer, member, var, subs_begin, key, subs_end, index, wildcard};

    Parser(std::istream &is_, std::function<void(char, TokenType kind)> cb_);

    void run();

    static std::ostream & display(std::ostream & out, TokenType kind);

  private:

    char next;
    std::istream &is;
    std::function<void(char, TokenType)> cb;

    char pull();
    void scan();
    void root();
    void member();
    void var();
    void key();
};

std::ostream & operator<<(std::ostream & out, Parser::TokenType kind);
