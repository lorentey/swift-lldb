//===-- AdditionalFormatter.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AdditionalFormatters_h_
#define liblldb_AdditionalFormatters_h_

#include <memory>
#include <vector>

#include "lldb/Utility/ConstString.h"

namespace lldb_private {
namespace formatters {

class FormatterMatcher {
public:
  virtual ~FormatterMatcher() = default;
  virtual bool Match(ConstString className) = 0;

  typedef std::unique_ptr<FormatterMatcher> UP;
};

class PrefixFormatterMatcher : public FormatterMatcher {
public:
  PrefixFormatterMatcher(ConstString prefix): m_prefix(prefix) {}
  virtual ~PrefixFormatterMatcher() = default;
  virtual bool Match(ConstString className) override {
    return className.GetStringRef().startswith(m_prefix.GetStringRef());
}

private:
  ConstString m_prefix;
};

class FullFormatterMatcher : public FormatterMatcher {
public:
  FullFormatterMatcher(ConstString name): m_name(name) {}
  virtual ~FullFormatterMatcher() = default;
  virtual bool Match(ConstString className) override {
    return (className == m_name);
  }

private:
  ConstString m_name;
};


template <typename Formatter>
class AdditionalFormatters {
private:
  class Entry {
  public:
    Entry(ConstString language,
          FormatterMatcher::UP &&matcher,
          Formatter formatter)
      : m_language(language),
        m_matcher(std::move(matcher)),
        m_formatter(formatter) {}
    Formatter GetFormatter() const { return m_formatter; }
    ConstString GetLanguage() const { return m_language; }
    bool Match(ConstString className) const {
      return m_matcher->Match(className);
    }
  private:
    ConstString m_language;
    FormatterMatcher::UP m_matcher;
    Formatter m_formatter;
  };

public:
  AdditionalFormatters &AddPrefix(ConstString language,
                                  ConstString prefix,
                                  Formatter formatter) {
    m_entries.emplace_back(
      language,
      llvm::make_unique<PrefixFormatterMatcher>(prefix),
      formatter);
    return *this;
  }
  AdditionalFormatters &AddFull(ConstString language,
                                ConstString name,
                                Formatter formatter) {
    m_entries.emplace_back(
      language,
      llvm::make_unique<FullFormatterMatcher>(name),
      formatter);
    return *this;
  }
  Formatter Match(ConstString className, Formatter fallback) const {
    for (auto &candidate : m_entries) {
      if (candidate.Match(className))
        return candidate.GetFormatter();
    }
    return fallback;
  }
  void RemoveLanguage(ConstString language) {
    m_entries.erase(
       std::remove_if(
         m_entries.begin(), m_entries.end(),
         [language](const Entry &entry) {
           return entry.GetLanguage() == language;
         }),
       m_entries.end());
  }

private:
  std::vector<Entry> m_entries;
};

}
}

#endif // liblldb_AdditionalFormatters_h_
