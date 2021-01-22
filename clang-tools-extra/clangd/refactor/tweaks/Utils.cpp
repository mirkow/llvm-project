#include "Utils.h"
#include "SourceCode.h"
#include "clang/Basic/SourceLocation.h"

#include <clang/AST/DeclCXX.h>
#include <cstddef>
#include <sstream>

bool clang::clangd::startsWith(const std::string &str,
                               const std::string &substr) {
  if (substr.size() > str.size()) {
    return false;
  }
  for (size_t i = 0; i < str.size() && i < substr.size(); i++) {
    if (str[i] != substr[i]) {
      return false;
    }
  }

  return true;
}

std::string
clang::clangd::makeCanonicalAbs(std::experimental::filesystem::path p) {
  // std::clog << "path before: " << p.string() << std::endl;
  if (p.is_relative()) {
    p = std::experimental::filesystem::current_path() / p;
    // std::clog << "cwd: " <<
    // std::experimental::filesystem::current_path().string()
    //           << " path before: " << p.string() << std::endl;
  }
  return std::experimental::filesystem::absolute(
             std::experimental::filesystem::canonical(p))
      .string();
}

bool clang::clangd::isSubpath(
    const std::experimental::filesystem::path &path,
    const std::experimental::filesystem::path &subpath) {
  auto canonicalPath = makeCanonicalAbs(path);
  auto canonicalSubpath = makeCanonicalAbs(subpath);
  // std::clog << "path: " << canonical_path << " subpath: " <<
  // canonical_subpath
  //           << std::endl;
  return startsWith(canonicalSubpath, canonicalPath);
}

clang::SourceLocation
clang::clangd::getEndPositionOfToken(clang::SourceLocation const &startOfToken,
                                     const clang::SourceManager &sm) {
  clang::LangOptions lopt;
  return clang::Lexer::getLocForEndOfToken(startOfToken, 0, sm, lopt);
}

std::string clang::clangd::getTypeString(const clang::QualType &type,
                                         bool cppStyle) {
  clang::LangOptions lopt;
  clang::PrintingPolicy policy(lopt);
  if (cppStyle) {
    policy.adjustForCPlusPlus();
  }

  return type.getAsString(policy);
}

std::string clang::clangd::getCharacterData(const clang::SourceManager &sm,
                                            const clang::SourceRange &range) {
  return {sm.getCharacterData(range.getBegin()),
          sm.getCharacterData(range.getEnd())};
}

std::string clang::clangd::getSymbolString(const clang::SourceManager &sm,
                                           const clang::SourceRange &range) {
  return {sm.getCharacterData(range.getBegin()),
          sm.getCharacterData(
              clang::clangd::getEndPositionOfToken(range.getEnd(), sm))};
}

