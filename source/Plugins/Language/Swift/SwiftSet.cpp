//===-- SwiftSet.cpp --------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "SwiftSet.h"

#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/SwiftASTContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SwiftLanguageRuntime.h"

#include "swift/AST/ASTContext.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;
using namespace lldb_private::formatters::swift;

namespace lldb_private {
namespace formatters {
namespace swift {
class SwiftSetStorageBufferHandler
    : public SwiftHashedContainerStorageBufferHandler {
public:
  SwiftHashedContainerBufferHandler::Kind GetKind() { return Kind::eSet; }

  static ConstString GetMangledStorageTypeName();

  static ConstString GetDemangledStorageTypeName();

  virtual lldb::ValueObjectSP GetElementAtIndex(size_t);

  SwiftSetStorageBufferHandler(ValueObjectSP nativeStorage_sp,
                              CompilerType key_type)
      : SwiftHashedContainerStorageBufferHandler(nativeStorage_sp, key_type,
                                                CompilerType()) {}
  friend class SwiftHashedContainerBufferHandler;

private:
};

class SwiftSetSyntheticFrontEndBufferHandler
    : public SwiftHashedContainerSyntheticFrontEndBufferHandler {
public:
  SwiftHashedContainerBufferHandler::Kind GetKind() { return Kind::eSet; }

  virtual ~SwiftSetSyntheticFrontEndBufferHandler() {}

  SwiftSetSyntheticFrontEndBufferHandler(lldb::ValueObjectSP valobj_sp)
      : SwiftHashedContainerSyntheticFrontEndBufferHandler(valobj_sp) {}
  friend class SwiftHashedContainerBufferHandler;

private:
};

class SetSyntheticFrontEnd : public HashedContainerSyntheticFrontEnd {
public:
  SetSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : HashedContainerSyntheticFrontEnd(valobj_sp) {}

  virtual bool Update();

  virtual ~SetSyntheticFrontEnd() = default;
};
}
}
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::swift::SetSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return NULL;
  return (new SetSyntheticFrontEnd(valobj_sp));
}

bool lldb_private::formatters::swift::SetSyntheticFrontEnd::Update() {
  m_buffer = SwiftHashedContainerBufferHandler::CreateBufferHandler(
      m_backend,
      [](ValueObjectSP a, CompilerType b,
         CompilerType c) -> SwiftHashedContainerBufferHandler * {
        return new SwiftSetStorageBufferHandler(a, b);
      },
      [](ValueObjectSP a) -> SwiftHashedContainerBufferHandler * {
        return new SwiftSetSyntheticFrontEndBufferHandler(a);
      },
      SwiftSetStorageBufferHandler::GetMangledStorageTypeName(),
      SwiftSetStorageBufferHandler::GetDemangledStorageTypeName());
  return false;
}

bool lldb_private::formatters::swift::Set_SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  auto handler = SwiftHashedContainerBufferHandler::CreateBufferHandler(
      valobj,
      [](ValueObjectSP a, CompilerType b,
         CompilerType c) -> SwiftHashedContainerBufferHandler * {
        return new SwiftSetStorageBufferHandler(a, b);
      },
      [](ValueObjectSP a) -> SwiftHashedContainerBufferHandler * {
        return new SwiftSetSyntheticFrontEndBufferHandler(a);
      },
      SwiftSetStorageBufferHandler::GetMangledStorageTypeName(),
      SwiftSetStorageBufferHandler::GetDemangledStorageTypeName());

  if (!handler)
    return false;

  auto count = handler->GetCount();

  stream.Printf("%zu value%s", count, (count == 1 ? "" : "s"));

  return true;
};

lldb::ValueObjectSP SwiftSetStorageBufferHandler::GetElementAtIndex(size_t idx) {
  ValueObjectSP parent_element(
      this->SwiftHashedContainerStorageBufferHandler::GetElementAtIndex(idx));
  if (!parent_element)
    return parent_element;
  static ConstString g_key("key");
  ValueObjectSP key_child(parent_element->GetChildMemberWithName(g_key, true));
  return key_child ? (key_child->SetName(parent_element->GetName()), key_child)
                   : parent_element;
}

ConstString SwiftSetStorageBufferHandler::GetMangledStorageTypeName() {
  static ConstString g_name(SwiftLanguageRuntime::GetCurrentMangledName("_TtCs22_NativeSetStorageOwner"));
  return g_name;
}

ConstString SwiftSetStorageBufferHandler::GetDemangledStorageTypeName() {
  static ConstString g_name(
      "Swift._NativeSetStorageOwner with unmangled suffix");
  return g_name;
}
