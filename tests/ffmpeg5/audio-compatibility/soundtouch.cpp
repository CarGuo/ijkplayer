/* Real pinned SoundTouch algorithm through production bounded adapter. */
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstring>
#include <climits>
#include <complex>
#include <algorithm>
#include "ff_soundtouch.h"
#include "SoundTouch.h"
static void check_pitch(const std::vector<short>& pcm,double frequency) {
 const size_t n=16384;assert(pcm.size()>=n);
 std::vector<std::complex<double>> fft(n);
 size_t offset=(pcm.size()-n)/2;double energy=0;
 for(size_t i=0;i<n;i++){double v=pcm[offset+i];energy+=v*v;
  fft[i]=v*(.5-.5*std::cos(2*3.141592653589793*i/(n-1)));}
 assert(std::sqrt(energy/n)>1000);
 for(size_t i=1,j=0;i<n;i++){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(fft[i],fft[j]);}
 for(size_t len=2;len<=n;len<<=1){auto step=std::polar(1.,-2*3.141592653589793/len);
  for(size_t i=0;i<n;i+=len){std::complex<double>w(1,0);for(size_t j=0;j<len/2;j++){
   auto u=fft[i+j],v=fft[i+j+len/2]*w;fft[i+j]=u+v;fft[i+j+len/2]=u-v;w*=step;}}}
 size_t peak=1;for(size_t i=2;i<n/2;i++)if(std::norm(fft[i])>std::norm(fft[peak]))peak=i;
 double measured=peak*48000./n;assert(std::fabs(measured-frequency)<8.0);
}
static long run(double rate, int input_frames, int channels) {
 void *h=ffp_soundtouch_create(48000,channels,rate);assert(h);
 soundtouch::SoundTouch reference;reference.setSampleRate(48000);reference.setChannels(channels);
 reference.setRate(1);reference.setPitch(1);reference.setTempo(rate);
 std::vector<short> in(1024*channels),out(257*channels+2),ref(257*channels);
 long total=0;std::vector<short> audible;
 auto drain=[&](){for(;;){out.front()=123;out.back()=456;
  int n=ffp_soundtouch_receive(h,out.data()+1,257);assert(n>=0&&n<=257);
  assert(out.front()==123&&out.back()==456);
  unsigned r=reference.receiveSamples(ref.data(),257);assert((unsigned)n==r);
  assert(!std::memcmp(out.data()+1,ref.data(),n*channels*sizeof(short)));
  for(int i=0;i<n;i++)audible.push_back(out[1+i*channels]);
  total+=n;if(!n)break;}};
 for(int pos=0;pos<input_frames;) {int n=std::min(1024,input_frames-pos);
  for(int i=0;i<n*channels;i++)in[i]=(short)(12000*std::sin((pos*channels+i)*.13));
  assert(!ffp_soundtouch_put(h,in.data(),n));reference.putSamples(in.data(),n);pos+=n;drain();}
 ffp_soundtouch_flush(h);reference.flush();drain();
 ffp_soundtouch_flush(h);assert(!ffp_soundtouch_receive(h,out.data()+1,257));
 assert(std::labs(total-std::lround(input_frames/rate))<=1);
 assert(ffp_soundtouch_put(h,in.data(),1)<0);
 if(input_frames>=102400)check_pitch(audible,.13*channels*48000/(2*3.141592653589793));
 ffp_soundtouch_destroy(h);return total;
}
int main(){
 for(double r:{.25,.5,1.,2.,3.,4.})for(int ch:{1,2}) {
  long n=run(r,102400,ch);std::printf("PASS real SoundTouch %.2fx channels=%d EOS frames=%ld\n",r,ch,n);}
 for(double r:{1.,3.,1.})run(r,10240,2); /* fresh drained segments */
 for(int i=0;i<3;i++){void *h=ffp_soundtouch_create(48000,2,3);assert(h);
  short in[2048]={0};assert(!ffp_soundtouch_put(h,in,1024));ffp_soundtouch_destroy(h);
  run(.5,10240,2);} /* committed seek drops old buffered input, new flush valid */
 assert(!ffp_soundtouch_create(48000,2,NAN));assert(!ffp_soundtouch_create(48000,2,0));
 assert(!ffp_soundtouch_create(0,2,1));
 for(double r:{.24999,4.00001,1e-30,1e30,(double)INFINITY,-1.})assert(!ffp_soundtouch_create(48000,2,r));
 void *h=ffp_soundtouch_create(48000,2,.25);assert(h);
 assert(ffp_soundtouch_needs_reset(h,INT_MAX));
 assert(!ffp_soundtouch_needs_reset(h,1024));ffp_soundtouch_destroy(h);
 puts("PASS bounded output equals reference, 1→3→1, reset/recreate and invalid rate");
}
