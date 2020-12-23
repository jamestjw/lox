#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

// We do not have a pointer to the source string, but merely to the 
// start and current position of the lexeme we are processing now
typedef struct {
  // Tracks the start of the current lexeme we are working on 
  const char* start;
  // Tracks which character we are at in the current lexeme 
  const char* current;
  // Tracks the current line number of the lexeme we are 
  // working on for error reporting
  int line;
} Scanner;

// We create another global variable for the scanner to avoid having to pass an instance around everywhere
Scanner scanner;

// We initiate the start and current char pointer to the
// beginning of the source string and set current line to 1
void initScanner(const char* source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         c == '_';
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAtEnd() {
  return *scanner.current == '\0';
}

static char advance() {
  scanner.current++;
  return scanner.current[-1];
}

static char peek() {
  return *scanner.current;
}

static char peekNext() {
  if (isAtEnd()) return '\0';
  return scanner.current[1];
}

static bool match(char expected) {
  if (isAtEnd()) return false;
  if (*scanner.current != expected) return false;

  scanner.current++;
  return true;
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;

  return token;
}

// Care must be taken to pass in a string that has a
// lifetime long enough for the compiler to read it,
// and for now we only call it with C string literals
static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

// This function will advance as many times as necessary
// to skip all whitespaces, and it stops it encounters a
// character that is not a whitespace
// When it encounters a newline it will increment the current line number.
// This will also handle comments (although comments are technically not 
// whitespace)
static void skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      // Handle newlines
      case '\n':
        scanner.line++;
        advance();
        break;
      // Handle comments
      case '/':
        if (peekNext() == '/') {
          // A comment will go on until the end of the line
          // When we detect a newline, we do not consume it here
          // since we want it to be handled by skipWhitespace() in
          // the outer loop.
          while (peek() != '\n' && !isAtEnd()) advance();
        } else {
          // Syntax for comments will have two slashes in a row
          // We do not consume the first '/' if the second one
          // is absent.
          return;
        }
      default:
        return;
    }
  }
}

// Start is the offset from scanner.current from where rest starts
// Length is the strlen of rest
// checkKeyword will check if given a start and length, does rest match the string defined
// by the above parameters, given that it does then type will be returned, else TOKEN_IDENTIFIER
static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
  if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0) {
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
  switch (scanner.start[0]) {
    case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
    case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
      // First check if there is indeed more than one char
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
          case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
          case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
        }
      }
      break;
    case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
      // Check for existence of more than one char
      if (scanner.current - scanner.start > 1) {
        switch (scanner.start[1]) {
          case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
          case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
        }
      }
      break;
    case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
    case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE); 
  }

  return TOKEN_IDENTIFIER;
}

static Token identifier() {
  while (isAlpha(peek()) || isDigit(peek())) advance();

  return makeToken(identifierType());
}

static Token number() {
  while(isDigit(peek())) advance();

  // Check if there are digits after decimal
  if (peek() == '.' && isDigit(peekNext())) {
    // Consume the .
    advance();

    // Consume the remaining digits
    while (isDigit(peek())) advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') scanner.line++;
    advance();
  }

  if (isAtEnd()) return errorToken("Unterminated string.");

  // Consume the closing quote
  advance();
  return makeToken(TOKEN_STRING);
}

Token scanToken() {
  // We want to skip whitespace between tokens
  skipWhitespace();

  scanner.start = scanner.current;

  if (isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c)) return identifier();

  // Instead of adding a switch case for digits 0 - 9, we do this
  if (isDigit(c)) return number();

  switch (c) {
    // One char lexemes
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case ',': return makeToken(TOKEN_COMMA);
    case '.': return makeToken(TOKEN_DOT);
    case '-': return makeToken(TOKEN_MINUS);
    case '+': return makeToken(TOKEN_PLUS);
    case '/': return makeToken(TOKEN_SLASH);
    case '*': return makeToken(TOKEN_STAR);
    // Two char lexemes        
    case '!':
      return makeToken(
          match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(
          match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(
          match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(
          match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"': return string();
  }

  return errorToken("Unexpected character");
}
