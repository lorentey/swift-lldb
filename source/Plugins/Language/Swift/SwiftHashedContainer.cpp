//===-- SwiftHashedContainer.cpp --------------------------------*- C++ -*-===//
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

#include "SwiftHashedContainer.h"

#include "lldb/Core/Value.h"  // +

#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/SwiftASTContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SwiftLanguageRuntime.h"
#include "lldb/Utility/DataBufferHeap.h"

#include "Plugins/Language/ObjC/NSDictionary.h"

#include "swift/AST/ASTContext.h"
#include "llvm/ADT/StringRef.h"

#include <iostream>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;
using namespace lldb_private::formatters::swift;

size_t SwiftHashedContainerSyntheticFrontEndBufferHandler::GetCount() {
  return m_frontend->CalculateNumChildren();
}

lldb_private::CompilerType
SwiftHashedContainerSyntheticFrontEndBufferHandler::GetElementType() {
  // this doesn't make sense here - the synthetic children know best
  return CompilerType();
}

lldb::ValueObjectSP
SwiftHashedContainerSyntheticFrontEndBufferHandler::GetElementAtIndex(
    size_t idx) {
  return m_frontend->GetChildAtIndex(idx);
}

SwiftHashedContainerSyntheticFrontEndBufferHandler::
    SwiftHashedContainerSyntheticFrontEndBufferHandler(
        lldb::ValueObjectSP valobj_sp)
    : m_valobj_sp(valobj_sp),
      m_frontend(NSDictionarySyntheticFrontEndCreator(nullptr, valobj_sp)) {
  // Cocoa frontends must be updated before use
  if (m_frontend)
    m_frontend->Update();
}

bool SwiftHashedContainerSyntheticFrontEndBufferHandler::IsValid() {
  return m_frontend.get() != nullptr;
}

size_t SwiftHashedContainerStorageBufferHandler::GetCount() { return m_count; }

lldb_private::CompilerType
SwiftHashedContainerStorageBufferHandler::GetElementType() {
  return m_element_type;
}

lldb::ValueObjectSP
SwiftHashedContainerStorageBufferHandler::GetElementAtIndex(size_t idx) {
  lldb::ValueObjectSP null_valobj_sp;
  if (idx >= m_count)
    return null_valobj_sp;
  if (!IsValid())
    return null_valobj_sp;
  int64_t found_idx = -1;
  Status error;
  for (Cell cell_idx = 0; cell_idx < m_capacity; cell_idx++) {
    const bool used = ReadBitmaskAtIndex(cell_idx, error);
    if (error.Fail()) {
      Status bitmask_error;
      bitmask_error.SetErrorStringWithFormat(
              "Failed to read bit-mask index from Dictionary: %s",
              error.AsCString());
      return ValueObjectConstResult::Create(m_process, bitmask_error);
    }
    if (!used)
      continue;
    if (++found_idx == idx) {
      // you found it!!!
      DataBufferSP full_buffer_sp(
          new DataBufferHeap(m_key_stride_padded + m_value_stride, 0));
      uint8_t *key_buffer_ptr = full_buffer_sp->GetBytes();
      uint8_t *value_buffer_ptr =
          m_value_stride ? (key_buffer_ptr + m_key_stride_padded) : nullptr;
      if (GetDataForKeyAtCell(cell_idx, key_buffer_ptr) &&
          (value_buffer_ptr == nullptr ||
           GetDataForValueAtCell(cell_idx, value_buffer_ptr))) {
        DataExtractor full_data;
        full_data.SetData(full_buffer_sp);
        StreamString name;
        name.Printf("[%zu]", idx);
        return ValueObjectConstResult::Create(
            m_process, m_element_type, ConstString(name.GetData()), full_data);
      }
    }
  }
  return null_valobj_sp;
}

bool SwiftHashedContainerStorageBufferHandler::ReadBitmaskAtIndex(Index i, 
                                                                 Status &error) {
  if (i >= m_capacity)
    return false;
  const size_t word = i / (8 * m_ptr_size);
  const size_t offset = i % (8 * m_ptr_size);
  const lldb::addr_t effective_ptr = m_bitmask_ptr + (word * m_ptr_size);
  uint64_t data = 0;

  auto cached = m_bitmask_cache.find(effective_ptr);
  if (cached != m_bitmask_cache.end()) {
    data = cached->second;
  } else {
    data = m_process->ReadUnsignedIntegerFromMemory(effective_ptr, m_ptr_size,
                                                    0, error);
    if (error.Fail())
      return false;
    m_bitmask_cache[effective_ptr] = data;
  }

  const uint64_t mask = static_cast<uint64_t>(1UL << offset);
  const uint64_t value = (data & mask);
  return (0 != value);
}

