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
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"

#include <AST.h>

#include <climits>
#include <experimental/bits/fs_ops.h>
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
  std::clog << "Trying to apply MoveClassToOwnFile" << std::endl;
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

  // if (hasSubRecordWithFunctions(*ClassDecl, SM)) {
  //   return llvm::createStringError(
  //       llvm::inconvertibleErrorCode(),
  //       "Cannot move class with sub-class/struct with functions.");
  // }

  // Position Pos = sourceLocToPosition(SM, Inputs.Cursor);
  // locateSymbolAt(*Inputs.AST, Pos, Inputs.Index);
  Tweak::Effect Effect;
  {
    auto ErrorStr = extractClassDeclEdits(Effect, *ClassDecl, *Inputs.AST);
    if (!ErrorStr.empty()) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
    }
  }

  {
    auto ErrorStr =
        extractSourceEdits(Effect, *ClassDecl, *Inputs.AST, *Inputs.Index);
    if (!ErrorStr.empty()) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
    }
  }

  {
    // auto ErrorStr = calcStaticVariableDefinitionEdits(
    //     Effect, *ClassDecl, *Inputs.AST, *Inputs.Index);
    // if (!ErrorStr.empty()) {
    //   return llvm::createStringError(llvm::inconvertibleErrorCode(),
    //   ErrorStr);
    // }
  }
  // TODO: Move also static variable definitions

  for (llvm::StringMapEntry<Edit> &E : Effect.ApplyEdits) {
    std::clog << "File: " << E.first().str() << std::endl;
    for (auto Edit : E.getValue().Replacements) {
      std::clog << Edit.getFilePath().str() << ":" << Edit.getOffset() << ":"
                << Edit.getLength() << std::endl;
    }
    if (!std::experimental::filesystem::exists(E.first().str())) {
      std::ofstream F(E.first().str().c_str()); // create file

      if (!F.is_open()) {
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            std::string("Failed to create file with path ") + E.first().str());
      }
      // for (auto &Repl : E.second.Replacements) {
      //   F << Repl.getReplacementText().str();
      // }
      // E.second.Replacements.clear();
    }
  }

  // return llvm::createStringError(llvm::inconvertibleErrorCode(), "Dummy
  // mode");
  return std::move(Effect);
  // auto HeaderFE = Effect::fileEdit(SM, SM.getMainFileID(),
  //                                  tooling::Replacements(DeleteFuncBody));
}

