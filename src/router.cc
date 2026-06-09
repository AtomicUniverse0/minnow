#include "router.hh"

#include <cassert>
#include <iostream>
// #include <limits>
#include <ranges>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

       assert(prefix_length <= 32);
  routing_table_[prefix_length].emplace_back(route_prefix, prefix_length, next_hop, interface_num );
}

void Router::route() {
  for( auto& network_interface : interfaces_){
    auto datagram = network_interface.maybe_receive();
    while( datagram.has_value() ){
      route_datagram( datagram.value() );
      datagram = network_interface.maybe_receive();
    }
  }
}

void Router::route_datagram( InternetDatagram& datagram ){
  for(auto& rtable_iter : std::views::reverse(routing_table_)){
    auto& vec = rtable_iter.second;
    auto target = std::find_if(vec.begin(), vec.end(), 
[&datagram](const auto& entry){
        uint8_t prefix_len = entry.prefix_length_;
        if(prefix_len == 0){
          return true;
        }

        uint32_t entry_prefix = entry.route_prefix_;
        entry_prefix = entry_prefix >> (32U - prefix_len);
        uint32_t datagram_prefix = datagram.header.dst >> (32U - prefix_len);

        return entry_prefix == datagram_prefix;
      }
    );

    if(target != vec.end()){
      if(datagram.header.ttl <= 1){
        return;
      }
      datagram.header.ttl--;
      datagram.header.compute_checksum();

      Address next_hop = target->next_hop_.has_value() ? target->next_hop_.value() : Address::from_ipv4_numeric(datagram.header.dst);
      interface(  target->interface_num_).send_datagram(datagram, next_hop);
      return;
    }
  }
}