#!/usr/bin/env python3
"""Real files, independent MKV/MP4 readers, and partial editor failures."""
import json
import os
from pathlib import Path
import shutil
import struct
import re
import subprocess
import sys
import tempfile

binary = sys.argv[1]
failures = 0


def check(ok, name):
    global failures
    print(('PASS ' if ok else 'FAIL ') + name, flush=True)
    failures += not ok


def run(args, **kwargs):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs).stdout


def ffmpeg(*args):
    run(['ffmpeg','-nostdin','-v','error','-y',*map(str,args)])


def digest(path):
    data=json.loads(run(['ffprobe','-v','error','-show_packets','-show_data_hash','sha256',
                         '-show_entries','packet=stream_index,data_hash,size,pts,dts','-of','json',str(path)]))
    return data['packets']


def mark(path):
    data=json.loads(run(['mkvmerge','-J',str(path)]))
    return [t['properties'].get('stereo_mode',0) for t in data['tracks'] if t['type']=='video']


def save(path, container, packing, right=0, stream=0, track=1, env=None):
    process=subprocess.run([binary,str(path),container,str(packing),str(right),str(stream),str(track)],
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
    if process.returncode not in (0,1):
        print('  save code='+str(process.returncode)+' '+process.stdout.decode(errors='replace').strip(),flush=True)
    return process.returncode


def mp4_mode(data):
    # Independent ISO BMFF parser: descend the video sample description to st3d.
    def boxes(start,end):
        pos=start
        while pos<end:
            assert end-pos>=8
            length,kind=struct.unpack_from('>I4s',data,pos)
            header=8
            if length==1:
                length=struct.unpack_from('>Q',data,pos+8)[0];header=16
            elif length==0: length=end-pos
            assert header<=length<=end-pos
            yield pos,kind,pos+header,pos+length
            pos+=length
    def descend(start,end):
        for at,kind,payload,stop in boxes(start,end):
            if kind==b'st3d':
                assert stop-payload==5 and data[payload:payload+4]==b'\0'*4
                return data[payload+4]
            if kind in (b'moov',b'trak',b'mdia',b'minf',b'stbl'):
                result=descend(payload,stop)
                if result is not None:return result
            elif kind==b'stsd':
                assert struct.unpack_from('>I',data,payload+4)[0]==1
                for _,_,entry,entry_end in boxes(payload+8,stop):
                    result=descend(entry+78,entry_end)
                    if result is not None:return result
        return None
    return descend(0,len(data))


with tempfile.TemporaryDirectory(prefix='sview-video-') as scratch:
    folder=Path(scratch)
    original=folder/'source.mkv'
    ffmpeg('-f','lavfi','-i','testsrc2=size=128x32:rate=2','-t','1','-c:v','ffv1','-threads','1',original)
    original_packets=digest(original)
    modes=[(0,0,0),(1,1,0),(2,4,0),(3,3,0),(4,16,0),(5,15,0),(6,14,0),(7,5,0),
           (8,13,0),(9,6,0),(10,9,0),(11,2,0),(12,10,0),(13,8,0),(14,8,1)]
    for value,packing,right in modes:
        path=folder/('mode-'+str(value)+'.mkv')
        shutil.copyfile(original,path)
        result=save(path,'matroska',packing,right)
        check(result in (0,1) and mark(path)==[value], 'Matroska StereoMode '+str(value))
        check(digest(path)==original_packets,'Matroska packets/timestamps retained '+str(value))
    multi=folder/'two-tracks.mkv'
    ffmpeg('-i',original,'-map','0:v','-map','0:v','-c','copy',multi)
    packets=digest(multi)
    check(save(multi,'matroska',1,stream=1,track=2)==0 and mark(multi)==[0,1], 'only selected video track marked')
    check(digest(multi)==packets,'both video tracks retained')
    webm=folder/'source.webm'
    ffmpeg('-i',original,'-c:v','libvpx-vp9','-lossless','1','-threads','1',webm)
    before=webm.read_bytes()
    check(save(webm,'matroska,webm',5)==4 and webm.read_bytes()==before,'WebM unsupported rows refused unchanged')
    check(save(webm,'matroska,webm',2)==0 and mark(webm)==[11], 'WebM right-first side by side')
    mp4=folder/'tail.mp4'
    ffmpeg('-i',original,'-c:v','libx264','-qp','0','-threads','1',mp4)
    packets=digest(mp4)
    check(save(mp4,'mov,mp4',1)==0 and mp4_mode(mp4.read_bytes())==2,'MP4 st3d inserted, independent parser')
    check(digest(mp4)==packets,'MP4 packet bytes and timestamps retained')
    check(save(mp4,'mov,mp4',3)==0 and mp4_mode(mp4.read_bytes())==1,'existing MP4 st3d updated')
    before=mp4.read_bytes()
    check(save(mp4,'mov,mp4',2)==4 and mp4.read_bytes()==before,'MP4 unsupported right-first refused unchanged')
    fast=folder/'faststart.mp4'
    ffmpeg('-i',original,'-c:v','libx264','-qp','0','-threads','1','-movflags','+faststart',fast)
    before=fast.read_bytes()
    check(save(fast,'mov,mp4',1)==5 and fast.read_bytes()==before,'MP4 no-room refusal unchanged')
    sei=folder/'sei.mkv'
    ffmpeg('-i',original,'-c:v','libx264','-x264-params','frame-packing=4','-threads','1',sei)
    frames=json.loads(run(['ffprobe','-v','error','-show_frames','-of','json',str(sei)]))['frames']
    trace=subprocess.run(['ffmpeg','-nostdin','-v','trace','-i',str(sei),'-c','copy','-bsf:v','trace_headers','-f','null','-'],
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True).stderr.decode(errors='replace')
    check(any(side.get('side_data_type')=='Stereo 3D' for frame in frames for side in frame.get('side_data_list',[]))
          and bool(re.search(r'frame_packing_arrangement_type[^\n]*=\s*4\b',trace)), 'independent stream-SEI fixture')
    before=sei.read_bytes()
    check(save(sei,'matroska',1)==10 and sei.read_bytes()==before,'stream SEI conflict refused unchanged')
    tools=folder/'tools';tools.mkdir()
    tool=tools/'mkvpropedit'
    env=os.environ.copy();env['PATH']=str(tools)+os.pathsep+env['PATH']
    for exitcode in (2,0):
        tool.write_text('#!/usr/bin/python3\nimport sys\nwith open(sys.argv[1],"r+b") as f: f.write(b"broken header")\nsys.exit('+str(exitcode)+')\n')
        tool.chmod(0o755)
        path=folder/('partial-tool-'+str(exitcode)+'.mkv');shutil.copyfile(original,path)
        before=path.read_bytes()
        result=save(path,'matroska',1,env=env)
        check(result not in (0,1) and path.read_bytes()==before,'partial editor failure/read-back preserves original '+str(exitcode))
    check(not list(folder.glob('.stereo-save-*')), 'no transaction temporaries left')
print('video failures='+str(failures),flush=True)
sys.exit(bool(failures))