std::string MoveClassToOwnFile::extractClassDeclEdits(
    Effect &Effect, const CXXRecordDecl &ClassDecl, const ParsedAST &AST) {
  const clang::SourceManager &SM = AST.getSourceManager();
  auto FullClassDeclStr =
      getSymbolString(SM, ClassDecl.getSourceRange()) + ";\n\n";
  auto CurrentFileID = SM.getFileID(ClassDecl.getSourceRange().getBegin());
  auto OriginalHeaderFilepath =
      getCanonicalPath(SM.getFileEntryForID(CurrentFileID), SM);
  if (!OriginalHeaderFilepath) {
    return "Couldnt get canonical path for " +
           SM.getFilename(ClassDecl.getSourceRange().getBegin()).str();
  }
  auto Dir = std::experimental::filesystem::path(*OriginalHeaderFilepath)
                 .parent_path();
  auto TargetHeaderFilepath = Dir / (ClassDecl.getNameAsString() + ".h");
  auto TargetHeaderExistsAndNotEmpty =
      std::experimental::filesystem::exists(TargetHeaderFilepath) &&
      std::experimental::filesystem::file_size(TargetHeaderFilepath) > 0;
  std::clog << "TargetHeaderFilepath: " << TargetHeaderFilepath.string()
            << " exists: " << TargetHeaderExistsAndNotEmpty << std::endl;

  if (TargetHeaderExistsAndNotEmpty) {
    return "Target header file exists already: " +
           TargetHeaderFilepath.string();
  }

  for (CXXMethodDecl *Method : ClassDecl.methods()) {
    if (!Method) {
      continue;
    }
    std::clog << "Method " << Method->getNameAsString() << std::endl;
    // if(Method->hasInlineBody()) {
    //   continue;
    // }
    FunctionDecl *Definition = Method->getDefinition();
    if (Definition) {
      std::clog << "Function " << Method->getNameAsString() << " at "
                << getSourceRangeAsString(SM, Definition->getSourceRange())
                << " inline: " << Method->hasInlineBody()
                << " has body: " << Method->hasBody() << std::endl;
      if (Method->hasBody() && !Method->hasInlineBody() &&
          getSourceRangeLength(SM, Definition->getSourceRange()) > 1) {

        FullClassDeclStr +=
            getSymbolString(SM, Definition->getSourceRange()) + "\n\n";
        std::clog << ("Adding edit for file " + *OriginalHeaderFilepath +
                      " for definition " + Definition->getNameAsString())
                  << std::endl;
        if (addEdit(Effect.ApplyEdits, *OriginalHeaderFilepath,
                    SM.getBufferData(CurrentFileID),
                    replaceDecl(SM, *Definition, ""))) {
          return "Failed to add edit for file " + *OriginalHeaderFilepath +
                 " for definition " + Definition->getNameAsString();
        }
        // Edit OriginalHeaderEdit(
        //     SM.getBufferData(CurrentFileID),
        //     tooling::Replacements{replaceDecl(SM, *Definition, "")});

        // (void)Effect.ApplyEdits[*OriginalHeaderFilepath].Replacements.add(
        //     replaceDecl(SM, *Definition, ""));
        // Effect.ApplyEdits.try_emplace(*OriginalHeaderFilepath,
        // std::move(OriginalHeaderEdit));
      }
    }
  }

  auto NamespaceStrings = getNamespaces(ClassDecl);
  std::string HeaderStr = "#pragma once\n\n";

  for (auto Include : extractIncludeStrings(AST)) {
    HeaderStr += Include + "\n";
  }
  HeaderStr += "\n";
  for (auto NS : NamespaceStrings) {
    HeaderStr += "namespace " + NS + "\n{\n";
  }
  HeaderStr += FullClassDeclStr;
  for (auto NS : NamespaceStrings) {
    HeaderStr += "}\n";
  }
  std::clog << "HeaderStr " << HeaderStr << std::endl;
  // auto HeaderOffset =
  //     TargetHeaderExists
  //         ? std::experimental::filesystem::file_size(TargetHeaderFilepath)
  //         : 0;

  // std::ofstream(TargetHeaderFilepath.c_str()); // create file
  // if (!std::experimental::filesystem::exists(TargetHeaderFilepath)) {
  //   return "Failed to create header file with path " +
  //          TargetHeaderFilepath.string();
  // }
  tooling::Replacements Reps(
      tooling::Replacement(TargetHeaderFilepath.string(), 0, 0, HeaderStr));
  Edit TargetHeaderEdit("", Reps);

  Effect.ApplyEdits.try_emplace(TargetHeaderFilepath.string(),
                                std::move(TargetHeaderEdit));

  // Edit OriginalHeaderEdit(
  //     SM.getBufferData(CurrentFileID),
  //     tooling::Replacements{replaceDecl(SM, ClassDecl, "")});

  // Effect.ApplyEdits.try_emplace(*OriginalHeaderFilepath,
  //                               std::move(OriginalHeaderEdit));

  if (addEdit(Effect.ApplyEdits, *OriginalHeaderFilepath,
              SM.getBufferData(CurrentFileID),
              replaceDecl(SM, ClassDecl, ""))) {
    return "Failed to add edit for file " + *OriginalHeaderFilepath +
           " for def " + ClassDecl.getNameAsString();
  }

  // (void)Effect.ApplyEdits[*OriginalHeaderFilepath].Replacements.add(
  //     replaceDecl(SM, ClassDecl, ""));

  // tooling::Replacements MainFileReps(replaceDecl(SM, ClassDecl, ""));
  // auto Edit = Effect::fileEdit(SM, SM.getMainFileID(), MainFileReps);
  // if (!Edit) {
  //   const FileEntry *Entry = SM.getFileEntryForID(SM.getMainFileID());
  //   return "Couldnt make edit for " +
  //          (Entry ? Entry->getName().str() : "Unknownfile");
  // }
  // Effect.ApplyEdits.try_emplace(Edit->first, Edit->second);
  return "";
}

