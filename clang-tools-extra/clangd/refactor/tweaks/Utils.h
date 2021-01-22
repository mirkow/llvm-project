#pragma once

#include "Protocol.h"
#include "clang/AST/ASTFwd.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/ADT/StringRef.h"
#include <clang/AST/Decl.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <experimental/filesystem>
#include <iostream>
#include <string>

namespace clang {
namespace clangd {

bool startsWith(const std::string &str, const std::string &substr);

std::string makeCanonicalAbs(std::experimental::filesystem::path p);

bool isSubpath(const std::experimental::filesystem::path &path,
               const std::experimental::filesystem::path &subpath);

clang::SourceLocation
getEndPositionOfToken(clang::SourceLocation const &startOfToken,
                      const clang::SourceManager &sm);

std::string getCharacterData(const clang::SourceManager &sm,
                             const clang::SourceRange &range);

std::string getSymbolString(const clang::SourceManager &sm,
                            const clang::SourceRange &range);

std::string getSymbolString(const clang::SourceManager &sm,
                            const clang::SourceLocation &loc);

std::string getTypeString(const clang::QualType &type, bool cppStyle = true);

std::string getFunctionDefinitionString(const clang::SourceManager &sm,
                                        const clang::FunctionDecl &fDecl,
                                        bool withBody = true);

std::string getFunctionSignatureString(const clang::SourceManager &sm,
                                       const clang::FunctionDecl &fDecl,
                                       bool withVariableNames = true,
                                       bool withFunctionName = true);

std::vector<std::string> getNamespaces(const Decl &D);

tooling::Replacement replaceDecl(const clang::SourceManager &SM,
                                 const Decl &Decl,
                                 const std::string &ReplacementText = "");

size_t getSourceRangeLength(const clang::SourceManager &SM,
                            const SourceRange &Range);

std::string getSourceLocationAsString(const clang::SourceManager &SM,
                                      const SourceLocation &Loc);
std::string getSourceRangeAsString(const clang::SourceManager &SM,
                                   const SourceRange &Range);

// int64_t positionToOffset(const StringRef &Code, const Position &Pos);

llvm::Expected<llvm::StringRef> findFunctionDefinition(const StringRef &Code,
                                                       int Cursor);

} // namespace clangd
} // namespace clang