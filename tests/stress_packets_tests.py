#!/usr/bin/env python3
"""Disposable local peer: paced/fragmented/coalesced bursts and RECONNECT.
Application barriers hold simulation time fixed while transport arrival varies.
No production server, saved credentials or settings are used.
"""
import hashlib,json,socket,struct,subprocess,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
utf=lambda s:struct.pack('>H',len(s.encode()))+s.encode()
frame=lambda i,b:struct.pack('>IB',len(b)+5,i)+b

def exact(c,n):
    b=b''
    while len(b)<n:
        part=c.recv(n-len(b))
        assert part,'Disconnected before barrier ACK'
        b+=part
    return b

def receive(c):
    n=struct.unpack('>I',exact(c,4))[0];assert 5<=n<=1048576
    b=exact(c,n-4);return b[0],b[1:]

def run(exe,out,fragment,delay):
    errors=[];sent=[];received=[]
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0));listener.listen(2);listener.settimeout(30)
        def send(c,b):
            sent.append(b)
            if fragment:
                at=0
                for size in [1,2,3,7,31,127,4096]:
                    c.sendall(b[at:at+size]);at+=size;time.sleep(.001)
                c.sendall(b[at:])
            else:c.sendall(b)
        def peer():
            try:
                for connection in range(2):
                    c,_=listener.accept()
                    with c:
                        c.settimeout(30)
                        ident,b=receive(c);assert ident==15
                        assert b==utf('1.0.0')+struct.pack('>i',-1 if not connection else -3)+utf('Q02Fixture')+utf('disposable-mock')+struct.pack('>i',0)
                        received.append((ident,b))
                        info=struct.pack('>ii',128,128)+utf('Nexus')+utf('Stress mock')+struct.pack('>Ii??',77,0,True,True)
                        send(c,frame(20,info))
                        ident,b=receive(c);assert ident==21 and b==struct.pack('>i',7);received.append((ident,b))
                        status=lambda i,x,y:struct.pack('>iffB',i,x,y,0)
                        objects=b''.join(struct.pack('>H',0x030e)+status(i,64 if i==1 else 64+(i%10)*.01,64) for i in range(1,1001))
                        send(c,frame(1,struct.pack('>ii',1,7))+frame(9,struct.pack('>hh',0,1000)+objects+struct.pack('>h',0)))
                        for batch in range(2):
                            stage=connection*2+batch
                            burst=b''
                            for k in range(300):
                                tick=struct.pack('>h',1)+status(2,65+stage*.125,64)
                                burst+=frame(11,tick)+frame(61,struct.pack('>i',stage*1000+k))
                            text=utf('')+struct.pack('>iiB',-1,-1,0)+utf('')+utf('BARRIER')
                            send(c,burst+frame(6,text))
                            counts={};ack=False
                            while not ack or counts.get(4,0)<300:
                                ident,b=receive(c);received.append((ident,b));counts[ident]=counts.get(ident,0)+1
                                if ident==5:
                                    assert b==utf('BARRIER_ACK_'+str(stage+1));ack=True
                                if ident==62:
                                    serial,t=struct.unpack('>ii',b);assert stage*1000<=serial<stage*1000+300 and t==stage*100
                                if ident==4:
                                    t,x,y=struct.unpack('>iff',b);assert t==stage*100 and x==64 and y==64
                            assert counts=={62:300,5:1,4:300},counts
                        if not connection:send(c,frame(19,struct.pack('>i',-3)))
            except BaseException as e:errors.append(repr(e))
        t=threading.Thread(target=peer);t.start()
        cmd=[str(exe),'--packets',str(listener.getsockname()[1]),str(out)+'.json',str(out)+'.jsonl',str(delay)]
        result=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=40);t.join(30)
        assert not t.is_alive(),'Peer still active'
        assert not errors,errors
        assert result.returncode==0,result.stdout+result.stderr
    summary=json.loads(Path(str(out)+'.json').read_text())
    assert summary['barriers']==4 and summary['reconnects']==1 and summary['resident']==1000
    assert summary['queue_peak']<1048576 and summary['framing_peak']<1048576
    packet_bytes=b''.join(frame(i,b) for i,b in received)
    # Cross-type network response interleaving may vary; compare each FIFO type independently.
    hashes={str(i):hashlib.sha256(b''.join(b for ident,b in received if ident==i)).hexdigest() for i in sorted(set(i for i,b in received))}
    summary.update(command=cmd,received_packets=len(received),packet_hash_by_type=hashes,bytes_sent=sum(map(len,sent)),bytes_received=len(packet_bytes),fragmented=fragment,driver_delay_ms=delay)
    Path(str(out)+'.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary

def main():
    exe=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]) if len(sys.argv)>2 else ROOT/'build/stress-packets';out.mkdir(parents=True,exist_ok=True)
    a=run(exe,out/'coalesced',False,0);b=run(exe,out/'fragmented',True,3)
    for key in ['state_checksum','packet_hash_by_type','resident','barriers','reconnects']:assert a[key]==b[key],key
    print('PACKETS: 1000 entities, 1200 ticks/PONG/MOVE, fragmented/coalesced/jittered bursts, reconnect and bounded queues passed')
if __name__=='__main__':main()
