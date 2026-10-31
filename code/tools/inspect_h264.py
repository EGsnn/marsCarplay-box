#!/usr/bin/env python3
"""Read raw H.264 SPS/PPS facts; no transcoding or compatibility claims."""
import argparse, hashlib, json, re
from pathlib import Path
class Bits:
    def __init__(self,data):self.data=data;self.at=0
    def read(self,n):
        if n<0 or self.at+n>len(self.data)*8:raise ValueError('Truncated RBSP')
        v=0
        for _ in range(n):v=(v<<1)|((self.data[self.at//8]>>(7-self.at%8))&1);self.at+=1
        return v
    def ue(self):
        z=0
        while not self.read(1):
            z+=1
            if z>31:raise ValueError('Invalid Exp-Golomb')
        return (1<<z)-1+self.read(z)
    def se(self):
        v=self.ue();return (v+1)//2 if v&1 else -v//2

def rbsp(nal):
    return nal[1:].replace(b'\0\0\3',b'\0\0')

def inspect(data):
    result={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'sps':[],'pps':[]}
    for nal in re.split(b'\x00\x00(?:\x00)?\x01',data):
        if not nal:continue
        kind=nal[0]&31;b=Bits(rbsp(nal))
        if kind==7:
            p=b.read(8);constraints=b.read(8);level=b.read(8);sid=b.ue();chroma=1;depth=8
            if p in (100,110,122,244,44,83,86,118,128,138,139,134,135):
                chroma=b.ue()
                if chroma==3:b.read(1)
                depth=8+b.ue();b.ue();b.read(1)
                if b.read(1):
                    for i in range(8 if chroma!=3 else 12):
                        if b.read(1):
                            last=next_=8
                            for _ in range(16 if i<6 else 64):
                                if next_:next_=(last+b.se()+256)%256
                                if next_:last=next_
            log_frame=4+b.ue();poc=b.ue()
            if poc==0:b.ue()
            elif poc==1:
                b.read(1);b.se();b.se()
                for _ in range(b.ue()):b.se()
            refs=b.ue();b.read(1);w=(b.ue()+1)*16;map_h=b.ue()+1;progressive=b.read(1)
            if not progressive:b.read(1)
            b.read(1);crop=[b.ue() for _ in range(4)] if b.read(1) else [0]*4
            h=map_h*16*(2-progressive);subw=1 if chroma in (0,3) else 2;subh=2 if chroma==1 else 1
            result['sps'].append(dict(id=sid,profile_idc=p,constraints=constraints,level_idc=level,chroma_format_idc=chroma,bit_depth_luma=depth,max_num_ref_frames=refs,log2_max_frame_num=log_frame,pic_order_cnt_type=poc,frame_mbs_only_flag=progressive,coded_width=w,coded_height=h,crop_offsets=crop,width=w-(crop[0]+crop[1])*subw,height=h-(crop[2]+crop[3])*subh*(2-progressive)))
        elif kind==8:
            result['pps'].append(dict(id=b.ue(),sps_id=b.ue(),entropy_coding_mode_flag=b.read(1),bottom_field_pic_order_in_frame_present_flag=b.read(1),num_slice_groups=1+b.ue()))
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('sample',type=Path);p.add_argument('--out',type=Path);a=p.parse_args();r=inspect(a.sample.read_bytes());s=json.dumps(r,indent=2)+'\n';print(s)
    if a.out:a.out.write_text(s)
