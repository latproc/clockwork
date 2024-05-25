#pragma once
#include <ostream>
#include <sstream>
#include <string>
#include <functional>
#include <boost/optional.hpp>

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

    Parser(const char *&input, 
                    std::function<void(char, TokenType kind)> cb_,
                    std::function<void(std::string, TokenType kind)> cb2_);

    Parser(std::istream &is_, 
                    std::function<void(char, TokenType kind)> cb_,
                    std::function<void(std::string, TokenType kind)> cb2_);

    void run();

    static std::ostream & display(std::ostream & out, TokenType kind);

  private:

    char next;
    std::string value;
    boost::optional<std::istream &>is;
    boost::optional<const char *&>input;
    std::function<void(char, TokenType)> cb;
    std::function<void(std::string, TokenType)> cb2;

    char pull();
    bool is_eof();
    void scan();
    void root();
    void member();
    void var();
    void key();
};

std::ostream & operator<<(std::ostream & out, Parser::TokenType kind);