std::string MoveClassToOwnFile::extractSourceEdits(
    Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
    const SymbolIndex &Index) {
  const auto &SM = AST.getSourceManager();
  std::clog << "Function definitions of " << ClassDecl.getNameAsString()
            << std::endl;
  std::map<std::string, std::string> FileContentMap;
  std::vector<std::string> IncludeStrings;
  auto DefStrings = extractMethodDefinitionStrings(
      Effect, ClassDecl, AST, Index, FileContentMap, IncludeStrings);
  if (!DefStrings) {
    std::string Str;
    llvm::raw_string_ostream S(Str);
    S << DefStrings.takeError();
    return Str;
  }
  auto NamespaceStrings = getNamespaces(ClassDecl);
  std::string SourceStr =
      "#include \"" + ClassDecl.getNameAsString() + ".h\"\n\n";
  for (auto &Include : IncludeStrings) {
    SourceStr += Include + "\n";
  }
  SourceStr += "\n";
  for (auto NS : NamespaceStrings) {
    SourceStr += "namespace " + NS + "\n{\n";
  }
  for (auto &S : *DefStrings) {
    SourceStr += "\n" + S + "\n\n";
  }
  for (auto NS : NamespaceStrings) {
    SourceStr += "}\n";
  }
  auto CurrentFileID = SM.getFileID(ClassDecl.getSourceRange().getBegin());
  auto Filepath = getCanonicalPath(SM.getFileEntryForID(CurrentFileID), SM);
  if (!Filepath) {
    return "Couldnt get canonical path for " +
           SM.getFilename(ClassDecl.getSourceRange().getBegin()).str();
  }
  auto Dir = std::experimental::filesystem::path(*Filepath).parent_path();
  auto TargetSourceFilepath = Dir / (ClassDecl.getNameAsString() + ".cpp");
  auto TargetSourceExistsAndNotEmpty =
      std::experimental::filesystem::exists(TargetSourceFilepath) &&
      std::experimental::filesystem::file_size(TargetSourceFilepath) > 0;
  std::clog << "TargetSourceFilepath: " << TargetSourceFilepath.string()
            << " exists: " << TargetSourceExistsAndNotEmpty << std::endl;

  if (TargetSourceExistsAndNotEmpty) {
    return "Target source file exists already: " +
           TargetSourceFilepath.string();
  }

  // std::ofstream(TargetSourceFilepath.c_str()); // create file
  // if (!std::experimental::filesystem::exists(TargetSourceFilepath)) {
  //   return "Failed to create source file with path " +
  //          TargetSourceFilepath.string();
  // }

  // tooling::Replacements Reps(
  //     tooling::Replacement(TargetSourceFilepath.string(), 0, 0, SourceStr));
  // Edit TargetSourceEdit("", Reps);
  // auto HeaderEdit =
  //     std::make_pair(TargetSourceFilepath.string(), TargetSourceEdit);

  if (addEdit(Effect.ApplyEdits, TargetSourceFilepath.string(), "",
              tooling::Replacement(TargetSourceFilepath.string(), 0, 0,
                                   SourceStr))) {
    return "Failed to add edit for file " + TargetSourceFilepath.string() +
           " for def " + ClassDecl.getNameAsString();
  }

  // Effect.ApplyEdits.try_emplace(TargetSourceFilepath.string(),
  //                               std::move(TargetSourceEdit));
  // tooling::Replacements MainFileReps(replaceDecl(SM, ClassDecl, ""));
  // auto Edit = Effect::fileEdit(SM, SM.getMainFileID(), MainFileReps);
  // if (!Edit) {
  //   return "Couldnt make edit for " +
  //          SM.getFileEntryForID(SM.getMainFileID())->getName().str();
  // }
  // Effect.ApplyEdits.try_emplace(Edit->first, Edit->second);

  // const FileEntry *Entry = SM.getFileEntryForID(SM.getMainFileID());
  // if (!Entry) {
  //   return "Failed to get file entry for it ";
  // }
  // llvm::StringRef OriginalSourceFilePath = Entry->getName();
  // if (addEdit(Effect.ApplyEdits,
  //             SM.getFileEntryForID(SM.getMainFileID())->getName(),
  //             SM.getBufferData(SM.getMainFileID()),
  //             replaceDecl(SM, ClassDecl, ""))) {
  //   return "Failed to add edit for file " + OriginalSourceFilePath.str() +
  //          " for def " + ClassDecl.getNameAsString();
  // }

  return "";
}