std::string
clang::clangd::getFunctionDefinitionString(const clang::SourceManager &sm,
                                           const clang::FunctionDecl &fDecl,
                                           bool withBody) {
  const clang::FunctionType *fType = fDecl.getFunctionType();
  bool hasTrailingReturn = false;
  if (clang::FunctionType::classof(fType)) {
    const clang::FunctionProtoType *fTypeProto =
        fType->getAs<clang::FunctionProtoType>();
    hasTrailingReturn = fTypeProto->hasTrailingReturn();
  }
  bool isConst = fType ? fType->isConst() : false;

  // const clang::FunctionProtoType* fTypeProto =
  // dynamic_cast<clang::FunctionProtoType>(
  //     fType);
  // std::clog << "FunctionType found: " << (fType ? "true" : "false") <<
  // std::endl; std::clog << "has trailing return: " << (hasTrailingReturn ?
  // "true" : "false") << std::endl; std::clog << " return type: "
  //           << getTypeString(fDecl.getReturnType()) // getSymbolString(sm,
  //                                                   //
  //                                                   fDecl.getReturnTypeSourceRange())
  //           << std::endl;
  // std::clog << " param: " << getSymbolString(sm,
  // fDecl.getParametersSourceRange()) << std::endl; std::clog << " full decl
  // string: "
  //           << getSymbolString(sm, fDecl.DeclaratorDecl::getSourceRange()) <<
  //           std::endl;
  // std::clog << " body: " << getSymbolString(sm,
  // fDecl.getBody()->getSourceRange()) << std::endl; std::clog << " type spec:
  // "
  //           << getSymbolString(
  //                  sm, clang::SourceRange(fDecl.getTypeSpecStartLoc(),
  //                  fDecl.getTypeSpecEndLoc()))
  //           << std::endl;
  std::stringstream functionDefString;

  if (hasTrailingReturn) {
    functionDefString << "auto";
  } else {
    functionDefString << getTypeString(fDecl.getReturnType());
  }

  functionDefString << std::string(" ") << fDecl.getQualifiedNameAsString()
                    << "(";
  int i = 0;
  // std::cout << "def string: " << functionDefString.str() << std::endl;
  auto numParams = fDecl.getNumParams();
  // clang::FunctionTypeLoc loc = fDecl.getFunctionTypeLoc();

  if (numParams >
      0) // && !loc.isNull()  && fDecl.getFunctionTypeLoc().getNumParams() > 0)
  {
    auto params = fDecl.parameters(); // fDecl.getFunctionTypeLoc().getParams();
    for (const clang::ParmVarDecl *const param : params) {
      i++;
      functionDefString << getTypeString(param->getType()) << " "
                        << param->getName().str();
      if (i < params.size()) {
        functionDefString << ", ";
      }
      // std::clog << "Param: " << param->getName().str() << " type: "
      //           << getTypeString(param->getType())
      //           // << getSymbolString(sm, .)

      //           << " full type?: "
      //           << getSymbolString(
      //                  sm,
      //                  clang::SourceRange(
      //                      param->getTypeSpecStartLoc(),
      //                      param->getTypeSpecEndLoc()))
      //           // << getSymbolString(
      //           //        sm,
      //           param->getTypeSourceInfo()->getTypeLoc().getSourceRange())
      //           //   << " qualifier: "
      //           //   <<
      //           (param->getType().getQualifier().getQualifierLoc().hasQualifier()
      //           ||
      //           //               true ?
      //           //           getSymbolString(
      //           //               sm,
      //           //               param->getType()
      //           //                   .getQualifier()
      //           //                   .getQualifierLoc()
      //           //                   .getSourceRange()) :
      //           //           "<none>")
      //           << " default: "
      //           << (param->hasDefaultArg() ?
      //                   getSymbolString(sm, param->getDefaultArgRange()) :
      //                   "none")
      //           << std::endl;
    }
  }
  functionDefString << ")";
  if (isConst) {
    functionDefString << " const";
  }
  if (hasTrailingReturn) {
    functionDefString << " -> " + getTypeString(fDecl.getReturnType());
  }

  if (withBody) {
    auto body = fDecl.getBody();
    if (body) {
      functionDefString << "\n" + getSymbolString(sm, body->getSourceRange());
    } else {
      functionDefString << "{}";
    }
  }
  return functionDefString.str();
}

std::string clang::clangd::getFunctionSignatureString(
    const clang::SourceManager &sm, const clang::FunctionDecl &fDecl,
    bool withVariableNames, bool withFunctionName) {
  const clang::FunctionType *fType = fDecl.getFunctionType();
  bool hasTrailingReturn = false;
  if (clang::FunctionType::classof(fType)) {
    const clang::FunctionProtoType *fTypeProto =
        fType->getAs<clang::FunctionProtoType>();
    hasTrailingReturn = fTypeProto->hasTrailingReturn();
  }
  bool isConst = fType ? fType->isConst() : false;

  std::stringstream functionDefString;

  if (hasTrailingReturn) {
    functionDefString << "auto";
  } else {
    functionDefString << getTypeString(fDecl.getReturnType());
  }

  functionDefString << (withFunctionName ? std::string(" ") +
                                               fDecl.getQualifiedNameAsString()
                                         : "")
                    << "(";
  int i = 0;
  auto numParams = fDecl.getNumParams();

  if (numParams > 0) {
    auto params = fDecl.parameters();
    for (const clang::ParmVarDecl *const param : params) {
      i++;
      functionDefString << getTypeString(param->getType())
                        << (withVariableNames
                                ? std::string(" ") + param->getName().str()
                                : "");
      if (i < params.size()) {
        functionDefString << ", ";
      }
    }
  }
  functionDefString << ")";
  if (isConst) {
    functionDefString << " const";
  }
  if (hasTrailingReturn) {
    functionDefString << " -> " + getTypeString(fDecl.getReturnType());
  }

  return functionDefString.str();
}

