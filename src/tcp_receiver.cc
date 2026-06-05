#include "tcp_receiver.hh"

#include <cassert>

using namespace std;

// 假设首次收到的消息必定是SYN
void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  // Your code here.
  if(message.SYN){
    if(received_syn_){
      return;
    }
    received_syn_ = true;
    zero_point_ = message.seqno + 1;
    message.seqno = zero_point_; // 考虑到如果SYN报文承载了数据
  }

  if( !received_syn_){
    return;
  }

  uint64_t first_index = message.seqno.unwrap(zero_point_, checkpoints_);
  if(message.payload.size() > 0){
    reassembler.insert(first_index, message.payload,false, inbound_stream);
  }
  
  if(message.FIN){
    reassembler.insert(first_index, {}, true, inbound_stream);
  }

  checkpoints_ = reassembler.checkpoint();
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  // Your code here.
  TCPReceiverMessage message;

  uint64_t window_size = inbound_stream.available_capacity();
  if(window_size > UINT16_MAX){
    window_size = UINT16_MAX;
  }
  message.window_size = static_cast<uint16_t>(window_size);

  if( received_syn_){
    message.ackno = Wrap32::wrap(checkpoints_, zero_point_);
  }

  return message;
}
