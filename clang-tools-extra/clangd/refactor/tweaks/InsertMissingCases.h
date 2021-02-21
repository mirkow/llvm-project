/**
 * @file InsertMissingCases.h
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

class InsertMissingCases : public Tweak {
public:
  InsertMissingCases() {
    // std::clog << "InsertMissingCases created" << std::endl;
  }
  const char *id() const override;
  std::string title() const override;
  bool prepare(const Selection &Inputs) override;

  Expected<Effect> apply(const Selection &Inputs) override;
  Intent intent() const override { return Refactor; }

private:
  std::vector<std::string> MissingEnumValueStrings;
};

} // namespace clangd
} // namespace clang