std::string clang::clangd::getSymbolString(const clang::SourceManager &sm,
                                           const clang::SourceLocation &loc) {
  return {sm.getCharacterData(loc),
          sm.getCharacterData(clang::clangd::getEndPositionOfToken(loc, sm))};
}

std::vector<std::string> clang::clangd::getNamespaces(const Decl &D) {
  std::vector<std::string> Namespaces;
  for (const auto *Context = D.getDeclContext(); Context;
       Context = Context->getParent()) {
    if (llvm::isa<TranslationUnitDecl>(Context) ||
        llvm::isa<LinkageSpecDecl>(Context))
      break;

    if (const auto *ND = llvm::dyn_cast<NamespaceDecl>(Context))
      Namespaces.push_back(ND->getName().str());
  }
  std::reverse(Namespaces.begin(), Namespaces.end());
  return Namespaces;
}
clang::tooling::Replacement
clang::clangd::replaceDecl(const clang::SourceManager &SM, const Decl &Decl,
                           const std::string &ReplacementText) {
  SourceRange DeclRange(Decl.getSourceRange());
  SourceLocation DeclBegin(DeclRange.getBegin());
  SourceLocation DeclStartEnd(DeclRange.getEnd());
  SourceLocation DeclEndEnd(getEndPositionOfToken(DeclStartEnd, SM));
  auto Length = getSourceRangeLength(SM, DeclRange);
  // std::clog << "character after decl: " << SM.getCharacterData(DeclEndEnd)
  //           << std::endl;
  if (*SM.getCharacterData(DeclEndEnd) == ';') {
    Length += 1; // replace semicolon
  }
  return tooling::Replacement(SM, DeclBegin, Length, ReplacementText);
}

std::string
clang::clangd::getSourceLocationAsString(const clang::SourceManager &SM,
                                         const SourceLocation &Loc) {
  std::stringstream S;
  S << SM.getFilename(Loc).str() << ":" << SM.getSpellingLineNumber(Loc) << ":"
    << SM.getSpellingColumnNumber(Loc);
  return S.str();
}

std::string
clang::clangd::getSourceRangeAsString(const clang::SourceManager &SM,
                                      const SourceRange &Range) {
  std::stringstream S;
  auto Begin = Range.getBegin();
  auto End = Range.getEnd();

  S << SM.getFilename(Begin).str() << ":" << SM.getSpellingLineNumber(Begin)
    << ":" << SM.getSpellingColumnNumber(Begin) << "-"
    << SM.getSpellingLineNumber(End) << ":" << SM.getSpellingColumnNumber(End);
  return S.str();
}

