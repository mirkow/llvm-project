/**
 * @file InsertVirtualFunctionsBase.cpp
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#include "InsertMissingCases.h"
#include "SourceCode.h"
#include "Utils.h"
#include "clang/AST/APValue.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringExtras.h"

// #include "XRefs.h"
// #include "support/Logger.h"
// #include "clang/AST/Stmt.h"
// #include "clang/AST/Type.h"
// #include "clang/AST/TypeLoc.h"
// #include "clang/Basic/AttrKinds.h"
// #include "clang/Basic/LLVM.h"
// #include "clang/Basic/SourceLocation.h"
// #include "clang/Basic/TokenKinds.h"
// #include "llvm/ADT/None.h"
// #include "llvm/ADT/Optional.h"
// #include "llvm/ADT/StringExtras.h"
// #include "llvm/Support/Debug.h"
// #include "llvm/Support/Error.h"

#include <AST.h>
#include <chrono>
#include <sstream>

namespace clang {
namespace clangd {
REGISTER_TWEAK(InsertMissingCases)

std::string InsertMissingCases::title() const {

  std::stringstream SStr;
  SStr << "Add " << MissingEnumValueStrings.size() << " missing cases.";
  return SStr.str();
}

bool InsertMissingCases::prepare(const Selection &Inputs) {
  // auto Start = std::chrono::steady_clock::now();
  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    std::clog << "node is nullptr" << std::endl;
    return false;
  }
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const SwitchStmt *Switch = AstNode.get<SwitchStmt>();

  if (Switch && Switch->getCond() && Switch->getCond()->getExprStmt()) {
    const Expr *Cond = Switch->getCond();

    std::vector<const EnumConstantDecl *> AllEnumValues, FoundEnumValues,
        MissingEnumValues;
    if (!Cond->getExprStmt()->children().empty()) {
      const Stmt *Child = *Cond->getExprStmt()->child_begin();

      if (const ImplicitCastExpr *Cast =
              dyn_cast_or_null<ImplicitCastExpr>(Child)) {
        QualType Type = Cast->getType();

        EnumDecl *CurEnumDecl =
            dyn_cast_or_null<EnumDecl>(Type.getTypePtr()->getAsTagDecl());

        RequiredNamespaces = getRelativeNamespaces(Inputs.AST->getASTContext(),
                                                   *Switch, *CurEnumDecl);
        if (CurEnumDecl) {
          for (EnumConstantDecl *E : CurEnumDecl->enumerators()) {
            AllEnumValues.push_back(E);
            OrderedEnumValues.push_back(
                std::make_pair(E->getNameAsString(), Optional<SourceRange>()));
          }
        }
      }
    }

    // Find Case and Enum cast Decl. Not a recursion since the depth should
    // always be the same?
    if (Switch->getBody()) {
      for (const Stmt *C : Switch->getBody()->children()) {
        if (C->getStmtClass() == Stmt::CaseStmtClass) {
          for (const Stmt *C2 : C->children()) {

            for (const Stmt *C3 : C2->children()) {

              for (const Stmt *C4 : C3->children()) {

                if (const DeclRefExpr *CurExpr =
                        dyn_cast_or_null<DeclRefExpr>(C4)) {
                  const ValueDecl *CurEnumDecl = CurExpr->getDecl();

                  if (const EnumConstantDecl *CurEnumValue =
                          dyn_cast_or_null<EnumConstantDecl>(CurEnumDecl)) {
                    FoundEnumValues.push_back(CurEnumValue);
                  }
                }
              }
            }
          }
        }
      }
    }
    for (auto &Candidate : AllEnumValues) {
      bool Found = false;
      for (auto &Enum : FoundEnumValues) {
        if (Enum->getNameAsString() == Candidate->getNameAsString()) {
          Found = true;
          break;
        }
      }
      if (!Found) {
        MissingEnumValues.push_back(Candidate);
      }
    }

    if (!MissingEnumValues.empty()) {
      for (const EnumConstantDecl *Enum : MissingEnumValues) {
        MissingEnumValueStrings.push_back(Enum->getNameAsString());
      }
      return true;
    }
  }

  return false;
}

Expected<Tweak::Effect> InsertMissingCases::apply(const Selection &Inputs) {
  std::clog << "Trying to apply InsertMissingCases" << std::endl;
  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Symbol under cursor is NULL");
  }
  SourceManager &SM = Inputs.AST->getSourceManager();
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const SwitchStmt *Switch = AstNode.get<SwitchStmt>();
  std::map<std::string, SourceRange> ExistingCases;
  if (Switch) {

    if (Switch->getBody()) {
      for (const Stmt *C : Switch->getBody()->children()) {
        if (C->getStmtClass() == Stmt::CaseStmtClass) {
          std::string EnumValue;
          for (const Stmt *C2 : C->children()) {

            for (const Stmt *C3 : C2->children()) {

              for (const Stmt *C4 : C3->children()) {

                if (const DeclRefExpr *CurExpr =
                        dyn_cast_or_null<DeclRefExpr>(C4)) {
                  const ValueDecl *CurEnumDecl = CurExpr->getDecl();

                  if (const EnumConstantDecl *CurEnumValue =
                          dyn_cast_or_null<EnumConstantDecl>(CurEnumDecl)) {
                    EnumValue = CurEnumValue->getNameAsString();
                  }
                }
              }
            }
          }
          if (!EnumValue.empty()) {
            for (auto &Pair : OrderedEnumValues)
              if (Pair.first == EnumValue)
                Pair.second = C->getSourceRange();
          }
        }
      }
    }

    unsigned int Indentation =
        SM.getSpellingColumnNumber(Switch->getSourceRange().getBegin());
    std::string IndentationStr = std::string(Indentation, ' ');

    auto NSString =
        llvm::join(RequiredNamespaces.begin(), RequiredNamespaces.end(), "::");
    if (!NSString.empty()) {
      NSString += "::";
    }
    std::string InsertionText = "\n";

    tooling::Replacements R;
    for (const auto &Pair : OrderedEnumValues) {

      if (Pair.second) {
        InsertionText += getSymbolString(SM, *Pair.second) + ";\n";

        if (auto Err = R.add(getReplacement(SM, *Pair.second, ""))) {
          return std::move(Err);
        }
      } else {
        InsertionText +=
            IndentationStr + "case " + NSString + Pair.first + ":\n";
        InsertionText += IndentationStr + "\tbreak;\n";
      }
    }

    if (auto Err = R.add(tooling::Replacement(
            SM,
            Switch->getBody()->getSourceRange().getBegin().getLocWithOffset(1),
            0, InsertionText))) {
    }

    return Effect::mainFileEdit(Inputs.AST->getASTContext().getSourceManager(),
                                std::move(R));
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(), "NYI");
}

} // namespace clangd
} // namespace clang