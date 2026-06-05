#include "reassembler.hh"
#include <cassert>

using namespace std;

void Reassembler::consume_data(std::string&& data, uint64_t index, Writer& output){
  assert(is_data_consumable(data, index, output));

  crop_data_right(data, index, output);
  crop_data_left(data, index);

  assert(data.size() <= output.available_capacity() 
          and !data.empty());

  expected_index_ += data.size(); 
  output.push(std::move(data));
}

// 收到了乱序的data，存储到pending_substrings_中
void Reassembler::pend(std::string&& data, uint64_t index, Writer& output){
  assert(is_data_outoforder(index, output));

  crop_data_right(data, index, output);

  vector<uint64_t> left_overlapping; // 与data左侧重叠的pending_substrings_的index
  vector<uint64_t> contained; // 被data包含的pending_substrings_的index
  vector<uint64_t> right_overlapping; // 与data右侧重叠的pending_substrings_的index
  vector<uint64_t> is_substring; // data是pending_substrings_的子串的pending_substrings_的index

  // 查询 pending_substrings_ 中与data有重叠的部分
  for(const auto& [pending_index, pending_data] : pending_substrings_){
    if(pending_index < index and pending_index + pending_data.size() > index and pending_index + pending_data.size() <= index + data.size()){
      left_overlapping.push_back(pending_index);
    }else if(pending_index >= index and pending_index + pending_data.size() <= index + data.size()){
      contained.push_back(pending_index);
    }else if(pending_index >= index and pending_index < index + data.size() and pending_index + pending_data.size() > index + data.size()){
      right_overlapping.push_back(pending_index);
    }else if(pending_index <= index and pending_index + pending_data.size() >= index + data.size()){
      is_substring.push_back(pending_index);
    }
  }
  assert(left_overlapping.size() <= 1 and right_overlapping.size() <= 1 and is_substring.size() <= 1);

  for(auto idx : left_overlapping){
    // data与pending_substrings_[idx]左侧重叠，那么需要将其与data合并，并更新index
    uint64_t len = index - idx;
    data.insert(0, pending_substrings_[idx], 0, len);
    index = idx;

    bytes_pending_ -= pending_substrings_[idx].size();
    pending_substrings_.erase(idx);
  }

  for(auto idx : contained){
    bytes_pending_ -= pending_substrings_[idx].size();
    pending_substrings_.erase(idx);
  }

  for(auto idx : right_overlapping){
    uint64_t len = idx + pending_substrings_[idx].size() - (index + data.size()); // 个字节与data右侧重叠，那么需要将其与data合并
    data.append(pending_substrings_[idx], pending_substrings_[idx].size() - len, len);

    bytes_pending_ -= pending_substrings_[idx].size();
    pending_substrings_.erase(idx);
  }

  for(auto idx : is_substring){
    assert(left_overlapping.empty() and right_overlapping.empty() and contained.empty());

    auto dest_start = pending_substrings_[idx].begin() + static_cast<std::string::difference_type>(index - idx);
    auto dest_end = dest_start + static_cast<std::string::difference_type>(data.size());
    std::copy(dest_start, dest_end, data.begin());

    return; // 比较特殊的情况，data是pending_substrings_[idx]的子串，那么直接将data覆盖到pending_substrings_[idx]上就行了
  }
  
  bytes_pending_ += data.size();
  pending_substrings_[index] = std::move(data);
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output)
{
  // Your code here.
  if(is_data_outoforder(first_index, output)){
    // 将data存储到里面
    pend(std::move(data), first_index, output);
  }else if (is_data_consumable(data, first_index, output)){
      
    consume_data(std::move(data), first_index, output);
    // data[effective_start_index:] 是 data 中有效的部分
    while( not pending_substrings_.empty() and 
           pending_substrings_.begin()->first <= expected_index_){

      auto data_size = pending_substrings_.begin()->second.size(); 
      if( pending_substrings_.begin()->first + data_size > expected_index_){
        consume_data(std::move(pending_substrings_.begin()->second), pending_substrings_.begin()->first, output);
      }
      pending_substrings_.erase(pending_substrings_.begin());
      bytes_pending_ -= data_size;
    }
  }

  if(is_last_substring){
    stop_ = true;
  }

  if(stop_  and bytes_pending_ == 0){
    output.close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  // Your code here.
  return bytes_pending_;
}