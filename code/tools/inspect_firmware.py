#!/usr/bin/env python3
"""Check actual linked version, USB descriptor, advertised video and BOX BSP."""
from pathlib import Path
import hashlib,json,plistlib,struct
from elftools.elf.elffile import ELFFile
root=Path(__file__).resolve().parents[2];binary=root/'build/firmware/atk_box_carplay.bin'
with (root/'build/firmware/atk_box_carplay.elf').open('rb') as f:
 elf=ELFFile(f);symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
 def contents(s):
  section=elf.get_section(s['st_shndx']);at=s['st_value']-section['sh_addr'];return section.data()[at:at+s['st_size']]
 def value(name):
  found=[s for s in symbols if s.name==name and s['st_size']];assert len(found)==1,(name,len(found));return contents(found[0])
 assert value('esp_app_desc')[16:48].split(b'\0')[0]==b'0.1.8-public-source'
 assert struct.unpack('<II',value('box_clock_config'))==(80,240)
 assert struct.unpack('<III',value('box_video_config'))==(320,192,8)
 info=plistlib.loads(value('probe_info'));assert (info['displays'][0]['widthPixels'],info['displays'][0]['heightPixels'])==(320,192)
 assert info['displays'][0]['maxFPS']==8
 assert info['model']=='ATK-DNESP32S3-BOX' and info['name']=='ATK BOX CarPlay'
 config=value('configuration');assert config[4]==3 and config[8]==250 and int.from_bytes(config[2:4],'little')==len(config)
 endpoints=[];interfaces=[];at=0
 while at<len(config):
  n,kind=config[at:at+2];assert n>=2 and at+n<=len(config)
  if kind==4:interfaces.append(tuple(config[at+i] for i in (2,3,4,5,6,7)))
  if kind==5:endpoints.append(config[at+2])
  at+=n
 assert (0,0,2,255,240,0) in interfaces and (1,0,1,2,13,0) in interfaces
 assert sorted(endpoints)==[1,3,129,130,131]
 assert any(s.name=='info' and s['st_size']==20 and struct.unpack_from('<HHII',contents(s),8)==(320,240,16*1024*1024,8*1024*1024) for s in symbols)
 assert not any(s.name in ('probe_ui_draw','box_direct_update_status') for s in symbols)
 assert any(s.name=='probe_ui_draw_idle' for s in symbols)
 for name in ('box_decoder_feed','box_pixels_fit','box_direct_start','box_direct_sample','box_perf_sample','box_decoder_profile','box_decode_profile_record'):assert any(s.name==name for s in symbols),name
 result={'firmware':'0.1.8-public-source','target':'ATK-DNESP32S3-BOX','native_display':[320,240],'advertised_video':[320,192],'advertised_max_fps':8,'usb_interfaces':interfaces,'usb_endpoints':endpoints,'usb_max_power_ma':500,'local_decoder_linked':True,'psram_config_mhz':80,'cpu_config_mhz':240,'bytes':binary.stat().st_size,'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'flashed':False,'real_box_display':'pending'}
(root/'build/firmware-inspection.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
