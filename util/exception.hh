#pragma once

#include <cxxabi.h>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

class tagged_error : public std::system_error
{
private:
  std::string attempt_and_error_;
  int error_code_;

public:
  tagged_error( const std::error_category& category, const std::string_view s_attempt, const int error_code )
    : system_error( error_code, category )
    , attempt_and_error_( std::string( s_attempt ) + ": " + std::system_error::what() )
    , error_code_( error_code )
  {}

  const char* what() const noexcept override { return attempt_and_error_.c_str(); }

  int error_code() const { return error_code_; }
};

class unix_error : public tagged_error
{
public:
  explicit unix_error( const std::string_view s_attempt, const int s_errno = errno )
    : tagged_error( std::system_category(), s_attempt, s_errno )
  {}
};

inline int CheckSystemCall( const std::string_view s_attempt, const int return_value )
{
  if ( return_value >= 0 ) {
    return return_value;
  }

  throw unix_error { s_attempt };
}

template<typename T>
inline T* notnull( const std::string_view context, T* const x )
{
  return x ? x : throw std::runtime_error( std::string( context ) + ": returned null pointer" );
}
/**
  优雅的写法，算是学到了，通过unique_ptr的自定义删除器，来自动管理cxa_demangle函数返回的内存，避免了内存泄漏的风险。
*/
// demangle 是将编程中编译器生成的加密符号，还原为人类可读名称的过程
inline std::string demangle( const char* name )
{
  //status是 一个输出参数。如果还原成功，它会被置为 0；如果失败（比如传入的不是一个合法的 C++ 名字），会变成负数
  int status {};
  // RAII的范例
  // __cxa_demangle 这个底层 C 函数成功时，会内部调用 malloc 在堆上分配一块内存来存放还原后的字符串。调用者（也就是你）必须手动用 free() 释放它
  // 普通的 std::unique_ptr<T> 在销毁时默认调用 delete。但因为底层函数是用 malloc 分配的，必须用 free 释放
  const std::unique_ptr<char, decltype( &free )> res { abi::__cxa_demangle( name, nullptr, nullptr, &status ),
                                                       free };
  if ( status ) {
    throw std::runtime_error( "cxa_demangle" );
  }
  return res.get(); // 通过 char* 隐式地转换为 std::string
}
