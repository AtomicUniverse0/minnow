#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  const uint64_t two_pow_32 = static_cast<uint64_t>( 1 ) << 32;
  uint64_t mod  = checkpoint % two_pow_32;
  uint64_t base = checkpoint - mod;

  uint64_t diff = zero_point.raw_value_ <= raw_value_ ? 
                            raw_value_ - zero_point.raw_value_ 
                              : two_pow_32 - ( zero_point.raw_value_ - raw_value_ );

  if(base == 0 and diff >= mod){
    return diff;
  }

  uint64_t less = 0;
  uint64_t bigger = 0;
  if(diff >= mod){
    bigger = diff + base;
    less = bigger - two_pow_32;
  }else{
    less = diff + base;
    bigger = less + two_pow_32;
  }

  return ( checkpoint - less ) <= ( bigger - checkpoint ) ? less : bigger;
}