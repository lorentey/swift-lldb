//===-- SwiftDictionary.cpp -------------------------------------*- C++ -*-===//
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

#include "SwiftDictionary.h"

#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/SwiftASTContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SwiftLanguageRuntime.h"

#include "swift/AST/ASTContext.h"
#include "swift/Demangling/ManglingMacros.h"
#include "llvm/ADT/StringRef.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;
using namespace lldb_private::formatters::swift;

namespace lldb_private {
namespace formatters {
namespace swift {
class SwiftDictionaryStorageBufferHandler
    : public SwiftHashedContainerStorageBufferHandler {
public:
  SwiftHashedContainerBufferHandler::Kind GetKind() {
    return Kind::eDictionary;
  }

  static ConstString GetMangledStorageTypeName();

  static ConstString GetDemangledStorageTypeName();

  SwiftDictionaryStorageBufferHandler(ValueObjectSP storage_sp,
                                      CompilerType key_type,
                                      CompilerType value_type)
      : SwiftHashedContainerStorageBufferHandler(storage_sp, key_type,
                                                 value_type) {}
  friend class SwiftHashedContainerBufferHandler;

private:
};

class SwiftDictionarySyntheticFrontEndBufferHandler
    : public SwiftHashedContainerSyntheticFrontEndBufferHandler {
public:
  SwiftHashedContainerBufferHandler::Kind GetKind() {
    return Kind::eDictionary;
  }

  virtual ~SwiftDictionarySyntheticFrontEndBufferHandler() {}

  SwiftDictionarySyntheticFrontEndBufferHandler(lldb::ValueObjectSP valobj_sp)
      : SwiftHashedContainerSyntheticFrontEndBufferHandler(valobj_sp) {}
  friend class SwiftHashedContainerBufferHandler;

private:
};

class DictionarySyntheticFrontEnd : public HashedContainerSyntheticFrontEnd {
public:
  DictionarySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : HashedContainerSyntheticFrontEnd(valobj_sp) {}

  virtual bool Update();

  virtual ~DictionarySyntheticFrontEnd() = default;
};
}
}
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::swift::DictionarySyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return NULL;
  return (new DictionarySyntheticFrontEnd(valobj_sp));
}

bool lldb_private::formatters::swift::DictionarySyntheticFrontEnd::Update() {
  m_buffer = SwiftHashedContainerBufferHandler::CreateBufferHandler(
      m_backend,
      [](ValueObjectSP valobj_sp,
         CompilerType key,
         CompilerType value) -> SwiftHashedContainerBufferHandler * {
        return new SwiftDictionaryStorageBufferHandler(valobj_sp, key, value);
      },
      [](ValueObjectSP valobj_sp) -> SwiftHashedContainerBufferHandler * {
        return new SwiftDictionarySyntheticFrontEndBufferHandler(valobj_sp);
      },
      SwiftDictionaryStorageBufferHandler::GetMangledStorageTypeName(),
      SwiftDictionaryStorageBufferHandler::GetDemangledStorageTypeName());
  return false;
}

bool lldb_private::formatters::swift::Dictionary_SummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  auto handler = SwiftHashedContainerBufferHandler::CreateBufferHandler(
      valobj,
      [](ValueObjectSP valobj_sp,
         CompilerType key,
         CompilerType value) -> SwiftHashedContainerBufferHandler * {
        return new SwiftDictionaryStorageBufferHandler(valobj_sp, key, value);
      },
      [](ValueObjectSP valobj_sp) -> SwiftHashedContainerBufferHandler * {
        return new SwiftDictionarySyntheticFrontEndBufferHandler(valobj_sp);
      },
      SwiftDictionaryStorageBufferHandler::GetMangledStorageTypeName(),
      SwiftDictionaryStorageBufferHandler::GetDemangledStorageTypeName());

  if (!handler)
    return false;

  auto count = handler->GetCount();

  stream.Printf("%zu key/value pair%s", count, (count == 1 ? "" : "s"));

  return true;
};

ConstString SwiftDictionaryStorageBufferHandler::GetMangledStorageTypeName() {
  static ConstString g_name(
    SwiftLanguageRuntime::GetCurrentMangledName(
      "_TtGCs37_HashableTypedNativeDictionaryStorage"));
  // static ConstString g_name(
  //   SwiftLanguageRuntime::GetCurrentMangledName(
  //     MANGLING_PREFIX_STR "s37_HashableTypedNativeDictionaryStorage"));
  return g_name;
}

ConstString SwiftDictionaryStorageBufferHandler::GetDemangledStorageTypeName() {
  static ConstString g_name(
      "Swift._HashableTypedNativeDictionaryStorage<");
  return g_name;
}
