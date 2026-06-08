#include "network_interface.hh"

#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <cassert>

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address ), ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

EthernetFrame NetworkInterface::build_arp_request(const Address& dst){
  EthernetFrame frame;
  EthernetHeader& header = frame.header;
  header.dst = ETHERNET_BROADCAST;
  header.src = ethernet_address_;
  header.type = EthernetHeader::TYPE_ARP;

  Serializer serializer;
  ARPMessage arp_message;
  arp_message.opcode = ARPMessage::OPCODE_REQUEST;
  arp_message.sender_ip_address = ip_address_.ipv4_numeric();
  arp_message.sender_ethernet_address = ethernet_address_;
  arp_message.target_ip_address = dst.ipv4_numeric();
  // arp_message.target_ethernet_address;
  arp_message.serialize(serializer);
  assert(arp_message.supported());

  frame.payload = serializer.output();

  return frame;
}

EthernetFrame NetworkInterface::build_arp_reply( const ARPMessage& req_arp_message, const EthernetAddress& next_eth){
  EthernetFrame frame;
  EthernetHeader& header = frame.header;
  header.dst = next_eth;
  header.src = ethernet_address_;
  header.type = EthernetHeader::TYPE_ARP;

  Serializer serializer;
  ARPMessage rsp_arp_message;
  rsp_arp_message.opcode = ARPMessage::OPCODE_REPLY;
  rsp_arp_message.sender_ip_address = ip_address_.ipv4_numeric();
  rsp_arp_message.sender_ethernet_address = ethernet_address_;
  rsp_arp_message.target_ip_address = req_arp_message.sender_ip_address;
  rsp_arp_message.target_ethernet_address = req_arp_message.sender_ethernet_address;
  rsp_arp_message.serialize(serializer);
  assert(rsp_arp_message.supported());

  frame.payload = serializer.output();

  return frame;
}

EthernetFrame NetworkInterface::build_ethernet_frame(const InternetDatagram& dgram, const EthernetAddress& dst_eth_addr){
  EthernetFrame frame;
  EthernetHeader& header = frame.header;
  header.dst = dst_eth_addr;
  header.src = ethernet_address_;
  header.type = EthernetHeader::TYPE_IPv4;

  Serializer serializer;
  dgram.serialize( serializer );
  frame.payload = serializer.output();

  return frame;
}


void NetworkInterface::recv_handle_arp_message(const EthernetFrame& frame){
  assert(frame.header.type == EthernetHeader::TYPE_ARP);  
  
  // 将IP地址和mac地址的对应关系解析到ARP表里
  Parser parser(frame.payload);
  ARPMessage arp_message;
  arp_message.parse(parser);

  uint32_t sender_ip = arp_message.sender_ip_address;
  arp_table_[sender_ip] = std::make_pair(arp_message.sender_ethernet_address, 0);
  Address next_hop = Address::from_ipv4_numeric(sender_ip);

  // 如果有数据包在等待这个IP的ARP解析，那么现在就可以发出去了
  if(waiting_datagrams_.find(sender_ip) != waiting_datagrams_.end()) { 
    const auto& datagrams = waiting_datagrams_[sender_ip];
    for(const auto& ddgram : datagrams) {
      send_datagram(ddgram, next_hop);
    }

    waiting_datagrams_.erase(sender_ip);
    arp_request_ticks_.erase(sender_ip);
  }

  if(arp_message.target_ip_address != ip_address_.ipv4_numeric()) {
    return;
  }

  if(arp_message.opcode == ARPMessage::OPCODE_REQUEST){
    EthernetFrame reply_frame = build_arp_reply( arp_message, frame.header.src );
    maybe_sent_queue_.push(reply_frame);
  }else{
    assert(arp_message.opcode == ARPMessage::OPCODE_REPLY);
  }
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
// 如果已经发过ARP了，那么5秒之内别再发第二次
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // assert(dgram.header.dst == next_hop.ipv4_numeric()); // 这个断言是错的，ip头中的目的地址是最终目的地址，而不是下一跳的地址，所以不应该断言它们相等
  // assert(dgram.header.src == ip_address_.ipv4_numeric()); // 这个断言也是错的，ip头中的源地址是发送主机的地址，而不是接口的地址，所以也不应该断言它们相等

  uint32_t ipv4_numeric = next_hop.ipv4_numeric();

  if(arp_table_.find(ipv4_numeric) == arp_table_.end()) { // 找不到下一跳的MAC地址，发ARP请求
    // 这个数据包进入等待ARP解析的队列
    waiting_datagrams_[ipv4_numeric].push_back(dgram);

    // 5秒之内别再发第二次ARP请求
    if(arp_request_ticks_.find(ipv4_numeric) != arp_request_ticks_.end()
       and arp_request_ticks_[ipv4_numeric] < ARP_RETRY_INTERVAL) {
        return;
    }
    // 超过了5秒，或者没发过，所以构造一个ARP请求并发送
    EthernetFrame frame = build_arp_request(next_hop);
    maybe_sent_queue_.push(frame);
    arp_request_ticks_[ipv4_numeric] = 0; 
    return;
  }

  EthernetAddress dst_eth_addr = arp_table_[ipv4_numeric].first;
  EthernetFrame frame = build_ethernet_frame(dgram, dst_eth_addr);
  maybe_sent_queue_.push(frame);
}

// frame: the incoming Ethernet frame
/*
  根据数据报里的
*/
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  if(frame.header.dst != ethernet_address_ and frame.header.dst != ETHERNET_BROADCAST) {
    return nullopt;
  }

  if(frame.header.type == EthernetHeader::TYPE_ARP){
    recv_handle_arp_message(frame);
    return nullopt;
  }

  // 如果是一个IP数据包，那么有必要存到ARP表里吗？先不管了
  assert(frame.header.type == EthernetHeader::TYPE_IPv4);
  InternetDatagram dgram;
  Parser parser(frame.payload);
  dgram.parse(parser);

  return dgram;
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  // 删除过期的arp表项
  std::for_each( arp_table_.begin(), arp_table_.end(), [ms_since_last_tick]( auto& x ) {
      x.second.second += ms_since_last_tick;
  });
  std::erase_if( arp_table_, []( auto& x ) {
      return x.second.second >= ARP_TIMEOUT;
  });

  // 更新等待ARP解析的数据报的等待时间
  std::for_each( arp_request_ticks_.begin(), arp_request_ticks_.end(), [ms_since_last_tick]( auto& x ) {
      x.second += ms_since_last_tick;
  });
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if(maybe_sent_queue_.empty()) {
    return nullopt;
  }

  auto result = maybe_sent_queue_.front();
  maybe_sent_queue_.pop();
  return result;
}
