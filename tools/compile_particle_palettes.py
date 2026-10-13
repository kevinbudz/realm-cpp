#!/usr/bin/env python3
"""Finite, pixel-weighted source texture colors for native hit/blood particles."""
import json,sys
from pathlib import Path
from PIL import Image
inventory,output=map(Path,sys.argv[1:]);data=json.loads(inventory.read_text());root=Path(data['source_root']);colors=[];ranges=[];groups=[]
for animated,key in [(False,'sheets'),(True,'animated')]:
 starts=[]
 for s in data[key]:
  starts.append(len(ranges))
  if s['asset']<0:ranges.append((len(colors),0));continue
  im=Image.open(root/data['assets'][s['asset']]['path']).convert('RGBA')
  sw,sh=(s['strip_width'],s['strip_height']) if animated else (s['width'],s['height'])
  for y in range(0,im.height-sh+1,sh):
   for x in range(0,im.width-sw+1,sw):
    # TextureData keeps imageFromAngle(0, STAND, 0), i.e. right-standing.
    row=s['height'] if animated and s['first']=='DOWN' and sh>=2*s['height'] else 0
    start=len(colors)
    for r,g,b,a in im.crop((x,y+row,x+s['width'],y+row+s['height'])).getdata():
     if a:colors.append((r<<16)|(g<<8)|b)
    ranges.append((start,len(colors)-start))
 starts.append(len(ranges));groups.append(starts)
lines=['// Generated from original source pixels; no runtime image cache.', '#pragma once','#include <array>','#include <cstdint>','namespace realm::flash {','struct PaletteRange { unsigned offset, count; };']
for key,values in [('particle_sheet_starts',groups[0]),('particle_animated_starts',groups[1])]:lines.append(f'inline constexpr std::array<unsigned,{len(values)}> {key}{{'+','.join(map(str,values))+'};')
lines.append(f'inline constexpr std::array<PaletteRange,{len(ranges)}> particle_palettes{{{{'+','.join('{'+f'{a},{b}'+'}' for a,b in ranges)+'}};')
lines.append(f'inline constexpr std::array<std::uint32_t,{len(colors)}> particle_colors{{')
for i in range(0,len(colors),128):lines.append(','.join(hex(v) for v in colors[i:i+128])+',')
lines+=['};','}'];output.write_text('\n'.join(lines)+'\n');print(f'Particle colors: {len(ranges)} textures, {len(colors)} weighted pixels')
