#include "bitstream.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
static uint32_t random_state=0x32ab917;
static uint32_t rng(){random_state=random_state*1664525U+1013904223U;return random_state;}
static uint32_t expected(const std::vector<uint8_t>& data,unsigned pos,unsigned n){
 uint32_t v=0;for(unsigned i=0;i<n;i++)v=(v<<1)|((pos+i)/8<data.size()?((data[(pos+i)/8]>>(7-(pos+i)%8))&1):0);return v;
}
int main(){
 unsigned checks=0;
 for(unsigned size=0;size<=96;size++){
  std::vector<uint8_t> bytes(size);for(auto& b:bytes)b=rng();
  sub0h264::BitReader br(bytes.data(),bytes.size());
  for(unsigned pos=0;pos<size*8+64;pos++){
   for(unsigned n=0;n<=32;n++){
    br.seekToBit(pos);auto value=expected(bytes,pos,n);
    assert(br.peekBits(n)==value&&br.bitOffset()==pos);
    assert(br.readBits(n)==value&&br.bitOffset()==pos+n);++checks;
   }
   // Warm a cache, then walk all 64 bits and cross its end, including padding.
   br.seekToBit(pos);(void)br.peekBits(32);
   for(unsigned i=0;i<72;i++)assert(br.readBit()==expected(bytes,pos+i,1));
  }
  unsigned pos=0;br.seekToBit(0);
  for(unsigned i=0;i<1000;i++){
   unsigned n=rng()%33;
   if(rng()&1){assert(br.readBits(n)==expected(bytes,pos,n));pos+=n;}
   else {assert(br.peekBits(n)==expected(bytes,pos,n));assert(br.readBit()==expected(bytes,pos,1));++pos;}
   if(i%17==0){pos=rng()%(size*8+64);br.seekToBit(pos);}
  }
 }
 std::printf("32-bit extraction: %u width/position cases plus cached single-bit and mixed seek/read/padding checks passed\n",checks);
}
