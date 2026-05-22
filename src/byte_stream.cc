#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) 
  : capacity_( capacity ), 
    bytes_pushed_( 0 ), 
    bytes_popped_( 0 ), 
    head_( 0 ), 
    tail_( 0 ), 
    closed_( false ), 
    has_error_(false),
    buffer_( capacity + 1 )
  {}

void Writer::push( string data )
{
  // Your code here.
  uint64_t available_capacity = this->available_capacity();
  uint64_t to_write= min( static_cast<uint64_t>( data.size() ), available_capacity );

  for ( uint64_t i = 0; i < to_write; ++i ) {
    buffer_[tail_] = data[i];
    tail_ = ( tail_ + 1 ) % buffer_.size();
  }

  bytes_pushed_ += to_write;
}

void Writer::close()
{
  // Your code here.
  closed_ = true;
}

void Writer::set_error()
{
  // Your code here.
  has_error_ = true;
}

bool Writer::is_closed() const
{
  // Your code here.
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  // Your code here.
  uint64_t used_capacity = tail_ >= head_ ? tail_ - head_ : capacity_ + 1 - head_ + tail_;
  return capacity_ - used_capacity;
}

uint64_t Writer::bytes_pushed() const
{
  // Your code here.
  return bytes_pushed_;
}

/**
  由于我实现的限制，不能一口气peek多个字节
*/
string_view Reader::peek() const
{
  // Your code here.
  uint64_t peek_tail = tail_ >= head_ ? tail_ : buffer_.size(); // tail_ 本身不指向数据
  return {&buffer_[head_], peek_tail - head_}; // 如果为长度为0，会怎样？
}

bool Reader::is_finished() const
{
  // Your code here.
  return closed_ && bytes_buffered() == 0;
}

bool Reader::has_error() const
{
  // Your code here.
  return has_error_;
}

void Reader::pop( uint64_t len )
{
  // Your code here.
  uint64_t pop_len = min(len, bytes_buffered());
  head_ = (head_ + pop_len) % buffer_.size();
  bytes_popped_ += pop_len;
}

uint64_t Reader::bytes_buffered() const
{
  // Your code here.
  return tail_ >= head_ ? tail_ - head_ : capacity_ + 1 - head_ + tail_;
}

uint64_t Reader::bytes_popped() const
{
  // Your code here.
  return bytes_popped_;
}