Expected<std::vector<std::string>>
MoveClassToOwnFile::extractStaticVariableDefinitionEdits(
    Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
    const SymbolIndex &Index,
    std::map<std::string, std::string> &FileContentMap) {
  const auto &SM = AST.getSourceManager();
  std::vector<std::string> StaticVarDefStrings;
  // const Decl *Decl = ClassDecl.getNextDeclInContext();

  for (Decl *Decl : ClassDecl.decls()) {
    std::clog << "Decl: " << getSymbolString(SM, Decl->getSourceRange())
              << std::endl;

    if (VarDecl::classof(Decl)) {
      VarDecl *Var = static_cast<VarDecl *>(Decl);
      std::clog << "Found var: " << getSymbolString(SM, Var->getSourceRange())
                << " static: " << Var->isStaticDataMember() << std::endl;
      Position Pos = sourceLocToPosition(SM, Decl->getLocation());
      std::vector<LocatedSymbol> LocatedSymbols =
          locateSymbolAt(AST, Pos, &Index);
      for (const LocatedSymbol &LocSym : LocatedSymbols) {
        if (LocSym.Definition) {
          // std::clog << "definition: " <<
          // LocSym.Definition->uri.file().str()
          //           << ":" << LocSym.Definition->range.start.line << "-"
          //           << LocSym.Definition->range.end.line << std::endl;
          std::string FileContent;
          auto FilePathStr = LocSym.Definition->uri.file().str();
          if (FileContentMap.count(FilePathStr) == 0) {
            std::ifstream File(LocSym.Definition->uri.file().str());
            if (!File.is_open()) {
              return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                             "Failed to read file " +
                                                 FilePathStr);
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
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Failed to get file offset");
          }
          int FuncDefOffset = 0;
          int FuncDefLength = 0;
          auto DefStr = findFunctionDefinition(StringRef(FileContent), *Offset,
                                               FuncDefOffset, FuncDefLength);
          if (!DefStr) {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Failed to get def str");
          }
          StaticVarDefStrings.push_back(DefStr->str());
          // std::clog << "Function Def String: " << DefStr->str();
        }
      }
    }
  }
  // while (Decl) {
  //   std::clog << "Decl: " << getSymbolString(SM, Decl->getSourceRange())
  //             << std::endl;
  //   Decl = Decl->getNextDeclInContext();
  // }
  // for (FieldDecl *Field : ClassDecl.fields()) {
  //   std::clog << "Field: " << getSymbolString(SM, Field->getSourceRange())
  //             << std::endl;
  // }
  return StaticVarDefStrings;
  // return llvm::createStringError(llvm::inconvertibleErrorCode(), ErrorStr);
}

Expected<std::vector<std::string>>
MoveClassToOwnFile::extractMethodDefinitionStrings(
    Effect &Effect, const CXXRecordDecl &ClassDecl, ParsedAST &AST,
    const SymbolIndex &Index,
    std::map<std::string, std::string> &FileContentMap,
    std::vector<std::string> &IncludeStrings) {
  std::vector<std::string> Result;
  const auto &SM = AST.getSourceManager();
  FileID CurrentFileID = SM.getFileID(ClassDecl.getSourceRange().getBegin());
  llvm::Optional<std::string> Filepath =
      getCanonicalPath(SM.getFileEntryForID(CurrentFileID), SM);
  if (!Filepath) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Couldnt get canonical path for " +
            SM.getFilename(ClassDecl.getSourceRange().getBegin()).str());
  }

  for (CXXMethodDecl *Method : ClassDecl.methods()) {
    if (!Method) {
      continue;
    }
    // std::clog << "Method " << Method->getNameAsString() << std::endl;
    // if(Method->hasInlineBody()) {
    //   continue;
    // }
    FunctionDecl *Definition = Method->getDefinition();
    if (Definition
        //  &&        getSourceRangeLength(SM, Definition->getSourceRange()) >
        //  1
    ) {
      // std::clog << "Function " << Method->getNameAsString() << " at "
      //           << getSourceRangeAsString(SM, Definition->getSourceRange())
      //           << " inline: " << Method->hasInlineBody() << std::endl;
    } else {

      // std::clog << "Looking for definition of symbol at "
      //           << getSourceLocationAsString(SM, Method->getLocation());
      Position Pos = sourceLocToPosition(SM, Method->getLocation());
      std::vector<LocatedSymbol> LocatedSymbols =
          locateSymbolAt(AST, Pos, &Index);
      for (const LocatedSymbol &LocSym : LocatedSymbols) {
        // std::clog << "symbol : " << LocSym.Name << " at "
        //           << LocSym.PreferredDeclaration.uri.file().str() << ":"
        //           << LocSym.PreferredDeclaration.range.start.line <<
        //           std::endl;
        if (LocSym.Definition) {
          std::clog << "definition: " << LocSym.Definition->uri.file().str()
                    << ":" << LocSym.Definition->range.start.line << "-"
                    << LocSym.Definition->range.end.line << std::endl;
          std::string FileContent;
          auto FilePathStr = LocSym.Definition->uri.file().str();
          if (FileContentMap.count(FilePathStr) == 0) {
            std::ifstream File(LocSym.Definition->uri.file().str());
            if (!File.is_open()) {
              return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                             "Failed to read file " +
                                                 FilePathStr);
            }
            FileContent = std::string((std::istreambuf_iterator<char>(File)),
                                      std::istreambuf_iterator<char>());
            FileContentMap[FilePathStr] = FileContent;
          } else {
            FileContent = FileContentMap.at(FilePathStr);
          }
          auto Includes = FindIncludes(FileContent);
          IncludeStrings.insert(IncludeStrings.end(), Includes.begin(),
                                Includes.end());
          llvm::Expected<unsigned long> Offset = positionToOffset(
              StringRef(FileContent), LocSym.Definition->range.start);

          if (!Offset) {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Failed to get file offset");
          }
          int FuncDefOffset = 0;
          int FuncDefLength = 0;

          llvm::Expected<StringRef> DefStr = findFunctionDefinition(
              StringRef(FileContent), *Offset, FuncDefOffset, FuncDefLength);
          if (!DefStr) {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Failed to get def str");
          }
          Result.push_back(DefStr->str());

          // Edit OriginalHeaderEdit(
          //     SM.getBufferData(CurrentFileID),
          //     tooling::Replacements{tooling::Replacement(
          //         *Filepath, FuncDefOffset, FuncDefLength, "")});
          // auto HeaderEdit =
          //     std::make_pair(Filepath->c_str(), OriginalHeaderEdit);
          // Effect.ApplyEdits.try_emplace(Filepath->c_str(),
          //                               std::move(OriginalHeaderEdit));
          std::clog << "path: " << FilePathStr << " " << FuncDefOffset << ":"
                    << FuncDefLength << std::endl;

          if (addEdit(Effect.ApplyEdits, FilePathStr, StringRef(FileContent),
                      tooling::Replacement(FilePathStr, FuncDefOffset,
                                           FuncDefLength, ""))) {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "Couldnt add edit for  " +
                                               FilePathStr);
          }
          // std::clog << "Function Def String: " << DefStr->str();
        }
      }
    }
  }
  return Result;
}

