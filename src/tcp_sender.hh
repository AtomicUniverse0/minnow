#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cassert>
#include <map>

// 用来实现超时重传的一个计时器
class ResendTimer{
private:
  bool started_ = false;
  uint64_t initial_RTO_ms_;
  uint64_t current_RTO_ms_;
  uint64_t ticks_ = 0;
  uint64_t consecutive_retransmissions_ = 0;
public:
  explicit ResendTimer( uint64_t initial_RTO_ms ) : initial_RTO_ms_( initial_RTO_ms ), current_RTO_ms_( initial_RTO_ms ) {}

  bool started() const { return started_; }

  uint64_t consecutive_retransmissions() const { return consecutive_retransmissions_; }

  void start() {
    assert(!started());
    started_ = true;
    current_RTO_ms_ = initial_RTO_ms_;
    consecutive_retransmissions_ = 0;
    ticks_ = 0;
  }

  void incr_ticks( uint64_t ms_since_last_tick ) {
    assert(started());
    ticks_ += ms_since_last_tick;
  }

  bool timeout() const {
    assert(started());
    return ticks_ >= current_RTO_ms_;
  }

  // 当发生超时时，才使用这个
  void reset(){
    assert(started());
    ++consecutive_retransmissions_;
    ticks_ = 0;
    current_RTO_ms_ = current_RTO_ms_ << 1;
  }
  
  void stop(){
    assert(started());
    started_ = false;
  }
};

class TCPSender
{
private:
  Wrap32 isn_;
  Wrap32 zero_point_;
  uint64_t checkpoint_ = 0;
  uint64_t initial_RTO_ms_;

  bool syn_sent_ = false;
  bool fin_available_ = false;
  uint64_t sequence_numbers_in_flight_ ;
  uint16_t window_size_ = 0; // 由TCPReceiver通过ACK告知的窗口大小，初始为0
  std::map<uint64_t, TCPSenderMessage> sending_window_; // {key是绝对序号，value是对应的segment}
  std::optional<TCPSenderMessage> maybe_send_;

  ResendTimer resend_timer_;

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
};
