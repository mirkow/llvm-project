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

class InsertVirtualFunctionsTweakBase : public Tweak {
public:
  enum class ModeType { ePureVirtualFunctions, eAllVirtualFunctions };
  InsertVirtualFunctionsTweakBase(ModeType Mode) : Mode(Mode) {
    // std::clog << "InsertVirtualFunctionsTweakBase created" << std::endl;
  }

  bool prepare(const Selection &Inputs) override;

  Expected<Effect> apply(const Selection &Inputs) override;
  Intent intent() const override { return Refactor; }
  const std::string &getRecordName() const { return RecordName; }

private:
  int findIndendation(const SourceManager &SM,
                      const CXXRecordDecl &CxxRecordDecl,
                      int FallbackIndentation = 4);
  SourceLocation findInsertLocation(const SourceManager &SM,
                                    const CXXRecordDecl &CxxRecordDecl);

  enum class DefinitionState { pureVirtual, preImplemented, implemented };
  struct VirtualFunctionData {
    const clang::CXXMethodDecl *FuncDecl;
    std::string FuncSig;
    DefinitionState ImplementationState;
  };
  using VirtualFunctionsMap =
      std::map<std::pair<std::string, std::string>,
               InsertVirtualFunctionsTweakBase::VirtualFunctionData>;
  VirtualFunctionsMap
  getAllVirtualFunctions(const clang::SourceManager &SM,
                         const clang::CXXRecordDecl &Record);
  void getAllVirtualFunctionsRecursive(const clang::SourceManager &SM,
                                       const clang::CXXRecordDecl &Record,
                                       VirtualFunctionsMap &Functions);

  ModeType Mode;
  std::string RecordName;
};

} // namespace clangd
} // namespace clang