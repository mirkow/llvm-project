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
#include "refactor/Tweak.h"

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
  std::string calcClassDeclEdits(Effect &Effect, const CXXRecordDecl &ClassDecl,
                                 const clang::SourceManager &SM);
  std::string calcMethodDefinitionEdits(Effect &Effect,
                                        const CXXRecordDecl &ClassDecl,
                                        ParsedAST &AST,
                                        const SymbolIndex &Index);

  std::string calcStaticVariableDefinitionEdits(Effect &Effect,
                                                const CXXRecordDecl &ClassDecl,
                                                ParsedAST &AST,
                                                const SymbolIndex &Index);

  std::string RecordName;
};

} // namespace clangd
} // namespace clang