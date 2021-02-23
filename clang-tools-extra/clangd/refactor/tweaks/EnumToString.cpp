/**
 * @file InsertVirtualFunctionsBase.cpp
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#include "EnumToString.h"
#include "SourceCode.h"
#include "Utils.h"
#include "clang/AST/APValue.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"

#include <AST.h>
#include <chrono>
#include <sstream>

namespace clang {
namespace clangd {
REGISTER_TWEAK(EnumToString)

std::string EnumToString::title() const { return "Add EnumToString function."; }

bool EnumToString::prepare(const Selection &Inputs) {

  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    std::clog << "node is nullptr" << std::endl;
    return false;
  }
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const EnumDecl *Enum = AstNode.get<EnumDecl>();

  if (Enum) {
    return true;
  }

  return false;
}
Expected<Tweak::Effect> EnumToString::apply(const Selection &Inputs) {
  std::clog << "Trying to apply EnumToString" << std::endl;
  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Symbol under cursor is NULL");
  }
  SourceManager &SM = Inputs.AST->getSourceManager();
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const EnumDecl *Enum = AstNode.get<EnumDecl>();

  if (Enum) {

    unsigned int Indentation =
        SM.getSpellingColumnNumber(Enum->getSourceRange().getBegin());
    std::string Code = getSymbolString(SM, Enum->getSourceRange());
    const std::string ScopedEnumPrefix =
        Enum->isScoped() ? (Enum->getNameAsString() + "::") : "";
    std::string Indent = std::string(Indentation, ' ');

    std::string InsertionText = "\n\n" + Indent + "inline const char* " +
                                Enum->getNameAsString() + "ToString(const " +
                                Enum->getNameAsString() + " value)\n" + Indent +
                                "{\n";

    InsertionText += Indent + "\tconst char* result;\n" + Indent +
                     "\tswitch (value)\n" + Indent + "\t{\n";

    for (EnumConstantDecl *E : Enum->enumerators()) {
      InsertionText +=
          Indent + "\tcase " + ScopedEnumPrefix + E->getNameAsString() + ":\n";
      InsertionText +=
          Indent + "\t\tresult = \"" + E->getNameAsString() + "\";\n";
      InsertionText += Indent + "\t\tbreak;\n";
    }
    InsertionText += Indent + "\tdefault:\n";
    InsertionText += Indent + "\t\tresult = \"<Undefined>\";\n";
    InsertionText += Indent + "\t\tbreak;\n";
    InsertionText += Indent + "\t}\n";
    InsertionText += Indent + "\treturn result;\n" + Indent + "}";
    tooling::Replacements R;

    if (auto Err = R.add(tooling::Replacement(
            SM,
            getEndPositionOfToken(Enum->getSourceRange().getEnd(), SM)
                .getLocWithOffset(1),
            0, InsertionText))) {
    }

    return Effect::mainFileEdit(Inputs.AST->getASTContext().getSourceManager(),
                                std::move(R));
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "Not a enum declaration");
}

} // namespace clangd
} // namespace clang