size_t clang::clangd::getSourceRangeLength(const clang::SourceManager &SM,
                                           const SourceRange &Range) {
  SourceLocation DeclBegin(Range.getBegin());
  SourceLocation DeclStartEnd(Range.getEnd());
  SourceLocation DeclEndEnd(getEndPositionOfToken(DeclStartEnd, SM));
  size_t Length = SM.getFileOffset(DeclEndEnd) - SM.getFileOffset(DeclBegin);
  return Length;
}
llvm::Expected<llvm::StringRef>
clang::clangd::findFunctionDefinition(const StringRef &Code, int Cursor) {
  bool InMultiLineComment = false;
  bool InSingleLineComment = false;
  bool InDoubleQuoteString = false;
  bool InSingleQuoteString = false;
  bool FoundFunctionOpeningBrace = false;
  std::list<char> BraceStack;
  size_t FunctionDefEnd = 0;
  for (size_t I = Cursor; I < Code.size(); I++) {
    assert(!(InSingleQuoteString && InDoubleQuoteString));
    bool InString = InSingleQuoteString || InDoubleQuoteString;
    auto PrevChar = I > 0 ? Code.data()[I - 1] : '\0';
    auto Char = Code.data()[I];
    auto Char2 = Code.size() > I + 1 ? Code.data()[I + 1] : '\0';
    // auto Pos = offsetToPosition(Code, I);
    if (!InMultiLineComment && !InString && Char == '/' && Char2 == '*') {
      InMultiLineComment = true;
    } else if (InMultiLineComment && Char == '*' && Char2 == '/') {
      InMultiLineComment = false;
    } else if (!InSingleLineComment && !InMultiLineComment && !InString &&
               Char == '/' && Char2 == '/') {
      InSingleLineComment = true;
    } else if (InSingleLineComment && (Char == '\n')) {
      InSingleLineComment = false;
    } else if (!InDoubleQuoteString && !InSingleQuoteString &&
               PrevChar != '\\' && Char == '"') {
      InDoubleQuoteString = true;
    } else if (InDoubleQuoteString && !InSingleQuoteString &&
               PrevChar != '\\' && Char == '"') {
      InDoubleQuoteString = false;
    } else if (!InDoubleQuoteString && !InSingleQuoteString &&
               PrevChar != '\\' && Char == '\'') {
      InDoubleQuoteString = true;
    } else if (InSingleQuoteString && !InDoubleQuoteString &&
               PrevChar != '\\' && Char == '\'') {
      InDoubleQuoteString = false;
    } else if (!InSingleLineComment && !InMultiLineComment && !InString &&
               (Char == '(' || Char == '{' || Char == '[')) { // || Char == '<'
      if (Char == '{' && BraceStack.size() == 0) {
        FoundFunctionOpeningBrace = true;
      }
      BraceStack.push_back(Char);
      // std::clog << "Added to stack: " << Char << " at " << Pos.line << ":"
      //           << Pos.character << std::endl;
    } else if (!InSingleLineComment && !InMultiLineComment && !InString &&
               (Char == ')' || Char == '}' || Char == ']')) { //  || Char == '>'
      assert(!BraceStack.empty());
      if (Char == '}' && BraceStack.size() == 1 && BraceStack.front() == '{') {
        FunctionDefEnd = I + 1;
        // std::clog << "Encountered function end: " << Char
        //           << " Value in stack: " << BraceStack.back() << " at "
        //           << Pos.line << ":" << Pos.character << std::endl;
        break;
      }
      if ((Char == ')' && BraceStack.back() == '(') ||
          (Char == '}' && BraceStack.back() == '{') ||
          (Char == ']' && BraceStack.back() == '[')) {
        // std::clog << "Removed from stack: " << Char << " at " << Pos.line <<
        // ":"
        //           << Pos.character << std::endl;
        BraceStack.pop_back();
      } else {
        auto Pos = offsetToPosition(Code, I);
        // std::clog << "Encountered: " << Char
        //           << " Value in stack: " << BraceStack.back() << " at "
        //           << Pos.line << ":" << Pos.character << std::endl;

        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "Invalid braces encountered");
      }
    }
  }
  return Code.substr(Cursor, FunctionDefEnd - Cursor);
}
// int64_t clang::clangd::positionToOffset(const StringRef &Code,
//                                         const Position &Pos) {
//   size_t LineCounter = 0;
//   size_t CharacterCounter = 0;
//   for (size_t I = 0; I < Code.size(); I++) {
//     auto Char = Code.data()[I];
//     if (Pos.line == LineCounter && Pos.character == CharacterCounter) {
//       return I;
//     }
//     if (LineCounter > Pos.line ||
//         (Pos.line == LineCounter && CharacterCounter > Pos.character)) {
//       return -1;
//     }
//     if (Char == '\n') {
//       LineCounter++;
//       CharacterCounter = 0;
//     }
//   }
//   return -1;
// }
