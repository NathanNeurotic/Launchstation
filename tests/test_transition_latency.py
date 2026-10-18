"""Verify encoded transition retains its decoded waveform and bounded preroll."""
from pathlib import Path
import hashlib
import struct
root=Path(__file__).resolve().parents[1]
b=(root/'res/sfx/transition.adp').read_bytes()
assert b[:8] == b'APCM\x01\x01\x00\x00'
assert len(b)%16==0 and b[16]>>4==0
coef=[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]
h1=h2=0
pcm=[]
for k in range(16,len(b),16):
    shift=b[k]&15; f=b[k]>>4
    assert f<5
    for byte in b[k+2:k+16]:
        for n in (byte&15,byte>>4):
            if n>=8: n-=16
            v=((n<<12)>>shift)+((h1*coef[f][0]+h2*coef[f][1]+32)>>6)
            v=max(-32768,min(32767,v)); h2=h1; h1=v; pcm.append(v)
assert struct.unpack_from('<I',b,12)[0] == len(pcm)
# Golden hash of the original decoded waveform after the 110 removed frames.
assert hashlib.sha256(struct.pack('<'+'h'*len(pcm),*pcm)).hexdigest() == "766addbf6207892c794dd63e617c57046b91a9af81becb30041b663fea227631"
rate=struct.unpack_from('<I',b,8)[0]*48000/4096
onset=next(i for i,v in enumerate(pcm) if abs(v)>32)/rate*1000
assert onset<15, onset
print(f'PASS: transition waveform retained; signal onset {onset:.2f} ms above amplitude 32')
