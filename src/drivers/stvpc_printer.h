// license:BSD-3-Clause
// Super TV-PC serial printer: inverted data on PIO0.5, clock on PIO1.1.
struct StvpcPrinter {
 static constexpr unsigned width=384, rowbytes=48, maxheight=4096;
 unsigned bits=0, count=0, height=0, rows=0, busy=0, sequence=0;
 uint8_t byte=0, packet[49]{}, pixels[rowbytes*maxheight]{};
 void write_progress() {
  static const char bin_path[]="/tmp/stvpc-print-progress.bin";
  static const char bin_temp[]="/tmp/stvpc-print-progress.tmp";
  FILE *f=fopen(bin_temp,"wb");
  if(!f)return;
  uint32_t header[4]={0x52505453, (uint32_t)width, (uint32_t)height, (uint32_t)rows};
  bool ok=(fwrite(header,sizeof(uint32_t),4,f)==4);
  if(ok && rows>0) {
   ok=(fwrite(pixels,1,rows*rowbytes,f)==(rows*rowbytes));
  }
  if(fclose(f))ok=false;
  if(ok)rename(bin_temp,bin_path);else remove(bin_temp);
 }
 void reset() {
  bits=count=height=rows=busy=0;byte=0;
  remove("/tmp/stvpc-print-progress.bin");
  remove("/tmp/stvpc-print-progress.tmp");
 }
 uint8_t status() {if(busy){--busy;return 0;}return 1;}
 void receive(uint8_t value) {
  packet[count++]=value;
  if(count!=49)return;
  count=0;busy=4;
  if(!height&&packet[0]==0&&packet[1]=='H'&&packet[2]=='=') {
   height=packet[3]|(packet[4]<<8);rows=0;
   if(!height||height>maxheight)height=0;
   else write_progress();
   return;
  }
  if(!height)return;
  memcpy(pixels+rows*rowbytes,packet+1,rowbytes);
  ++rows;
  write_progress();
  if(rows!=height)return;
  char path[96],temp[100];
  snprintf(path,sizeof(path),"/tmp/stvpc-print-%08u.pgm",++sequence);
  snprintf(temp,sizeof(temp),"%s.tmp",path);
  FILE *out=fopen(temp,"wb");
  if(out) {
   bool ok=fprintf(out,"P5\n%u %u\n255\n",width,height)>0;
   for(unsigned row=0;row<height&&ok;row++) {
    uint8_t gray[width];
    for(unsigned x=0;x<width;x++)gray[x]=(pixels[row*rowbytes+x/8]&(1<<(x&7)))?0:255;
    ok=fwrite(gray,1,width,out)==width;
   }
   if(fclose(out))ok=false;
   if(ok)rename(temp,path);else remove(temp);
  }
  height=rows=0;
  remove("/tmp/stvpc-print-progress.bin");
  remove("/tmp/stvpc-print-progress.tmp");
 }
 void clock(uint8_t old, uint8_t value,uint8_t data) {
  if((old&2)||!(value&2))return;
  byte|=((data&0x20)?0:1)<<bits;
  if(++bits==8){receive(byte);bits=0;byte=0;}
 }
};
