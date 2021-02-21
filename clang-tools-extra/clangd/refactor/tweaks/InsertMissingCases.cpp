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
  // SourceManager &SM = Inputs.AST->getSourceManager();
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const SwitchStmt *Switch = AstNode.get<SwitchStmt>();
  // ASTNodeKind Kind = AstNode.getNodeKind();
  // std::clog << "Kind: " << Kind.asStringRef().str() << " -  "
  //           << AstNode.getSourceRange().printToString(SM) << std::endl;
  if (Switch && Switch->getCond() && Switch->getCond()->getExprStmt()) {
    const Expr *Cond = Switch->getCond();

    // std::clog << "Cond: " << getSymbolString(SM, Cond->getSourceRange())
    //           << std::endl;
    // Cond->getExprStmt()->dump();
    // std::clog << "ExprStmt string: "
    //           << Cond->getExprStmt()->getType().getAsString() << std::endl;
    // QualType Type = Cond->getExprStmt()->getType();
    std::vector<const EnumConstantDecl *> AllEnumValues, FoundEnumValues,
        MissingEnumValues;
    if (!Cond->getExprStmt()->children().empty()) {
      const Stmt *Child = *Cond->getExprStmt()->child_begin();
      // DeclStmt *const *Decl = cast<DeclStmt *>(Child);
      // Child->child_begin()->getStmtClass()
      // DeclRefExpr v;

      if (const ImplicitCastExpr *Cast =
              dyn_cast_or_null<ImplicitCastExpr>(Child)) {
        QualType Type = Cast->getType();
        // std::clog << "Cast Type: " << Cast->getType().getAsString()
        //           << " enum: " << Type.getTypePtr()->isEnumeralType()
        //           << "EnumDecl: "
        //           << EnumDecl::classof(Type.getTypePtr()->getAsTagDecl())
        //           << std::endl;
        // TagDecl *EnumVar = Type.getTypePtr()->getAsTagDecl();
        EnumDecl *CurEnumDecl =
            dyn_cast_or_null<EnumDecl>(Type.getTypePtr()->getAsTagDecl());
        if (CurEnumDecl) {
          for (EnumConstantDecl *E : CurEnumDecl->enumerators()) {
            // std::clog << "\t enum: " << e->getNameAsString() << std::endl;
            AllEnumValues.push_back(E);
          }
        }
      }
    }
    std::clog << "body:" << std::endl;
    // Switch->getBody()->dump();
    if (Switch->getBody()) {
      for (const Stmt *C : Switch->getBody()->children()) {
        // C->dump();
        // std::cerr << "C->getStmtClass(): " << C->getStmtClassName()
        //           << std::endl;
        if (C->getStmtClass() == Stmt::CaseStmtClass) {
          for (const Stmt *C2 : C->children()) {
            // C2->dump();
            // std::cerr << "C2->getStmtClass(): " << C2->getStmtClassName()
            //           << std::endl;
            for (const Stmt *C3 : C2->children()) {
              // std::cerr << "C3->getStmtClass(): " << C3->getStmtClassName()
              //           << std::endl;
              for (const Stmt *C4 : C3->children()) {
                // std::cerr << "C4->getStmtClass(): " << C4->getStmtClassName()
                //           << " DeclRefExpr: " << DeclRefExpr::classof(C4)
                //           << std::endl;
                if (const DeclRefExpr *expr =
                        dyn_cast_or_null<DeclRefExpr>(C4)) {
                  const ValueDecl *CurEnumDecl = expr->getDecl();
                  // std::cerr << "EnumConstantDecl: "
                  //           << EnumConstantDecl::classof(CurEnumDecl) << " "
                  //           << CurEnumDecl->getNameAsString() << std::endl;
                  if (const EnumConstantDecl *CurEnumValue =
                          dyn_cast_or_null<EnumConstantDecl>(CurEnumDecl)) {
                    FoundEnumValues.push_back(CurEnumValue);
                    // std::cerr
                    //     << "CurEnumValue: " <<
                    //     CurEnumValue->getNameAsString()
                    //     << std::endl;
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
    // auto End = std::chrono::steady_clock::now();
    // std::cerr << "Prepare took "
    //           << std::chrono::duration_cast<std::chrono::microseconds>(End -
    //                                                                    Start)
    //                  .count()
    //           << " us" << std::endl;
    if (!MissingEnumValues.empty()) {
      // std::cerr << "Missing enum values: ";
      for (const EnumConstantDecl *Enum : MissingEnumValues) {
        // std::cerr << Enum->getNameAsString() << " ";
        MissingEnumValueStrings.push_back(Enum->getQualifiedNameAsString());
      }
      // std::cerr << std::endl;
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

  if (Switch) {
    unsigned int Indentation =
        SM.getSpellingColumnNumber(Switch->getSourceRange().getBegin());
    std::string IndentationStr = std::string(Indentation, ' ');
    std::clog << "Switch Body: "
              << getSymbolString(SM, Switch->getBody()->getSourceRange())
              << std::endl;
    std::string InsertionText = "\n";
    for (auto &EnumValueStr : MissingEnumValueStrings) {
      InsertionText +=
          IndentationStr + "case " + EnumValueStr + ":\n\tbreak;\n";
    }
    std::clog << "Insertion:\n" << InsertionText << std::endl;
    tooling::Replacements R;
    if (auto Err = R.add(tooling::Replacement(
            SM,
            Switch->getBody()->getSourceRange().getBegin().getLocWithOffset(1),
            0, InsertionText))) {
      std::clog << "Couldnt add replacement " << std::endl;
      return std::move(Err);
    }

    return Effect::mainFileEdit(Inputs.AST->getASTContext().getSourceManager(),
                                std::move(R));
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(), "NYI");
}

} // namespace clangd
} // namespace clang