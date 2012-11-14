import sys
import socket
import struct

import binascii


HASH_ZERO = '\x00' * 20

CHUNK_SPEC_ID_BIN32 = '\x00'
CHUNK_SPEC_ID_BYTE64 = '\x01'
CHUNK_SPEC_ID_CHUNK32 = '\x02' 


class Encodable:
    def to_bytes(self):
        pass
    def from_bytes(bytes):
        pass
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length(self):
        pass
    def __repr__(self):
        return self.__str__()


class Bin(Encodable):
    def __init__(self,i):
        self.i = i
        print >>sys.stderr,"Bin: set:",self.i
    def to_bytes(self):
        return struct.pack(">I",self.i)
    def from_bytes(bytes):
        [i] = struct.unpack(">I",bytes)
        return Bin(i)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length(self):
        return 4
    def get_id(self):
        return CHUNK_SPEC_ID_BIN32
    def __str__(self):
        return "Bin("+str(self.i)+")"

    
BIN_ALL  = Bin(0x7fffffff)
BIN_NONE = Bin(0xffffffff)


class ChunkRange(Encodable):
    def __init__(self,s,e):
        self.s = s
        self.e = e
        print >>sys.stderr,"ChunkRange: set:",s,e
    def to_bytes(self):
        return struct.pack(">II",self.s,self.e)
    def from_bytes(bytes):
        [s,e] = struct.unpack(">II",bytes)
        return ChunkRange(s,e)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length(self):
        return 8
    def get_id(self):
        return CHUNK_SPEC_ID_CHUNK32
    def __str__(self):
        return "ChunkRange("+str(self.s)+","+str(self.e)+")"


class Transfer:
    
    def __init__(self,swarmid,version,chunkspec,socket):
        self.swarmid = swarmid
        self.version = version
        self.chunkspec = chunkspec
        self.socket = socket

    def get_swarm_id(self):
        return self.swarmid
    
    def get_version(self):
        return self.version
    
    def get_hash_length(self):
        return len(HASH_ZERO)
    
    def get_chunkspec(self):
        return self.chunkspec
    
    def get_socket(self):
        return self.socket
    
MSG_ID_HANDSHAKE = '\x00'
MSG_ID_DATA = '\x01'
MSG_ID_ACK = '\x02'
MSG_ID_HAVE = '\x03'
MSG_ID_INTEGRITY = '\x04'

POPT_VER_TYPE = '\x00'
POPT_VER_SWIFT = '\x00'
POPT_VER_PPSP = '\x01'

POPT_SWARMID_TYPE = '\x01'
POPT_CIPM_TYPE = '\x02'
POPT_MHF_TYPE = '\x03'
POPT_LSA_TYPE = '\x04'
POPT_CAM_TYPE = '\x05'
POPT_LDW_TYPE = '\x06'
POPT_MSGS_TYPE = '\x07'
POPT_END_TYPE = '\xff'

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
        
    def add_handshake(self,mychanid,intdata=None):
        self.chain.append(MSG_ID_HANDSHAKE)
        self.chain.append(mychanid)
        if self.t.get_version() == POPT_VER_PPSP:
            self.chain.append(POPT_VER_TYPE)
            self.chain.append(self.t.get_version())
            self.chain.append(POPT_SWARMID_TYPE)
            s = len(intdata)
            sbytes = struct.pack(">H",s)
            self.chain.append(sbytes)
            self.chain.append(intdata)
            self.chain.append(POPT_END_TYPE)
        
    def add_integrity(self,chunkspec,intdata):
        self.chain.append(MSG_ID_INTEGRITY)
        self.chain.append(chunkspec.to_bytes())
        self.chain.append(intdata)
        
    def add_have(self,chunkspec):
        self.chain.append(MSG_ID_HAVE)
        self.chain.append(chunkspec.to_bytes())
        
    def to_bytes(self):
        print >>sys.stderr,"Datagram: to_bytes:",`self.chain`
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
        ver = None
        intdata = None
        if self.t.get_version() == POPT_VER_PPSP:
            while self.off < len(self.data):
                popt = self.data[self.off]
                self.off += 1
                if popt == POPT_VER_TYPE:
                    ver = self.data[self.off]
                    self.off += 1
                elif popt == POPT_SWARMID_TYPE:
                    intdata = self.data[self.off:self.off+self.t.get_hash_length()]
                    self.off += len(intdata)
                elif popt == POPT_END_TYPE:
                    break
        
        return [chanid,ver,intdata]
        
    def get_integrity(self):
        cabytes = self.data[self.off:self.off+self.t.get_chunkspec().get_bytes_length()]
        self.off += len(cabytes)
        chunkspec = self.t.chunkspec.from_bytes(cabytes)
        intdata = self.data[self.off:self.off+self.t.get_hash_length()]
        self.off += len(intdata)
        return [chunkspec,intdata]
        
    def get_have(self):
        cabytes = self.data[self.off:self.off+self.t.get_chunkspec().get_bytes_length()]
        self.off += len(cabytes)
        chunkspec = self.t.chunkspec.from_bytes(cabytes)
        return [chunkspec]
        

CONN_STATE_INIT = 0
CONN_STATE_WAIT4HIS = 1
CONN_STATE_WAIT4MINE = 2
CONN_STATE_ESTABLISHED = 3 

CONN_ID_ZERO = '\x00' * 4


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
        
    def send(self,d):
        self.t.get_socket().sendto(d,self.addr)
        
    def recv(self,d):
        d.set_t(self.t)
        
        while True:
            (msgid,fields) = d.get_message()
            if msgid is None:
                break 
            if msgid == MSG_ID_HANDSHAKE:
                print >>sys.stderr,"Found HS",`fields`
            elif msgid == MSG_ID_INTEGRITY:
                print >>sys.stderr,"Found INT",`fields`
            elif msgid == MSG_ID_HAVE:
                print >>sys.stderr,"Found HAVE",`fields`
      
    def get_my_chanid(self):
        return self.mychanid
      
    def get_his_chanid(self):
        return self.hischanid
            
        
class Socket:
    def __init__(self,myaddr):
        self.myaddr = myaddr
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(myaddr)

    def recv(self):
        data = self.sock.recv(DGRAM_MAX_RECV)
        print >>sys.stderr,"recv len",len(data)

        return Datagram(None,data)
        
    def sendto(self,d,addr):
        data = d.to_bytes()
        return self.sock.sendto(data,addr)
        
        
class SwiftConnection:
    def __init__(self,myaddr,hisaddr,swarmid):
        self.s = Socket(myaddr)
        t = Transfer(swarmid,POPT_VER_PPSP,BIN_ALL,self.s)
        c = Channel(t,hisaddr,True)
        
        d = Datagram(t)
        if t.version == POPT_VER_SWIFT:
            d.add_channel_id(c.get_his_chanid())
            d.add_integrity(BIN_ALL,t.get_swarm_id())
            d.add_handshake(c.get_my_chanid())
            d.add_have(BIN_NONE)
            c.send(d)
        else:
            d.add_channel_id(c.get_his_chanid())
            d.add_handshake(c.get_my_chanid(),t.get_swarm_id())
            d.add_have(ChunkRange(0,4))
            c.send(d)
        while True:
            d = self.s.recv()
            chanid = d.get_channel_id()
            # self.assertEquals(chanid,c.get_my_chanid())
            c.recv(d)
            break

