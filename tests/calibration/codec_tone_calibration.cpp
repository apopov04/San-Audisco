// Optional offline codec/tone calibration. This is not a network or hardware test.
// Retains the production 5ms Opus settings and existing spectral thresholds.
// Run explicitly via run_calibration.py; do not add the full matrix to default CTest.
#include <opus/opus.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>
int main(){
 const double pi=std::acos(-1.0); const std::array<double,4> fs{410,730,1090,2390};
 for(int source=0;source<4;++source)for(int contam=-1;contam<4;++contam){if(contam==source)continue;
  for(int mode=0;mode<10;++mode)for(double initial:{0.,.7,1.9}){
   int err=0,bitrate=source<2?128000:48000;auto*en=opus_encoder_create(48000,2,OPUS_APPLICATION_RESTRICTED_LOWDELAY,&err);auto*de=opus_decoder_create(48000,2,&err);
   opus_encoder_ctl(en,OPUS_SET_BITRATE(bitrate));opus_encoder_ctl(en,OPUS_SET_COMPLEXITY(3));opus_encoder_ctl(en,OPUS_SET_VBR(0));
   std::array<opus_int16,480> in{},out{};std::array<unsigned char,1000> pkt{};std::array<double,4>s{},c{};std::vector<std::array<double,4>>windows;long frame=0;double phase=initial,wrongphase=.123;
   for(int b=0;b<2000;b++){
    double drift=mode<9?(-.001+mode*.00025):(-.001+.00001*(b%400<200?b%400:400-b%400));
    for(int i=0;i<240;i++){float x=float(.15*std::sin(phase)+(contam<0?0:.015*std::sin(wrongphase)));phase+=2*pi*fs[source]*(1+drift)/48000;if(contam>=0)wrongphase+=2*pi*fs[contam]*(1+drift)/48000;in[i*2]=in[i*2+1]=static_cast<opus_int16>(x*32768.f);}
    int n=opus_encode(en,in.data(),240,pkt.data(),pkt.size());int got=opus_decode(de,pkt.data(),n,out.data(),240,0);if(got!=240)return 2;
    for(int i=0;i<240;i++){double x=out[2*i]/32768.f;for(int k=0;k<4;k++){double a=2*pi*fs[k]*double(frame+i)/48000;s[k]+=x*std::sin(a);c[k]+=x*std::cos(a);}}
    frame+=240;if((b+1)%20==0){std::array<double,4>w{};for(int k=0;k<4;k++)w[k]=2*std::hypot(s[k],c[k])/4800;if(b>=199)windows.push_back(w);s.fill(0);c.fill(0);}
   }
   std::array<double,4>minimum{9,9,9,9},maximum{};int falsepositive=0,missed=0;
   for(size_t i=0;i+2<windows.size();i++)for(int k=0;k<4;k++){std::array<double,3>v{windows[i][k],windows[i+1][k],windows[i+2][k]};std::sort(v.begin(),v.end());minimum[k]=std::min(minimum[k],v[1]);maximum[k]=std::max(maximum[k],v[1]);if(k!=source&&k!=contam&&v[1]>.008)++falsepositive;if(k==contam&&v[1]<=.008)++missed;}
   std::printf("{\"source\":%d,\"contamination\":%d,\"bitrate\":%d,\"drift_mode\":%d,\"initial_phase\":%.3f,\"false_positive_windows\":%d,\"missed_contamination_windows\":%d,\"min\":[",source,contam,bitrate,mode,initial,falsepositive,missed);for(int k=0;k<4;k++)std::printf("%s%.9f",k?",":"",minimum[k]);std::printf("],\"max\":[");for(int k=0;k<4;k++)std::printf("%s%.9f",k?",":"",maximum[k]);std::puts("]}");
   opus_encoder_destroy(en);opus_decoder_destroy(de);
  }
 }
}
