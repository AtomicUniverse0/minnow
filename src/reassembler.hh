#pragma once

#include "byte_stream.hh"

#include <map>
#include <string>


class Reassembler
{
public:
  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
   Reassembler() = default;

  void insert( uint64_t first_index, std::string data, bool is_last_substring, Writer& output );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;
private:

  uint64_t bytes_pending_ = 0;
  uint64_t expected_index_ = 0;
  
  std::map<uint64_t, std::string> pending_substrings_{}; // 感觉性能不会特别好。。。
  bool stop_ = false;

  /* 
    将一个数据写入到output中，并更新expected_index_
    调用方需要保证，传给consume的data一定是可消费的
  */
  void consume_data(std::string&& data, uint64_t index, Writer& output);

  void pend(std::string&& data, uint64_t index, Writer& output);

  void crop_data_right(std::string& data, uint64_t index, Writer& output) const{
    uint64_t effective_data_size = std::min(data.size(), expected_index_ + output.available_capacity() - index);
    data.resize(effective_data_size);
  }

  void crop_data_left(std::string& data, uint64_t index) const{
    uint64_t effective_start_index = expected_index_ - index;
    data.erase(0, effective_start_index);
  }

  bool is_data_outoforder(uint64_t index, Writer& output) const {
    return index > expected_index_ and index < expected_index_ + output.available_capacity();
  }

  bool is_data_consumable(std::string& data, uint64_t index, Writer& output) const{
    return index <= expected_index_ 
            and index + data.size() > expected_index_ 
            and output.available_capacity() > 0;
  }

};