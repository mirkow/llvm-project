/**
 * @file InsertVirtualFunctionsBase.cpp
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#include "InsertVirtualFunctionsBase.h"
#include "Utils.h"

#include "XRefs.h"
#include "support/Logger.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/AttrKinds.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"

#include <AST.h>

#include <climits>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace clang {
namespace clangd {

bool InsertVirtualFunctionsTweakBase::prepare(const Selection &Inputs) {

  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    return false;
  }
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const CXXRecordDecl *ClassDecl = AstNode.get<CXXRecordDecl>();
  if (ClassDecl && ClassDecl->isCompleteDefinition()) {
    bool Result =
        !ClassDecl->bases()
             .empty(); // the class has some bases -> we offer the tweak
    if (Result) {
      RecordName = ClassDecl->getNameAsString();
    }
    return Result;
  }

  return false;
}

Expected<Tweak::Effect>
InsertVirtualFunctionsTweakBase::apply(const Selection &Inputs) {
  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Symbol under cursor is NULL");
  }
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const CXXRecordDecl *ClassDecl = AstNode.get<CXXRecordDecl>();
  if (!ClassDecl) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Symbol under cursor is not a class");
  }
  auto &SM = Inputs.AST->getSourceManager();
  std::vector<Edit> Edits;
  SourceLocation InsertLocation = findInsertLocation(SM, *ClassDecl);
  auto VirtualFunctions = getAllVirtualFunctions(SM, *ClassDecl);
  auto IndentationStr = std::string(findIndendation(SM, *ClassDecl), ' ');
  std::string InsertionText = "\n";
  for (auto &FuncPair : VirtualFunctions) {
    auto &Func = FuncPair.second;
    if (!Func.FuncDecl) {
      continue;
    }
    if (Mode == ModeType::ePureVirtualFunctions &&
        Func.ImplementationState != DefinitionState::pureVirtual) {
      continue;
    }
    auto FullDeclString =
        getSymbolString(SM, Func.FuncDecl->DeclaratorDecl::getSourceRange());
    std::clog << "func: " << Func.FuncDecl->getQualifiedNameAsString()
              << " sig: " << Func.FuncSig << " decl: " << FullDeclString
              << std::endl;
    if (Func.ImplementationState == DefinitionState::implemented) {
      continue;
    }
    std::string CommentStr = "";
    if (Func.ImplementationState == DefinitionState::preImplemented) {
      CommentStr = "// "; // Function is already implemented in subclass ->
                          // add the function but commented
    }
    InsertionText +=
        IndentationStr + CommentStr + FullDeclString + " override {}\n";
  }

  tooling::Replacements R;
  if (auto Err =
          R.add(tooling::Replacement(SM, InsertLocation, 0, InsertionText))) {
    std::clog << "Couldnt add replacement " << std::endl;
    return std::move(Err);
  }

  return Effect::mainFileEdit(Inputs.AST->getASTContext().getSourceManager(),
                              std::move(R));
}

int InsertVirtualFunctionsTweakBase::findIndendation(
    const SourceManager &SM, const CXXRecordDecl &CxxRecordDecl,
    int FallbackIndentation) {
  int Column = -1;

  // find indentation of first member function
  for (CXXMethodDecl *Method : CxxRecordDecl.methods()) {
    if (Method) {
      Column = SM.getSpellingColumnNumber(Method->getSourceRange().getBegin());
    }
  }
  // if no method found, try to find a member variable
  if (Column < 0) {
    for (const FieldDecl *Field : CxxRecordDecl.fields()) {
      if (Field) {
        Column = SM.getSpellingColumnNumber(Field->getSourceRange().getBegin());
      }
    }
  }

  // at last, just use class column + fallback indentation
  if (Column < 0) {
    Column =
        SM.getSpellingColumnNumber(CxxRecordDecl.getSourceRange().getBegin()) +
        FallbackIndentation;
  }
  return Column;
}
SourceLocation InsertVirtualFunctionsTweakBase::findInsertLocation(
    const SourceManager &SM, const CXXRecordDecl &CxxRecordDecl) {
  return CxxRecordDecl.getSourceRange().getEnd().getLocWithOffset(-1);
}

InsertVirtualFunctionsTweakBase::VirtualFunctionsMap
InsertVirtualFunctionsTweakBase::getAllVirtualFunctions(
    const SourceManager &SM, const CXXRecordDecl &Record) {
  using namespace clang;
  VirtualFunctionsMap VirtualFunctions;
  getAllVirtualFunctionsRecursive(SM, Record, VirtualFunctions);

  for (const CXXMethodDecl *Function : Record.methods()) {
    if (Function) {
      auto CurrentFunction =
          getFunctionSignatureString(SM, *Function, false, false);
      for (auto &FuncPair : VirtualFunctions) {
        auto &Func = FuncPair.second;
        if (Func.FuncSig == CurrentFunction &&
            Func.FuncDecl->getNameAsString() == Function->getNameAsString()) {
          Func.ImplementationState = DefinitionState::implemented;
        }
      }
    }
  }
  return VirtualFunctions;
}
void InsertVirtualFunctionsTweakBase::getAllVirtualFunctionsRecursive(
    const SourceManager &SM, const CXXRecordDecl &Record,
    InsertVirtualFunctionsTweakBase::VirtualFunctionsMap &Functions) {
  using namespace clang;

  Record.forallBases([&](const CXXRecordDecl *BaseDefinition) -> bool {
    if (BaseDefinition) {
      for (const CXXMethodDecl *Function : BaseDefinition->methods()) {

        if (Function && Function->isVirtual()) {
          auto CurrentFunctionSig =
              getFunctionSignatureString(SM, *Function, false, false);
          auto FuncName = Function->getNameAsString();
          auto Id = std::make_pair(FuncName, CurrentFunctionSig);
          auto It = Functions.find(Id);
          if (It == Functions.end() ||
              It->second.ImplementationState == DefinitionState::pureVirtual) {
            Functions[Id] = VirtualFunctionData{
                Function, CurrentFunctionSig,
                Function->isDefined() ? DefinitionState::preImplemented
                                      : DefinitionState::pureVirtual};
          }
        }
      }
      getAllVirtualFunctionsRecursive(SM, *BaseDefinition, Functions);
    }
    return false;
  });
}

} // namespace clangd
} // namespace clang