#pragma once

#include "buffer.hh"
#include "ethernet_header.hh"
#include "parser.hh"

#include <vector>

struct EthernetFrame
{
  EthernetHeader header {};
  std::vector<Buffer> payload {}; // 神奇，为何是vector<BUffer>，难道纯Buffer不好吗

  void parse( Parser& parser )
  {
    header.parse( parser );
    parser.all_remaining( payload );
  }

  void serialize( Serializer& serializer ) const
  {
    header.serialize( serializer );
    serializer.buffer( payload );
  }
};
