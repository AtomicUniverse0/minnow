#include "tcp_sender.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"

#include <optional>
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
  if(pending_segments_.empty() ){
    return nullopt;
  }

  optional<TCPSenderMessage> res = pending_segments_.front();
  pending_segments_.pop_front();

  return res;
}

bool TCPSender::should_send_fin(const Reader& outbound_stream) const {
  if(not outbound_stream.is_finished()){
    return false;
  }

  if(fin_sent_){
    return false;
  }

  bool is_syn_packet = false; 
  if( not pending_segments_.empty()){
    is_syn_packet = pending_segments_.front().SYN;
  }

  return (window_size_ >= 1 or is_syn_packet);
}

bool TCPSender::nothing_to_send(const Reader& outbound_stream) const {
  if(outbound_stream.bytes_buffered() > 0){
    return false;
  }
  if(not syn_sent_){
    return false;
  }

  return( fin_sent_ or not outbound_stream.is_finished());
}

void TCPSender::save_to_sending_window(const TCPSenderMessage& packet, bool window_exhausted){
  uint64_t key = isn_.unwrap(zero_point_, checkpoint_);
  sending_window_.emplace(key, SendingWindowEntry(packet, window_exhausted));
  isn_ = isn_ + packet.sequence_length();
  sequence_numbers_in_flight_ += packet.sequence_length();
}

// 如果在第一次尝试发包时，发现window耗尽了，就发送一个单字符的
std::optional<TCPSenderMessage> TCPSender::build_packet(Reader& outbound_stream){
  std::optional<TCPSenderMessage> packet;

  // 边界情况，单拿出来写，有代码冗余，但可读性更强
  if(window_exhausted_){
    assert (syn_sent_ and window_size_ == 0);

    if(outbound_stream.bytes_buffered() > 0){
      auto sv = outbound_stream.peek().substr(0, 1);
      packet = TCPSenderMessage{isn_, false, Buffer{string(sv.data(), sv.size())}, false};
      
      outbound_stream.pop(1);

    }else if(outbound_stream.is_finished() and not fin_sent_){
      packet = TCPSenderMessage{isn_, false, Buffer{}, true};
      fin_sent_ = true;
    }

    return packet;
  }

  if(!syn_sent_){
    assert(pending_segments_.empty());
    // assert(window_size_ == 0); 真逆天，有些测试就是在syn包发出去之前就把window size设置成非0的
    syn_sent_ = true;
    packet = TCPSenderMessage{isn_, true, Buffer{}, false};
  }else if(outbound_stream.bytes_buffered() > 0 and window_size_ > 0){
    uint16_t payload_size = std::min(outbound_stream.bytes_buffered(), TCPConfig::MAX_PAYLOAD_SIZE);
    payload_size = std::min(payload_size, window_size_);
    assert(payload_size > 0);

    auto sv = outbound_stream.peek().substr(0, payload_size);
    packet = TCPSenderMessage{isn_,false,  Buffer{string(sv.data(), sv.size())},false};
    outbound_stream.pop(payload_size);
    window_size_ -= payload_size;
  }

  if( should_send_fin(outbound_stream) ){
    if(not packet.has_value()){
      packet = TCPSenderMessage{isn_, false, Buffer{}, false};
    }
    packet->FIN = true;
    window_size_ -= 1;
    fin_sent_ = true;
  }

  return packet;
}
/**
  每当outbound_stream的状态被改变了时，就调用这个函数
  状态包括有数据没读完，或者outbound_stream被close了
*/
void TCPSender::push( Reader& outbound_stream )
{
  /*
    判断是否有必要发包， 优先级：SYN > 没有数据可发
  */
  while ( not nothing_to_send(outbound_stream) ){
    auto  packet = build_packet(outbound_stream);
    if(not packet.has_value()){
      return;
    }

    if( !resend_timer_.started() ){
      resend_timer_.start();
    }

    save_to_sending_window(packet.value(), window_exhausted_);
    pending_segments_.push_back(packet.value()); 

    if(window_exhausted_){ 
      window_exhausted_ = false;
      break;
    }
    if(packet->SYN){
      break;
    }
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  // Your code here.
  assert(syn_sent_);
  return {isn_, false, Buffer{}, false};
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if( not msg.ackno.has_value() ){
    window_exhausted_ = (msg.window_size == 0);
    window_size_ = msg.window_size; // what can i say?
    return;
  }

  uint64_t ackno = msg.ackno->unwrap(zero_point_, checkpoint_);
  uint64_t isnno = isn_.unwrap(zero_point_, checkpoint_);
  if(ackno > isnno){
    return; // 不可能的ackno
  }

  if(sending_window_.empty()){
    return; // 看起来是个无效的ack
  }
  window_exhausted_ = (msg.window_size == 0);

  uint64_t left_bound = sending_window_.begin()->first;
  // 处理重复的
  if(ackno < left_bound){
    return; 
  }
  if(ackno == left_bound){
    window_size_ = std::max(window_size_, msg.window_size);
    return;
  }

  // 两种情况，第一，sending_window.begin()被完全ack了，第二，部分ack了
  uint64_t right_bound = sending_window_.begin()->first + sending_window_.begin()->second.segment().sequence_length();
  while( (not sending_window_.empty()) and ackno >= right_bound){
    sequence_numbers_in_flight_ -= sending_window_.begin()->second.segment().sequence_length();
    checkpoint_ = right_bound;
    sending_window_.erase(sending_window_.begin());
    if( not sending_window_.empty() ){
      left_bound = sending_window_.begin()->first;
      right_bound = sending_window_.begin()->first + sending_window_.begin()->second.segment().sequence_length();
    }
  }
  resend_timer_.stop();
  resend_timer_.start();

  // 更新window_size 
  if(sending_window_.empty() || ackno <= left_bound){
    // 被完全ack了，此时的window_size 可以放心更新
    window_size_ = msg.window_size;
  }else{ // 部分更新
    assert(ackno > left_bound and ackno < right_bound);
    uint16_t unacked_bytes = right_bound - ackno;
    // current window size = old_window_size - packet length
    // current_window_size + window size - (pakcet length - acked length)
    window_size_ += msg.window_size - unacked_bytes;
  }

  if(sending_window_.empty() and resend_timer_.started()){
    resend_timer_.stop();
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
    if( sending_window_.begin()->second.window_exhausted() ){
      // 边界情况，此时不增加重传的RTO
      resend_timer_.stop();
      resend_timer_.start();
    }else{
      resend_timer_.reset();
    }
    pending_segments_.push_back( sending_window_.begin()->second.segment() );
  }
}