Expected<std::vector<std::string>>
MoveClassToOwnFile::calcStaticVariableDefinitionStrings(
    const CXXRecordDecl &ClassDecl, ParsedAST &AST, const SymbolIndex &Index,
    std::map<std::string, std::string> &FileContentMap) {

  std::vector<std::string> Result;
  return Result;
}

bool MoveClassToOwnFile::hasSubRecordWithFunctions(
    const CXXRecordDecl &ClassDecl, const clang::SourceManager &SM) {
  for (Decl *Decl : ClassDecl.decls()) {
    std::clog << "Decl: " << getSymbolString(SM, Decl->getSourceRange())
              << std::endl;

    if (CXXRecordDecl::classof(Decl)) {
      CXXRecordDecl *RecDecl = static_cast<CXXRecordDecl *>(Decl);
      for (CXXMethodDecl *M : RecDecl->methods()) {

        std::clog << "Method: " << M->getNameAsString() << ": "
                  << getSymbolString(SM, M->getSourceRange()) << std::endl;
        if (M && !M->getNameAsString().empty() &&
            getSourceRangeLength(SM, M->getSourceRange()) > 0) {
          return true;
        }
      }
    }
  }

  return false;
}

llvm::Error
MoveClassToOwnFile::addEdit(FileEdits &EditMap, StringRef Filepath,
                            StringRef Code,
                            const tooling::Replacement &Repl) const {

  auto Iter = EditMap.find(Filepath);
  if (Iter == EditMap.end()) {
    std::clog << "New entry" << std::endl;
    Edit NewEdit(Code, tooling::Replacements{Repl});
    EditMap.try_emplace(Filepath, std::move(NewEdit));
    std::clog << __FILE__ << ":" << __LINE__ << std::endl;
    return llvm::Error::success();
  }
  std::clog << "extending entry" << std::endl;
  return Iter->second.Replacements.add(Repl);
}
std::vector<std::string>
MoveClassToOwnFile::extractIncludeStrings(const ParsedAST &AST) {
  std::vector<std::string> IncludeStrings;

  // const SourceManager &SM = AST.getSourceManager();
  for (auto &Include : AST.getIncludeStructure().MainFileIncludes) {
    std::clog << "Include: " << Include.Written << std::endl;
    std::string DirectiveStr;
    if (Include.Directive == tok::pp_include) {
      DirectiveStr = "#include";
    } else if (Include.Directive == tok::pp_include_next) {
      DirectiveStr = "#include_next";
    }
    if (!DirectiveStr.empty()) {
      IncludeStrings.push_back(DirectiveStr + Include.Written);
    }
  }

  // for (const syntax::Token &Token :
  //      AST.getTokens().spelledTokens(SM.getMainFileID())) {
  //   std::clog << "Tok: " << Token.str() << std::endl;
  //   if (Token.kind() == tok::hash) {
  //     IncludeStrings.push_back(Token.str());
  //   }
  // }
  return IncludeStrings;
}
} // namespace clangd
} // namespace clang