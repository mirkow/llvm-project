/**
 * @file InsertVirtualFunctionsBase.h
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#pragma once
#include "ParsedAST.h"
#include "refactor/Tweak.h"
#include "clang/Basic/TokenKinds.h"

namespace clang {
namespace clangd {

class MoveClassToOwnFile : public Tweak {
public:
  MoveClassToOwnFile() {
    // std::clog << "MoveClassToOwnFile created" << std::endl;
  }
  const char *id() const override;
  std::string title() const override {
    return "Move class '" + RecordName + "' to new header/source file.";
  }
  bool prepare(const Selection &Inputs) override;

  Expected<Effect> apply(const Selection &Inputs) override;
  Intent intent() const override { return Refactor; }

private:
  std::string extractClassDeclEdits(Effect &Effect,
                                    const CXXRecordDecl &ClassDecl,
                                    const ParsedAST &AST);
  std::string extractSourceEdits(Effect &Effect, const CXXRecordDecl &ClassDecl,
                                 ParsedAST &AST, const SymbolIndex &Index);

  Expected<std::vector<std::string>> calcStaticVariableDefinitionStrings(
      const CXXRecordDecl &ClassDecl, ParsedAST &AST, const SymbolIndex &Index,
      std::map<std::string, std::string> &FileContentMap);

  Expected<std::vector<std::string>> extractMethodDefinitionStrings(
      Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
      const SymbolIndex &Index,
      std::map<std::string, std::string> &FileContentMap,
      std::vector<std::string> &IncludeStrings);

  Expected<std::vector<std::string>> extractStaticVariableDefinitionEdits(
      Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
      const SymbolIndex &Index,
      std::map<std::string, std::string> &FileContentMap);

  bool hasSubRecordWithFunctions(const CXXRecordDecl &ClassDecl,
                                 const clang::SourceManager &SM);

  llvm::Error addEdit(FileEdits &EditMap, StringRef Filepath, StringRef Code,
                      const tooling::Replacement &Repl) const;

  static std::vector<std::string> extractIncludeStrings(const ParsedAST &AST);

  std::string RecordName;
};

} // namespace clangd
} // namespace clang