/**
 * @file InsertVirtualFunctions.cpp
 * @author Mirko Waechter (mail@mirko-waechter.de)
 * @brief
 * @date 2021-01-17
 *
 * @copyright Copyright (c) 2021
 *
 */
#include "InsertVirtualFunctionsBase.h"

namespace clang {
namespace clangd {
namespace {

/**
 * @brief Refactor Tweak that inserts all *pure* virtual functions of all
 * subclasses of class.
 * The cursor must be somewhere in the class-name or the class-keyword of the
 * class declaration.
 *
 * Before:
 * @code
 * class A
 *   {
 *   public:
 *       virtual void foo() = 0;
 *       virtual int bar(int x, const double& y) {}
 *   };
 *
 *   class B : public A
 *   ^^^^^^^
 *   {
 *   public:
 *   };
 * @endcode
 *
 * After:
 * @code
 * class A
 *   {
 *   public:
 *       virtual void foo() = 0;
 *       virtual int bar(int x, const double& y) {}
 *   };
 *
 *   class B : public A
 *   {
 *   public:
 *        virtual void foo() override {}
 *   };
 * @endcode
 *
 */
class InsertPureVirtualFunctions : public InsertVirtualFunctionsTweakBase {
public:
  InsertPureVirtualFunctions()
      : InsertVirtualFunctionsTweakBase(ModeType::ePureVirtualFunctions) {}
  ~InsertPureVirtualFunctions() {}

  const char *id() const override;

  std::string title() const override {
    return "Insert pure virtual functions for class '" + getRecordName() + "'.";
  }
};
REGISTER_TWEAK(InsertPureVirtualFunctions)

/**
 * @brief Refactor Tweak that inserts all virtual functions of all
 * subclasses of class. Non-pure virtual functions are implemented as a comment.
 * The cursor must be somewhere in the class-name or the class-keyword of the
 * class declaration.
 *
 * Before:
 * @code
 * class A
 *   {
 *   public:
 *       virtual void foo() = 0;
 *       virtual int bar(int x, const double& y) {}
 *   };
 *
 *   class B : public A
 *   ^^^^^^^
 *   {
 *   public:
 *   };
 * @endcode
 *
 * After:
 * @code
 * class A
 *   {
 *   public:
 *       virtual void foo() = 0;
 *       virtual int bar(int x, const double& y) {}
 *   };
 *
 *   class B : public A
 *   {
 *   public:
 *        virtual void foo() override {}
 *        // virtual int bar(int x, const double& y) override {}
 *   };
 * @endcode
 *
 */
class InsertAllVirtualFunctions : public InsertVirtualFunctionsTweakBase {
public:
  InsertAllVirtualFunctions()
      : InsertVirtualFunctionsTweakBase(ModeType::eAllVirtualFunctions) {}
  ~InsertAllVirtualFunctions() {}

  const char *id() const override;

  std::string title() const override {
    return "Insert all virtual functions for class '" + getRecordName() + "'.";
  }
};
REGISTER_TWEAK(InsertAllVirtualFunctions)

} // namespace
} // namespace clangd
} // namespace clang