lldb::addr_t
SwiftHashedContainerStorageBufferHandler::GetLocationOfKeyAtCell(Cell i) {
  return m_keys_ptr + (i * m_key_stride);
}

lldb::addr_t
SwiftHashedContainerStorageBufferHandler::GetLocationOfValueAtCell(Cell i) {
  return m_value_stride ? m_values_ptr + (i * m_value_stride)
                        : LLDB_INVALID_ADDRESS;
}

// these are sharp tools that assume that the Cell contains valid data and the
// destination buffer
// has enough room to store the data to - use with caution
bool SwiftHashedContainerStorageBufferHandler::GetDataForKeyAtCell(
    Cell i, void *data_ptr) {
  if (!data_ptr)
    return false;

  lldb::addr_t addr = GetLocationOfKeyAtCell(i);
  Status error;
  m_process->ReadMemory(addr, data_ptr, m_key_stride, error);
  if (error.Fail())
    return false;

  return true;
}

bool SwiftHashedContainerStorageBufferHandler::GetDataForValueAtCell(
    Cell i, void *data_ptr) {
  if (!data_ptr || !m_value_stride)
    return false;

  lldb::addr_t addr = GetLocationOfValueAtCell(i);
  Status error;
  m_process->ReadMemory(addr, data_ptr, m_value_stride, error);
  if (error.Fail())
    return false;

  return true;
}

SwiftHashedContainerStorageBufferHandler::
    SwiftHashedContainerStorageBufferHandler(
        lldb::ValueObjectSP storage_sp, CompilerType key_type,
        CompilerType value_type)
    : m_storage(storage_sp.get()), m_process(nullptr),
      m_ptr_size(0), m_count(0), m_capacity(0),
      m_bitmask_ptr(LLDB_INVALID_ADDRESS), m_keys_ptr(LLDB_INVALID_ADDRESS),
      m_values_ptr(LLDB_INVALID_ADDRESS), m_element_type(),
      m_key_stride(key_type.GetByteStride()), m_value_stride(0),
      m_key_stride_padded(m_key_stride), m_bitmask_cache() {
  static ConstString g_initializedEntries("initializedEntries");
  static ConstString g_values("values");
  static ConstString g__rawValue("_rawValue");
  static ConstString g_keys("keys");
  static ConstString g_buffer("buffer");

  static ConstString g_key("key");
  static ConstString g_value("value");
  static ConstString g__value("_value");

  static ConstString g_capacity("bucketCount");
  static ConstString g_count("count");

  if (!m_storage)
    return;
  if (!key_type)
    return;

  if (value_type) {
    m_value_stride = value_type.GetByteStride();
    if (SwiftASTContext *swift_ast =
            llvm::dyn_cast_or_null<SwiftASTContext>(key_type.GetTypeSystem())) {
      std::vector<SwiftASTContext::TupleElement> tuple_elements{
          {g_key, key_type}, {g_value, value_type}};
      m_element_type = swift_ast->CreateTupleType(tuple_elements);
      m_key_stride_padded = m_element_type.GetByteStride() - m_value_stride;
    }
  } else
    m_element_type = key_type;

  if (!m_element_type)
    return;

  m_process = m_storage->GetProcessSP().get();
  if (!m_process)
    return;

  m_ptr_size = m_process->GetAddressByteSize();

  auto buffer_sp = m_storage->GetChildAtNamePath({g_buffer});
  if (buffer_sp) {
    auto buffer_ptr = buffer_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (buffer_ptr == 0 || buffer_ptr == LLDB_INVALID_ADDRESS)
      return;

    Status error;
    m_capacity =
        m_process->ReadPointerFromMemory(buffer_ptr + 2 * m_ptr_size, error);
    if (error.Fail())
      return;
    m_count =
        m_process->ReadPointerFromMemory(buffer_ptr + 3 * m_ptr_size, error);
    if (error.Fail())
      return;
  } else {
    auto capacity_sp =
        m_storage->GetChildAtNamePath({g_capacity, g__value});
    if (!capacity_sp)
      return;
    m_capacity = capacity_sp->GetValueAsUnsigned(0);
    auto count_sp = m_storage->GetChildAtNamePath({g_count, g__value});
    if (!count_sp)
      return;
    m_count = count_sp->GetValueAsUnsigned(0);
  }

  m_storage = storage_sp.get();
  m_bitmask_ptr =
      m_storage
          ->GetChildAtNamePath({g_initializedEntries, g_values, g__rawValue})
          ->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);

  if (ValueObjectSP value_child_sp =
          m_storage->GetChildAtNamePath({g_values, g__rawValue})) {
    // it is fine not to pass a value_type, but if the value child exists, then
    // you have to pass one
    if (!value_type)
      return;
    m_values_ptr = value_child_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  }
  m_keys_ptr = m_storage->GetChildAtNamePath({g_keys, g__rawValue})
                   ->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  // Make sure we can read the bitmask at the ount index.  
  // and this will keep us from trying
  // to reconstruct many bajillions of invalid children.
  // Don't bother if the native buffer handler is invalid already, however. 
  if (IsValid())
  {
    Status error;
    ReadBitmaskAtIndex(m_capacity - 1, error);
    if (error.Fail())
    {
      m_bitmask_ptr = LLDB_INVALID_ADDRESS;
    }
  }
}

