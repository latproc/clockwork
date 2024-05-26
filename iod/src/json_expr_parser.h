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
    struct InputStream {
        virtual char get() = 0;
        virtual bool eof() = 0;
        virtual void mark() = 0;
        virtual void reset() = 0;
    };

    struct StringInputStream : public Parser::InputStream {
        std::stringstream is;
        size_t pos = 0;
        StringInputStream(const std::string &str_) : is(str_) {}
        char get() override { return is.get(); }
        bool eof() override { return is.eof(); }
        void mark() override { pos = is.tellg(); }
        void reset() override { is.seekg(pos); }
    };

    struct CStringInputStream : public Parser::InputStream {
        const char *pos;
        char last;
        const char *mark_pos = 0;
        CStringInputStream(const char *pos_) : pos(pos_), last(0) {}
        char get() override { last = *pos == '\0' ? 0xff : *pos++; return last; }
        bool eof() override { return last == (char)0xff; }
        void mark() override { mark_pos = pos; }
        void reset() override { pos = mark_pos; }
    };

    enum class TokenType { expr, root, introducer, member, var, subs_begin, key, subs_end, index, wildcard};

    Parser(InputStream &is_,
                    std::function<void(char, TokenType kind)> cb_,
                    std::function<void(std::string, TokenType kind)> cb2_);

    void run();

    static std::ostream & display(std::ostream & out, TokenType kind);

  private:

    char next;
    std::string value;
    InputStream &is;
    std::function<void(char, TokenType)> cb;
    std::function<void(std::string, TokenType)> cb2;

    char pull();
    void scan();
    void root();
    void member();
    void var();
    void key();
};

std::ostream & operator<<(std::ostream & out, Parser::TokenType kind);
