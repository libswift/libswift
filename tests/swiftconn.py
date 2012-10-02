import sys
import socket
import struct

import binascii


HASH_ZERO = '\x00' * 20

CHUNK_SPEC_ID_BINS = '\x00'
CHUNK_SPEC_ID_CRANGES = '\x01' 
CHUNK_SPEC_ID_BRANGES = '\x02'
CHUNKSPECID2SPECLEN = {CHUNK_SPEC_ID_BINS:4, CHUNK_SPEC_ID_CRANGES:8, CHUNK_SPEC_ID_BRANGES:16}


class Encodable:
    def to_bytes(self):
        pass

class Bin(Encodable):
    def __init__(self,i):
        self.i = i
        print >>sys.stderr,"Bin: set:",i
        

class MetaChunkSpec(Encodable):
    def __init__(self,chunkspecid):
        self.chunkspecid = chunkspecid
        
    def get_length(self):
        return CHUNKSPECID2SPECLEN[self.chunkspecid]
    
    def decode(self,chunkspec):
        if self.chunkspecid == CHUNK_SPEC_ID_BINS:
            print >>sys.stderr,"CHUNKSPEC LEN",len(chunkspec)
            [i] = struct.unpack(">I",chunkspec)
            return Bin(i)
        else:
            return None
        
    def encode(self,e):
        return e.to_bytes()

MetaChunkSpecs = {CHUNK_SPEC_ID_BINS:MetaChunkSpec(CHUNK_SPEC_ID_BINS)}

class Transfer:
    
    def __init__(self,swarmid,metachunkspec):
        self.swarmid = swarmid
        self.metachunkspec = metachunkspec

    def get_swarm_id(self):
        return self.swarmid
    
    def get_hash_length(self):
        return len(HASH_ZERO)
    
    def get_meta_chunkspec(self):
        return self.metachunkspec
    
    
MSG_ID_HANDSHAKE = '\x00'
MSG_ID_DATA = '\x01'
MSG_ID_ACK = '\x02'
MSG_ID_HAVE = '\x03'
MSG_ID_INTEGRITY = '\x04'

class Datagram(Encodable):
    """ Serialization """
    def __init__(self,t=None,data=None):
        self.t = t
        if data is None: # send
            self.chain = []
        else:
            self.data = data
            self.off = 0
        
    def set_t(self,t):
        self.t = t
        
    def add_channel_id(self,chanid):
        self.chain.append(chanid)
        
    def add_handshake(self,mychanid):
        self.chain.append(MSG_ID_HANDSHAKE)
        self.chain.append(mychanid)
        
    def add_integrity(self,chunkspec,intdata):
        self.chain.append(MSG_ID_INTEGRITY)
        self.chain.append(chunkspec)
        self.chain.append(intdata)
        
    def add_have(self,chunkspec):
        self.chain.append(MSG_ID_HAVE)
        self.chain.append(chunkspec)
        
    def to_bytes(self):
        return "".join(self.chain)

    def get_channel_id(self):
        chanid = self.data[0:len(CONN_ID_ZERO)]
        self.off += len(chanid)

    def get_message(self):
        if self.off == len(self.data):
            return (None,None)
            
        msgid = self.data[self.off:self.off+len(MSG_ID_HANDSHAKE)]
        self.off += len(msgid)
        print >>sys.stderr,"dgram: get_message: GOT msgid",`msgid`
        
        if msgid == MSG_ID_HANDSHAKE:
            fields = self.get_handshake()
        elif msgid == MSG_ID_HAVE:
            fields = self.get_have()
        elif msgid == MSG_ID_INTEGRITY:
            fields = self.get_integrity()
        else:
            print >>sys.stderr,"dgram: get_message: unknown msgid",`msgid`
            fields = []
        return (msgid,fields)
    
    def get_handshake(self):
        chanid = self.data[self.off:self.off+len(CONN_ID_ZERO)]
        self.off += len(chanid)
        return [chanid]
        
    def get_integrity(self):
        intdata = self.data[self.off:self.off+self.t.get_hash_length()]
        self.off += len(intdata)
        return [intdata]
        
    def get_have(self):
        chunkspec = self.data[self.off:self.off+self.t.get_meta_chunkspec().get_length()]
        self.off += len(chunkspec)
        return [chunkspec]
        

CONN_STATE_INIT = 0
CONN_STATE_WAIT4HIS = 1
CONN_STATE_WAIT4MINE = 2
CONN_STATE_ESTABLISHED = 3 

CONN_ID_ZERO = '\x00' * 4

BIN_ALL  = '\x7f\xff\xff\xff'
BIN_NONE = '\xff\xff\xff\xff'

DGRAM_MAX_RECV = 65536

class Channel:
    def __init__(self,t,addr,localinit):
        self.t = t
        self.addr = addr
        self.localinit = localinit
        if localinit:
            self.mychanid = '6778'
        else:
            self.mychanid = CONN_ID_ZERO
        self.hischanid = CONN_ID_ZERO 
        
    def send(self,sock):
        d = Datagram(self.t)
        d.add_channel_id(self.hischanid)
        d.add_integrity(BIN_ALL,self.t.get_swarm_id())
        d.add_handshake(self.mychanid)
        d.add_have(BIN_NONE)
        data = d.to_bytes()
        sock.sendto(data,self.addr)
        
    def recv(self,d):
        d.set_t(self.t)
        
        while True:
            (msgid,fields) = d.get_message()
            if msgid is None:
                break 
            if msgid == MSG_ID_HANDSHAKE:
                print >>sys.stderr,"GOT HS",`fields`
            elif msgid == MSG_ID_INTEGRITY:
                print >>sys.stderr,"GOT INT",`fields`
            elif msgid == MSG_ID_HAVE:
                print >>sys.stderr,"GOT HAVE",`fields`
                mcs = self.t.get_meta_chunkspec()
                mcs.decode(fields[0])
            
        
class Socket:
    def __init__(self,myaddr):
        self.myaddr = myaddr
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(myaddr)

    def recv(self,c):
        data = self.sock.recv(DGRAM_MAX_RECV)
        print >>sys.stderr,"GOT",len(data)

        d = Datagram(None,data)
        chanid = d.get_channel_id()
        # lookup connection
        c.recv(d)
        
        
        
class SwiftConnection:
    def __init__(self,myaddr,hisaddr,swarmid):
        self.s = Socket(("127.0.0.1", 3456))
        t = Transfer(swarmid,MetaChunkSpecs[CHUNK_SPEC_ID_BINS])
        c = Channel(t,hisaddr,True)
        c.send(self.s.sock)
        self.s.recv(c)