bool SwiftHashedContainerStorageBufferHandler::IsValid() {
  return (m_storage != nullptr) && (m_process != nullptr) &&
         m_element_type.IsValid() && m_bitmask_ptr != LLDB_INVALID_ADDRESS &&
         m_keys_ptr != LLDB_INVALID_ADDRESS &&
         /*m_values_ptr != LLDB_INVALID_ADDRESS && you can't check values
            because some containers have only keys*/
         m_capacity >= m_count;
}

std::unique_ptr<SwiftHashedContainerBufferHandler>
SwiftHashedContainerBufferHandler::CreateBufferHandlerForNativeStorage(
    ValueObject &valobj,
    ValueObjectSP storage_sp,
    NativeCreatorFunction Native) {
  if (!storage_sp) { return nullptr; }

  CompilerType child_type(valobj.GetCompilerType());
  CompilerType key_type(child_type.GetGenericArgumentType(0));
  CompilerType value_type(child_type.GetGenericArgumentType(1));

  return std::unique_ptr<SwiftHashedContainerBufferHandler>(
    Native(storage_sp, key_type, value_type));
}

std::unique_ptr<SwiftHashedContainerBufferHandler>
SwiftHashedContainerBufferHandler::CreateBufferHandler(
    ValueObject &valobj,
    NativeCreatorFunction Native,
    SyntheticCreatorFunction Synthetic,
    ConstString mangledStorageTypeName,
    ConstString demangledStorageTypeName) {
  static ConstString g__variantStorage("_variantStorage");
  static ConstString g__variantBuffer("_variantBuffer"); // Swift 4 & 4.2
  static ConstString g__variant("_variant"); // Swift 5
  static ConstString g_native("native");
  static ConstString g_cocoa("cocoa");
  static ConstString g_nativeStorage("nativeStorage");
  static ConstString g_nativeBuffer("nativeBuffer");
  static ConstString g_buffer("buffer");
  static ConstString g_storage("storage");
  static ConstString g__storage("_storage"); // Swift 4, 5
  static ConstString g_base("base"); // Swift 5

  Status error;

  ProcessSP process_sp(valobj.GetProcessSP());
  if (!process_sp)
    return nullptr;

  ValueObjectSP valobj_sp =
      valobj.GetSP()->GetQualifiedRepresentationIfAvailable(
          lldb::eDynamicCanRunTarget, false);

  ConstString type_name_cs(valobj.GetTypeName());
  if (type_name_cs) {
    std::cerr << "CreateBufferHandler type_name: " << type_name_cs.AsCString() << std::endl;
  }
  if (type_name_cs) {
    llvm::StringRef type_name_strref(type_name_cs.GetStringRef());

    if (type_name_strref.startswith(mangledStorageTypeName.GetCString()) ||
        type_name_strref.startswith(demangledStorageTypeName.GetCString())) {
      return CreateBufferHandlerForNativeStorage(valobj, valobj_sp, Native);
    }
  }

  ValueObjectSP variant_sp(valobj_sp->GetChildMemberWithName(g__variant, true));
  if (!variant_sp)  // Swift 4
    variant_sp = valobj_sp->GetChildMemberWithName(g__variantBuffer, true);
  if (!variant_sp) // Swift 3
    variant_sp = valobj_sp->GetChildMemberWithName(g__variantStorage, true);

  if (!variant_sp) {
    std::cerr << type_name_cs.AsCString() << std::endl;
    static ConstString g__SwiftDeferredNSDictionary(
      "Swift._SwiftDeferredNSDictionary<");
    if (type_name_cs.GetStringRef().startswith(
        g__SwiftDeferredNSDictionary.GetStringRef())) {
      ValueObjectSP storage_sp( // Swift 5+
        valobj_sp->GetChildAtNamePath({g_base, g__storage}));
      if (!storage_sp) // Swift 4
        storage_sp =
          valobj_sp->GetChildAtNamePath({g_nativeBuffer, g__storage});
      return CreateBufferHandlerForNativeStorage(
        *valobj_sp, storage_sp, Native);
    }
    static ConstString g__HashableTypedNativeDictionaryStorage(
      "Swift._HashableTypedNativeDictionaryStorage");
    if (type_name_cs.GetStringRef().startswith( // Swift 4
        g__HashableTypedNativeDictionaryStorage.GetStringRef())) {
      return CreateBufferHandlerForNativeStorage(*valobj_sp, valobj_sp, Native);
    }
    return nullptr;
  }

  ConstString variant_cs(variant_sp->GetValueAsCString());

  if (!variant_cs)
    return nullptr;

  if (g_native == variant_cs) {
    ValueObjectSP storage_sp( // Swift 5+
      variant_sp->GetChildAtNamePath({g_native, g__storage}));
    if (!storage_sp) // Swift 4
      storage_sp = variant_sp->GetChildAtNamePath({g_native, g_nativeStorage});
    if (!storage_sp)
      return nullptr;
    return CreateBufferHandlerForNativeStorage(*valobj_sp, storage_sp, Native);
  }

  if (g_cocoa == variant_cs) {
    // it's an NSDictionary in disguise
    static ConstString g_object("object"); // Swift 5+
    static ConstString g_cocoaDictionary("cocoaDictionary"); // Swift 4
    ValueObjectSP child_sp(variant_sp->GetChildAtNamePath({g_cocoa, g_object}));
    if (!child_sp)
      child_sp = variant_sp->GetChildAtNamePath({g_cocoa, g_cocoaDictionary});
    if (!child_sp)
      return nullptr;
    // child_sp is the _NSDictionary/_NSSet reference.
    ValueObjectSP ref_sp = child_sp->GetChildAtIndex(0, true); // instance
    if (!ref_sp)
      return nullptr;
    uint64_t cocoa_ptr = ref_sp->GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (cocoa_ptr == LLDB_INVALID_ADDRESS)
      return nullptr;
    // FIXME: for some reason I need to zero out the MSB; figure out why
    cocoa_ptr &= 0x00FFFFFFFFFFFFFF;
    CompilerType id =
        process_sp->GetTarget().GetScratchClangASTContext()->GetBasicType(
            lldb::eBasicTypeObjCID);
    InferiorSizedWord isw(cocoa_ptr, *process_sp);
    ValueObjectSP cocoarr_sp = ValueObject::CreateValueObjectFromData(
        "cocoarr", isw.GetAsData(process_sp->GetByteOrder()),
        valobj.GetExecutionContextRef(), id);
    if (!cocoarr_sp)
      return nullptr;
    auto objc_runtime = process_sp->GetObjCLanguageRuntime();
    auto descriptor_sp = objc_runtime->GetClassDescriptor(*cocoarr_sp);
    if (!descriptor_sp)
      return nullptr;
    ConstString classname(descriptor_sp->GetClassName());
    auto handler = std::unique_ptr<SwiftHashedContainerBufferHandler>(
      Synthetic(cocoarr_sp));
    if (handler && handler->IsValid())
      return handler;
    return nullptr;
  }
  return nullptr;
}

lldb_private::formatters::swift::HashedContainerSyntheticFrontEnd::
    HashedContainerSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp.get()), m_buffer() {}

size_t lldb_private::formatters::swift::HashedContainerSyntheticFrontEnd::
    CalculateNumChildren() {
  return m_buffer ? m_buffer->GetCount() : 0;
}

lldb::ValueObjectSP lldb_private::formatters::swift::
    HashedContainerSyntheticFrontEnd::GetChildAtIndex(size_t idx) {
  if (!m_buffer)
    return ValueObjectSP();

  lldb::ValueObjectSP child_sp = m_buffer->GetElementAtIndex(idx);

  if (child_sp)
    child_sp->SetSyntheticChildrenGenerated(true);

  return child_sp;
}

bool lldb_private::formatters::swift::HashedContainerSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::swift::HashedContainerSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  if (!m_buffer)
    return UINT32_MAX;
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}
