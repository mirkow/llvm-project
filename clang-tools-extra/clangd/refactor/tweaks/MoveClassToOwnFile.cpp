/**
 * @file InsertVirtualFunctionsBase.cpp
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#include "MoveClassToOwnFile.h"
#include "SourceCode.h"
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
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <streambuf>
#include <string>

namespace clang {
namespace clangd {
REGISTER_TWEAK(MoveClassToOwnFile)

bool MoveClassToOwnFile::prepare(const Selection &Inputs) {

  auto *SelNode = Inputs.ASTSelection.commonAncestor();
  if (SelNode == nullptr) {
    std::clog << "node is nullptr" << std::endl;
    return false;
  }
  const ast_type_traits::DynTypedNode &AstNode = SelNode->ASTNode;
  const CXXRecordDecl *ClassDecl = AstNode.get<CXXRecordDecl>();
  if (ClassDecl && ClassDecl->isCompleteDefinition()) {
    auto &SM = Inputs.AST->getSourceManager();
    auto Filepath =
        getCanonicalPath(SM.getFileEntryForID(SM.getFileID(
                             ClassDecl->getSourceRange().getBegin())),
                         SM);
    if (!Filepath) {
      std::clog << "Couldnt get canonical path for "
                << SM.getFilename(ClassDecl->getSourceRange().getBegin()).str()
                << std::endl;
      return false;
    }
    bool Result = std::experimental::filesystem::path(*Filepath).stem() !=
                  ClassDecl->getNameAsString();
    if (Result) {
      RecordName = ClassDecl->getNameAsString();
      std::clog << "Proposing fix " << id() << "?: " << Result << std::endl;
    }
    return Result;
  }
  std::clog << "node is not a class: "
            << AstNode.getNodeKind().asStringRef().str() << std::endl;

  return false;
}

Expected<Tweak::Effect> MoveClassToOwnFile::apply(const Selection &Inputs) {
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

  Position Pos = sourceLocToPosition(SM, Inputs.Cursor);
  locateSymbolAt(*Inputs.AST, Pos, Inputs.Index);
  Tweak::Effect Effect;
  {
    auto ErrorStr = calcClassDeclEdits(Effect, *ClassDecl, SM);
    if (!ErrorStr.empty()) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
    }
  }

  {
    auto ErrorStr = calcMethodDefinitionEdits(Effect, *ClassDecl, *Inputs.AST,
                                              *Inputs.Index);
    if (!ErrorStr.empty()) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
    }
  }

  {
    auto ErrorStr = calcStaticVariableDefinitionEdits(
        Effect, *ClassDecl, *Inputs.AST, *Inputs.Index);
    if (!ErrorStr.empty()) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
    }
  }
  // TODO: Move also static variable definitions
  return llvm::createStringError(llvm::inconvertibleErrorCode(), "Dummy mode");
  return std::move(Effect);
  // auto HeaderFE = Effect::fileEdit(SM, SM.getMainFileID(),
  //                                  tooling::Replacements(DeleteFuncBody));
}

std::string
MoveClassToOwnFile::calcClassDeclEdits(Effect &Effect,
                                       const CXXRecordDecl &ClassDecl,
                                       const clang::SourceManager &SM) {
  auto FullClassDeclStr = getSymbolString(SM, ClassDecl.getSourceRange());
  auto NamespaceStrings = getNamespaces(ClassDecl);
  std::string HeaderStr = "#pragma once\n\n";
  for (auto NS : NamespaceStrings) {
    HeaderStr += "namespace " + NS + "\n{\n";
  }
  HeaderStr += FullClassDeclStr + ";\n";
  for (auto NS : NamespaceStrings) {
    HeaderStr += "}\n";
  }
  auto CurrentFileID = SM.getFileID(ClassDecl.getSourceRange().getBegin());
  auto Filepath = getCanonicalPath(SM.getFileEntryForID(CurrentFileID), SM);
  if (!Filepath) {
    return "Couldnt get canonical path for " +
           SM.getFilename(ClassDecl.getSourceRange().getBegin()).str();
  }
  auto Dir = std::experimental::filesystem::path(*Filepath).parent_path();
  auto TargetHeaderFilepath = Dir / (ClassDecl.getNameAsString() + ".h");
  std::clog << "TargetHeaderFilepath: " << TargetHeaderFilepath.string()
            << "\n content:\n"
            << HeaderStr << std::endl;

  auto TargetHeaderExists =
      std::experimental::filesystem::exists(TargetHeaderFilepath);
  if (TargetHeaderExists) {
    return "Target header file exists already: " +
           TargetHeaderFilepath.string();
  }

  // auto HeaderOffset =
  //     TargetHeaderExists
  //         ? std::experimental::filesystem::file_size(TargetHeaderFilepath)
  //         : 0;

  // std::ofstream(TargetHeaderFilepath.c_str()); // create file
  // if (!std::experimental::filesystem::file_size(TargetHeaderFilepath)) {
  //   return "Failed to create header file with path " +
  //          TargetHeaderFilepath.string();
  // }

  tooling::Replacements Reps(
      tooling::Replacement(TargetHeaderFilepath.string(), 0, 0, HeaderStr));
  Edit TargetHeaderEdit("", Reps);
  auto HeaderEdit =
      std::make_pair(TargetHeaderFilepath.string(), TargetHeaderEdit);

  Effect.ApplyEdits.try_emplace(TargetHeaderFilepath.string(),
                                std::move(TargetHeaderEdit));
  tooling::Replacements MainFileReps(replaceDecl(SM, ClassDecl));
  auto Edit = Effect::fileEdit(SM, SM.getMainFileID(), MainFileReps);
  if (!Edit) {
    return "Couldnt make edit for " +
           SM.getFileEntryForID(SM.getMainFileID())->getName().str();
  }
  Effect.ApplyEdits.try_emplace(Edit->first, Edit->second);
  return "";
}

std::string MoveClassToOwnFile::calcMethodDefinitionEdits(
    Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
    const SymbolIndex &Index) {
  const auto &SM = AST.getSourceManager();
  std::clog << "Function definitions of " << ClassDecl.getNameAsString()
            << std::endl;
  std::map<std::string, std::string> FileContentMap;
  for (CXXMethodDecl *Method : ClassDecl.methods()) {
    if (!Method) {
      continue;
    }
    std::clog << "Method " << Method->getNameAsString() << std::endl;
    // if(Method->hasInlineBody()) {
    //   continue;
    // }
    FunctionDecl *Definition = Method->getDefinition();
    if (Definition
        //  &&        getSourceRangeLength(SM, Definition->getSourceRange()) > 1
    ) {
      std::clog << "Function " << Method->getNameAsString() << " at "
                << getSourceRangeAsString(SM, Definition->getSourceRange())
                << " inline: " << Method->hasInlineBody() << std::endl;
    } else {

      std::clog << "Looking for definition of symbol at "
                << getSourceLocationAsString(SM, Method->getLocation());
      Position Pos = sourceLocToPosition(SM, Method->getLocation());
      std::vector<LocatedSymbol> LocatedSymbols =
          locateSymbolAt(AST, Pos, &Index);
      for (const LocatedSymbol &LocSym : LocatedSymbols) {
        std::clog << "symbol : " << LocSym.Name << " at "
                  << LocSym.PreferredDeclaration.uri.file().str() << ":"
                  << LocSym.PreferredDeclaration.range.start.line << std::endl;
        if (LocSym.Definition) {
          std::clog << "definition: " << LocSym.Definition->uri.file().str()
                    << ":" << LocSym.Definition->range.start.line << "-"
                    << LocSym.Definition->range.end.line << std::endl;
          std::string FileContent;
          auto FilePathStr = LocSym.Definition->uri.file().str();
          if (FileContentMap.count(FilePathStr) == 0) {
            std::ifstream File(LocSym.Definition->uri.file().str());
            if (!File.is_open()) {
              return "Failed to read file " + FilePathStr;
            }
            FileContent = std::string((std::istreambuf_iterator<char>(File)),
                                      std::istreambuf_iterator<char>());
            FileContentMap[FilePathStr] = FileContent;
          } else {
            FileContent = FileContentMap.at(FilePathStr);
          }
          auto Offset = positionToOffset(StringRef(FileContent),
                                         LocSym.Definition->range.start);
          if (!Offset) {
            return "Failed to get file offset";
          }
          auto DefStr = findFunctionDefinition(StringRef(FileContent), *Offset);
          if (!DefStr) {
            return "Failed to get function def str";
          }
          std::clog << "Function Def String: " << DefStr->str();
        }
      }
    }
  }
  return "";
}
std::string MoveClassToOwnFile::calcStaticVariableDefinitionEdits(
    Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
    const SymbolIndex &Index) {
  const auto &SM = AST.getSourceManager();
  // const Decl *Decl = ClassDecl.getNextDeclInContext();

  for (Decl *Decl : ClassDecl.decls()) {
    std::clog << "Decl: " << getSymbolString(SM, Decl->getSourceRange())
              << std::endl;

    if (VarDecl::classof(Decl)) {
      VarDecl *Var = static_cast<VarDecl *>(Decl);
      std::clog << "Found var: " << getSymbolString(SM, Var->getSourceRange())
                << " static: " << Var->isStaticDataMember() << std::endl;
    }
  }
  // while (Decl) {
  //   std::clog << "Decl: " << getSymbolString(SM, Decl->getSourceRange())
  //             << std::endl;
  //   Decl = Decl->getNextDeclInContext();
  // }
  for (FieldDecl *Field : ClassDecl.fields()) {
    std::clog << "Field: " << getSymbolString(SM, Field->getSourceRange())
              << std::endl;
  }
  return "";
}
} // namespace clangd
} // namespace clang