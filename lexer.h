#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include "types.h"
#include "constants.h"
#include <string>
#include <fstream>
#include <exception>

class Lexer
{
public:
    Lexer(const std::string& sourceFile, Types& ty, Constants& co);
    Token GetToken();

private:
    int NextChar();
    int CurChar();
    int PeekChar();

    Location Where() const { return Location(fName, lineNo, column); }

private:
    std::string   fName;
    int           lineNo;
    int           column;
    std::ifstream inFile;
    int           curChar;
    int           nextChar;
    int           curValid;
    Types&        types;
    Constants&    constants;;
};

class LexerException : public std::exception
{
public:
    LexerException(std::string r)
    {
	reason = r;
    }
    const char* what() const  noexcept { return (std::string("Lexer Exception") + reason).c_str(); }

private:
    std::string reason;
};

bool IsKeyWord(const std::string& name);

#endif
