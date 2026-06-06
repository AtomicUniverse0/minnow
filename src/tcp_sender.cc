#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) ), 
    zero_point_(isn_),
    initial_RTO_ms_( initial_RTO_ms ),
    sequence_numbers_in_flight_( 0 ),
    resend_timer_( initial_RTO_ms )
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return sequence_numbers_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return resend_timer_.consecutive_retransmissions();
}

// 这里进行真正的数据包发送
optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if( !maybe_send_.has_value() ){
    return nullopt;
  }

  optional<TCPSenderMessage> res = maybe_send_;
  maybe_send_ = nullopt;

  return res;
}

// 何时发送FIN?
// 如果发出了有长度的数据，就需要启动定时器
/*
  梳理一下发送数据的大体逻辑:
    如果未发送SYN，需要发送SYN，当作window size 为 1  
    如果outbound_stream里没数据，则返回
    如果window size = 0，则当成1使用
    如果window size > 0 ，则这是常规情况

    要求每次push，如果有数据可发送，必须先调用 maybe_send()才行
*/
void TCPSender::push( Reader& outbound_stream )
{
  /*
    判断是否有必要发包， 优先级：SYN > 没有数据可发
  */
  if(outbound_stream.bytes_buffered() == 0 and syn_sent_){
    return;
  }

  // 执行到这里，说明有包可发送，先要求 maybe_send_ 为空
  assert(!maybe_send_.has_value());
  // 构造数据包，存到maybe_sent_里
  if(!syn_sent_){
    assert(window_size_ == 0);
    maybe_send_ = TCPSenderMessage{isn_, true, Buffer{}, false};
    syn_sent_ = true;
  }else{
    uint16_t payload_size = std::min(outbound_stream.bytes_buffered(), TCPConfig::MAX_PAYLOAD_SIZE);
    payload_size = std::min(payload_size, window_size_);
    payload_size = payload_size == 0 ? 1 : payload_size; // window size为0时当成1使用
    auto sv = outbound_stream.peek().substr(0, payload_size);
    maybe_send_ = TCPSenderMessage{isn_,
                                   false, 
                                   Buffer{string(sv.data(), sv.size())},
                                    false};
    
    outbound_stream.pop(payload_size);
    window_size_ -= payload_size;
  }

  assert(maybe_send_.has_value());

  if( !resend_timer_.started() ){
    resend_timer_.start();
  }
  sending_window_[isn_.unwrap(zero_point_, checkpoint_)] = maybe_send_.value(); 
  isn_ = isn_ + maybe_send_.value().sequence_length();
  sequence_numbers_in_flight_ += maybe_send_.value().sequence_length();
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  // Your code here.
  assert(syn_sent_);
  return {isn_, false, Buffer{}, false};
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  window_size_ = msg.window_size;

  if( not msg.ackno.has_value() ){
    return;
  }

  assert(not sending_window_.empty());

  uint64_t absolute_ackno = msg.ackno->unwrap(zero_point_, checkpoint_);
  if(absolute_ackno <= sending_window_.begin()->first){
    return; // 重复的？
  }

  uint64_t n = sending_window_.begin()->first + sending_window_.begin()->second.sequence_length();
  while(not sending_window_.empty() and absolute_ackno >= n){
    sequence_numbers_in_flight_ -= sending_window_.begin()->second.sequence_length();
    checkpoint_ = n;
    sending_window_.erase(sending_window_.begin());
    if( not sending_window_.empty() ){
      n = sending_window_.begin()->first + sending_window_.begin()->second.sequence_length();
    }
  }

  if(sending_window_.empty() and resend_timer_.started()){
    resend_timer_.stop();
  }

  if(sending_window_.empty() and fin_available_){
    assert(not maybe_send_.has_value());
    maybe_send_ = TCPSenderMessage{isn_, false, Buffer{}, true};
  }

}

void TCPSender::tick( const size_t ms_since_last_tick )
{
  if( !resend_timer_.started() ) {
    return;
  }

  resend_timer_.incr_ticks(ms_since_last_tick);

  if(resend_timer_.timeout()){
    assert(!sending_window_.empty());
    assert(!maybe_send_.has_value());
    resend_timer_.reset();
    maybe_send_ = sending_window_.begin()->second;
  }
}
