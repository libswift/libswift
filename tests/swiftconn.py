# Written by Arno Bakker
# see LICENSE.txt for license information
#
# Python implementation of the on-the-wire part of the swift protocol 
# (PPSP-04 draft) for simple tests. 
#

import sys
import socket
import struct

import binascii


HASH_ZERO = '\x00' * 20


TSBYTES_LEN = 8

IPV4BYTES_LEN = (32/8)+2
IPV6BYTES_LEN = (128/8)+2


CHUNK_SPEC_ID_BIN32 = '\x00'
CHUNK_SPEC_ID_BYTE64 = '\x01'
CHUNK_SPEC_ID_CHUNK32 = '\x02' 
CHUNK_SPEC_ID_BIN64 = '\x03' 
CHUNK_SPEC_ID_CHUNK64 = '\x04'


class Encodable:
    def to_bytes(self):
        pass
    def from_bytes(bytes):
        pass
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        pass
    def __repr__(self):
        return self.__str__()

#
# Chunk Addressing
#

class Bin(Encodable):
    def __init__(self,i):
        self.i = i
        #print >>sys.stderr,"Bin: set:",self.i
    def to_bytes(self):
        return struct.pack(">I",self.i)
    def from_bytes(bytes):
        [i] = struct.unpack(">I",bytes)
        return Bin(i)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 4
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return CHUNK_SPEC_ID_BIN32
    get_id = staticmethod(get_id)
    def __str__(self):
        return "Bin("+str(self.i)+")"

    
BIN_ALL  = Bin(0x7fffffff)
BIN_NONE = Bin(0xffffffff)


class ChunkRange(Encodable):
    def __init__(self,s,e):
        self.s = s
        self.e = e
        #print >>sys.stderr,"ChunkRange: set:",s,e
    def to_bytes(self):
        return struct.pack(">II",self.s,self.e)
    def from_bytes(bytes):
        [s,e] = struct.unpack(">II",bytes)
        return ChunkRange(s,e)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 8
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return CHUNK_SPEC_ID_CHUNK32
    get_id = staticmethod(get_id)
    def __str__(self):
        return "ChunkRange("+str(self.s)+","+str(self.e)+")"

#
# TimeStamp
#

class TimeStamp(Encodable):
    def __init__(self,ts):
        self.ts = ts
    def to_bytes(self):
        return struct.pack(">Q",self.ts)
    def from_bytes(bytes):
        [ts] = struct.unpack(">Q",bytes)
        return TimeStamp(ts)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 8
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return None
    get_id = staticmethod(get_id)
    def __str__(self):
        return "TimeStamp("+str(self.s)+","+str(self.e)+")"


class IPv4Port(Encodable):
    def __init__(self,ipport): # Python tuple
        self.ipport = ipport
    def to_bytes(self):
        ipbytes = socket.inet_aton(self.ipport[0])
        portbytes = struct.pack(">H",self.ipport[1])
        chain = [ipbytes,portbytes]
        return "".join(chain)
    def from_bytes(bytes):
        ipbytes = bytes[0:4]
        ip = socket.inet_ntoa(ipbytes)
        portbytes = bytes[4:6]
        [port] = struct.unpack(">H",portbytes)
        return IPv4Port((ip,port))
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 6
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return None
    get_id = staticmethod(get_id)
    def __str__(self):
        return str(self.ipport)


class IPv6Port(Encodable):
    def __init__(self,ipport): # Python tuple
        self.ipport = ipport
    def to_bytes(self):
        if sys.platform == "win32":
            ipbytes = inet_pton6(self.ipport[0])
        else:
            ipbytes = socket.inet_pton(socket.AF_INET6, self.ipport[0])
            
        portbytes = struct.pack(">H",self.ipport[1])
        chain = [ipbytes,portbytes]
        return "".join(chain)
    def from_bytes(bytes):
        ipbytes = bytes[0:16]
        ip = socket.inet_ntop(socket.AF_INET6, ipbytes)
        portbytes = bytes[16:18]
        [port] = struct.unpack(">H",portbytes)
        return IPv4Port((ip,port))
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 18
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return None
    get_id = staticmethod(get_id)
    def __str__(self):
        return str(self.ipport)


# Known deficiency: don't handle :: in the middle right.
def inet_pton6(p):
    #print >>sys.stderr,"inet_pton: Input",p,len(p)
    if '.' in p:
        # IPv4 mapped
        idx = p.rfind(':')
        p4 = p[idx+1:]
        n4 = socket.inet_aton(p4)
        n = '\x00' * 10
        n += '\xff' * 2
        n += n4
        #print >>sys.stderr,"inet_pton: IPv4mapped",binascii.hexlify(n)
        return n

    s = ''    
    q = p
    sidx = len(q)
    while sidx > 0:
        sidx = q.rfind(':')
        if sidx == -1:
            sidx = -1
        diff = len(q)-1 - sidx
        #print >>sys.stderr,"sidx",sidx
        elem = q[sidx+1:]
        #print >>sys.stderr,"inet_pton: elem",elem,diff
        if diff < 4:
            pad = '0' * (4-diff)
            newelem = pad+elem
        else:
            newelem = elem
        s = newelem+s
        
        #print >>sys.stderr,"inet_pton: while s",s
        q = q[0:sidx]

    if len(s) != 32:
        diff = 32-len(s)
        ndiff = diff / 4
        pre = '0000' * ndiff
        s = pre+s
        
    #print >>sys.stderr,"inet_pton: s",s
    return binascii.unhexlify(s)

#
# ProtocolOptions
#

POPT_VER_TYPE = '\x00'
POPT_VER_SWIFT = '\x00'
POPT_VER_PPSP = '\x01'

POPT_MIN_VER_TYPE = '\x01'
POPT_SWARMID_TYPE = '\x02'
POPT_CIPM_TYPE = '\x03'
POPT_MHF_TYPE = '\x04'
POPT_LSA_TYPE = '\x05'
POPT_CAM_TYPE = '\x06'
POPT_LDW_TYPE = '\x07'
POPT_MSGS_TYPE = '\x08'
POPT_END_TYPE = '\xff'

POPT_CIPM_NONE = '\x00'
POPT_CIPM_MERKLE = '\x01'
POPT_CIPM_SIGNALL = '\x02'
POPT_CIPM_UNIFIED_MERKLE = '\x03'

POPT_MHF_SHA1 = '\x00'
POPT_MHF_SHA224 = '\x01'
POPT_MHF_SHA256 = '\x02'
POPT_MHF_SHA384 = '\x03'
POPT_MHF_SHA512 = '\x04'

# POPT_CHUNK_ADDR_BIN32, see CHUNK_SPEC_ID_BIN32

POPT_LSA_DH = '\x02'
POPT_LSA_DSA = '\x03'
POPT_LSA_RSASHA1 = '\x05'
POPT_LSA_DSA_NSEC3_SHA1 = '\x06'
POPT_LSA_RSASHA1_NSEC3_SHA1 = '\x07'
POPT_LSA_RSASHA256 = '\x08'
POPT_LSA_RSASHA512 = '\x0a'
POPT_LSA_ECC_GOST =   '\x0c'
POPT_LSA_ECDSAP256SHA256 = '\x0d'
POPT_LSA_ECDSAP384SHA384 = '\x0e'
POPT_LSA_PRIVATEDNS = '\xfd'     

# SIGNPEAKTODO
DUMMY_DEFAULT_SIG_LENGTH  = 20

#
# Messages
#


MSG_ID_HANDSHAKE = '\x00'
MSG_ID_DATA = '\x01'
MSG_ID_ACK = '\x02'
MSG_ID_HAVE = '\x03'
MSG_ID_INTEGRITY = '\x04'
MSG_ID_PEX_RESv4 = '\x05'
MSG_ID_PEX_REQ = '\x06'
MSG_ID_SIGNED_INTEGRITY = '\x07'
MSG_ID_REQUEST = '\x08'
MSG_ID_CANCEL = '\x09'
MSG_ID_CHOKE = '\x0a'
MSG_ID_UNCHOKE = '\x0b'
MSG_ID_PEX_RESv6 = '\x0c'
MSG_ID_PEX_REScert = '\x0d'





class HandshakeMessage(Encodable):
    def __init__(self,chanid,ver,minver=None,swarmid=None,cipm=None,mhf=None,lsa=None,cam=None,ldw=None,msgdata=None):
        self.chanid = chanid
        self.ver = ver
        self.minver = minver
        self.swarmid = swarmid
        self.cipm = cipm
        self.mhf = mhf
        self.lsa = lsa
        self.cam = cam
        self.ldw = ldw
        self.msgdata = msgdata
    def to_bytes(self):
        chain = []
        chain.append(HandshakeMessage.get_id())
        chain.append(self.chanid.to_bytes())
        if self.ver == POPT_VER_PPSP:
            # TODO, make each ProtocolOption an Encodable
            chain.append(POPT_VER_TYPE)
            chain.append(self.ver)
            if self.minver is not None:
                chain.append(POPT_MIN_VER_TYPE)
                chain.append(self.minver)
            if self.swarmid is not None:
                chain.append(POPT_SWARMID_TYPE)
                s = len(self.swarmid)
                sbytes = struct.pack(">H",s)
                if len(sbytes) != 2:
                    print >>sys.stderr,"HandshakeMessage: SWARM SIZE WRONG PACK"
                chain.append(sbytes)
                chain.append(self.swarmid)
            if self.cipm is not None:
                chain.append(POPT_CIPM_TYPE)
                chain.append(self.cipm)
            if self.mhf is not None:
                chain.append(POPT_MHF_TYPE)
                chain.append(self.mhf)
            if self.lsa is not None:
                chain.append(POPT_LSA_TYPE)
                chain.append(self.lsa)
            if self.cam is not None:
                chain.append(POPT_CAM_TYPE)
                chain.append(self.cam)
            if self.ldw is not None:
                chain.append(POPT_LDW_TYPE)
                chain.append(self.ldw)
            if self.msgdata is not None:
                chain.append(POPT_MSGDATA_TYPE)
                s = len(msgdata)
                sbytes = struct.pack(">H",s)
                chain.append(sbytes)
                chain.append(self.msgdata)
            chain.append(POPT_END_TYPE)
        return "".join(chain)
    
    def from_bytes(t,bytes,off):
        off += 1
        chanid = ChannelID.from_bytes( bytes[off:off+ChannelID.get_bytes_length()])
        #print >>sys.stderr,"hs:",`chanid`
        off += chanid.get_bytes_length()
        ver = None
        minver = None
        swarmid = None
        cipm = None
        mhf = None
        lsa = None
        cam = None
        ldw = None
        msgdata = None
        if t.get_version() == POPT_VER_PPSP:
            while off < len(bytes):
                popt = bytes[off]
                
                #print >>sys.stderr,"hs: popt is",`popt`
                off += 1
                if popt == POPT_VER_TYPE:
                    ver = bytes[off]
                    off += 1
                elif popt == POPT_MIN_VER_TYPE:
                    minver = bytes[off]
                    off += 1
                elif popt == POPT_SWARMID_TYPE:
                    sbytes = bytes[off:off+2]
                    off += len(sbytes)
                    [s] = struct.unpack(">H",sbytes)
                    swarmid = bytes[off:off+s]
                    off += len(swarmid)
                elif popt == POPT_CIPM_TYPE:
                    cipm = bytes[off]
                    off += 1
                elif popt == POPT_MHF_TYPE:
                    mhf = bytes[off]
                    off += 1
                elif popt == POPT_LSA_TYPE:
                    lsa = bytes[off]
                    off += 1
                elif popt == POPT_CAM_TYPE:
                    cam = bytes[off]
                    off += 1
                elif popt == POPT_LDW_TYPE:
                    ldwsize = 8
                    if cam == CHUNK_SPEC_ID_BIN32 or cam == CHUNK_SPEC_ID_CHUNK32:
                        ldwsize = 4
                    ldw = bytes[off:off+ldwsize] 
                    off += ldwsize
                elif popt == POPT_MSGS_TYPE:
                    sbytes = bytes[off:off+1]
                    off += len(sbytes)
                    [s] = struct.unpack(">H",sbytes)
                    msgdata = bytes[off:off+s]
                    off += len(msgdata)
                elif popt == POPT_END_TYPE:
                    break
        
        return [HandshakeMessage(chanid,ver,minver,swarmid,cipm,mhf,lsa,cam,ldw,msgdata),off]
    
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_HANDSHAKE
    get_id = staticmethod(get_id)
    def __str__(self):
        return "HANDSHAKE(ver="+`self.ver`+"minver="+`self.minver`+",sid="+`self.swarmid`+",cam="+`self.cam`+")"





class DataMessage(Encodable):
    def __init__(self,chunkspec,timestamp,chunk): # timestamp MUST be None for POPT_VER_SWIFT
        self.chunkspec = chunkspec
        self.ts = timestamp
        self.chunk  = chunk
    def to_bytes(self):
        if self.ts is None:
            chain = [DataMessage.get_id(),self.chunkspec.to_bytes(),self.chunk]
        else:
            chain = [DataMessage.get_id(),self.chunkspec.to_bytes(),self.ts.to_bytes(),self.chunk]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        tsbytes = bytes[off:off+TimeStamp.get_bytes_length()]
        ts = TimeStamp.from_bytes(tsbytes)
        off += len(tsbytes)
        chunk = bytes[off:]
        off = len(bytes)
        return [DataMessage(chunkspec,ts,chunk),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_DATA
    get_id = staticmethod(get_id)
    def __str__(self):
        return "DATA("+`self.chunkspec`+",ts,chunk)"


class AckMessage(Encodable):
    def __init__(self,chunkspec,timestamp):
        self.chunkspec = chunkspec
        self.ts = timestamp
    def to_bytes(self):
        chain = [AckMessage.get_id(),self.chunkspec.to_bytes(),self.ts.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        tsbytes = bytes[off:off+TimeStamp.get_bytes_length()]
        ts = TimeStamp.from_bytes(tsbytes)
        off += len(tsbytes)
        return [AckMessage(chunkspec,ts),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_ACK
    get_id = staticmethod(get_id)
    def __str__(self):
        return "ACK("+`self.chunkspec`+",ts)"


class HaveMessage(Encodable):
    def __init__(self,chunkspec):
        self.chunkspec = chunkspec
    def to_bytes(self):
        chain = [HaveMessage.get_id(),self.chunkspec.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        return [HaveMessage(chunkspec),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_HAVE
    get_id = staticmethod(get_id)
    def __str__(self):
        return "HAVE("+`self.chunkspec`+")"


class IntegrityMessage(Encodable):
    def __init__(self,chunkspec,intbytes):
        self.chunkspec = chunkspec
        self.intbytes  = intbytes
    def to_bytes(self):
        chain = [IntegrityMessage.get_id(),self.chunkspec.to_bytes(),self.intbytes]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        intbytes = bytes[off:off+t.get_hash_length()]
        off += len(intbytes)
        return [IntegrityMessage(chunkspec,intbytes),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_INTEGRITY
    get_id = staticmethod(get_id)
    def __str__(self):
        return "INTEGRITY("+`self.chunkspec`+",intbytes)"

class PexResv4Message(Encodable):
    def __init__(self,ipp):
        self.ipp = ipp
    def to_bytes(self):
        chain = [PexResv4Message.get_id(),self.ipp.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        ippbytes = bytes[off:off+IPv4Port.get_bytes_length()]
        off += len(ippbytes)
        ipp = IPv4Port.from_bytes(ippbytes)
        return [PexResv4Message(ipp),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_PEX_RESv4
    get_id = staticmethod(get_id)
    def __str__(self):
        return "PEX_RESv4("+`self.ipp`+")"

class PexReqMessage(Encodable):
    def __init__(self):
        pass
    def to_bytes(self):
        chain = [PexReqMessage.get_id()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        return [PexReqMessage(),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 1 # just MSG_ID
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_PEX_REQ
    get_id = staticmethod(get_id)
    def __str__(self):
        return "PEX_REQ()"


class SignedIntegrityMessage(Encodable):
    def __init__(self,t,chunkspec,intbytes):
        self.chunkspec = chunkspec
        self.intbytes  = intbytes
    def to_bytes(self):
        chain = [SignedIntegrity.get_id(),self.chunkspec.to_bytes(),self.intbytes]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        # if t.lsa: siglen =  SIGNPEAKTODO 
        intbytes = bytes[off:off+DUMMY_DEFAULT_SIG_LENGTH]
        off += len(intbytes)
        return [SignedIntegrityMessage(t,chunkspec,intbytes),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_SIGNED_INTEGRITY
    get_id = staticmethod(get_id)
    def __str__(self):
        return "SIGNED_INTEGRITY("+`self.chunkspec`+",intbytes)"


class RequestMessage(Encodable):
    def __init__(self,chunkspec):
        self.chunkspec = chunkspec
    def to_bytes(self):
        chain = [RequestMessage.get_id(),self.chunkspec.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        return [RequestMessage(chunkspec),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_REQUEST
    get_id = staticmethod(get_id)
    def __str__(self):
        return "REQUEST("+`self.chunkspec`+")"

class CancelMessage(Encodable):
    def __init__(self,chunkspec):
        self.chunkspec = chunkspec
    def to_bytes(self):
        chain = [CancelMessage.get_id(),self.chunkspec.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        cabytes = bytes[off:off+t.get_chunkspec().get_bytes_length()]
        off += len(cabytes)
        chunkspec = t.chunkspec.from_bytes(cabytes)
        return [CancelMessage(chunkspec),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_CANCEL
    get_id = staticmethod(get_id)
    def __str__(self):
        return "CANCEL("+`self.chunkspec`+")"


class ChokeMessage(Encodable):
    def __init__(self):
        pass
    def to_bytes(self):
        chain = [ChokeMessage.get_id()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        return [ChokeMessage(),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 1 # just MSG_ID
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_CHOKE
    get_id = staticmethod(get_id)
    def __str__(self):
        return "CHOKE()"


class UnchokeMessage(Encodable):
    def __init__(self):
        pass
    def to_bytes(self):
        chain = [UnchokeMessage.get_id()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        off += 1
        return [UnchokeMessage(),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 1 # just MSG_ID
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_UNCHOKE
    get_id = staticmethod(get_id)
    def __str__(self):
        return "UNCHOKE()"



class PexResv6Message(Encodable):
    def __init__(self,ipp):
        self.ipp = ipp
    def to_bytes(self):
        chain = [PexResv6Message.get_id(),self.ipp.to_bytes()]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        ippbytes = bytes[off:off+IPv6Port.get_bytes_length()]
        off += len(ippbytes)
        ipp = IPv6Port.from_bytes(cabytes)
        return [PexResv6Message(ipp),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to chunkspec
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_PEX_RESv6
    get_id = staticmethod(get_id)
    def __str__(self):
        return "PEX_RESv6("+`self.ipp`+")"


class PexResCertMessage(Encodable):
    def __init__(self,certbytes):
        self.certbytes = certbytes
    def to_bytes(self):
        sbytes = struct.pack(">H",len(self.certbytes))
        chain = [PexResCertMessage.get_id(),sbytes,self.certbytes]
        return "".join(chain)
    def from_bytes(t,bytes,off):
        sbytes = bytes[off:off+1]
        off += len(sbytes)
        [s] = struct.unpack(">H",sbytes)
        certbytes = bytes[2:2+s]
        off += len(certbytes)
        return [PexRescertMessage(certbytes),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return None # variable due to certificate
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return MSG_ID_PEX_REScert
    get_id = staticmethod(get_id)
    def __str__(self):
        return "PEX_REScert(cert)"


class KeepAliveMessage(Encodable):
    def __init__(self):
        pass 
    def to_bytes(self):
        return ""
    def from_bytes(t,bytes,off):
        return [KeepAliveMessage(),off]
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 0
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():
        return -1
    get_id = staticmethod(get_id)
    def __str__(self):
        return "KEEPALIVE"


#
# Transfer
#


class Transfer:
    
    def __init__(self,swarmid,version,chunkspec,cipm,lsa,socket):
        self.swarmid = swarmid
        self.version = version
        self.chunkspec = chunkspec
        self.cipm = cipm
        self.lsa = lsa
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



class ChannelID(Encodable):
    def __init__(self,id):
        self.id = id
    def to_bytes(self):
        return self.id
    def from_bytes(bytes):
        id = bytes[0:4]
        return ChannelID(id)
    from_bytes = staticmethod(from_bytes)
    def get_bytes_length():
        return 4
    get_bytes_length = staticmethod(get_bytes_length)
    def get_id():     
        return None
    get_id = staticmethod(get_id)
    def __str__(self):
        return "ChannelID("+`self.id`+")"

    
#
# Datagram
#

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
        
    def add(self,e):
        self.chain.append(e)
        
    def to_bytes(self):
        wire = ''
        for e in self.chain:
            #print >>sys.stderr,"dgram: Add",`e`
            wire += e.to_bytes()
        return wire

    def get_channel_id(self):
        #print >>sys.stderr,"dgram: get_channel_id"
        x = ChannelID.from_bytes( self.data[0:ChannelID.get_bytes_length()])
        self.off += ChannelID.get_bytes_length()
        return x

    def get_message(self):
        if self.off == len(self.data):
            return None
            
        msgid = self.data[self.off:self.off+len(MSG_ID_HANDSHAKE)]
        #print >>sys.stderr,"dgram: get_message: GOT msgid",`msgid`
        
        if msgid == MSG_ID_HANDSHAKE:
            [msg,self.off] = HandshakeMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_DATA:
            [msg,self.off] = DataMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_ACK:
            [msg,self.off] = AckMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_HAVE:
            [msg,self.off] = HaveMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_INTEGRITY:
            [msg,self.off] = IntegrityMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_PEX_RESv4:
            [msg,self.off] = PexResv4Message.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_PEX_REQ:
            [msg,self.off] = PexReqMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_SIGNED_INTEGRITY:
            [msg,self.off] = SignedIntegrityMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_REQUEST:
            [msg,self.off] = RequestMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_CANCEL:
            [msg,self.off] = CancelMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_CHOKE:
            [msg,self.off] = ChokeMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_UNCHOKE:
            [msg,self.off] = UnchokeMessage.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_PEX_RESv6:
            [msg,self.off] = PexResv6Message.from_bytes(self.t,self.data,self.off)
        elif msgid == MSG_ID_PEX_REScert:
            [msg,self.off] = PexRescertMessage.from_bytes(self.t,self.data,self.off)
        else:
            print >>sys.stderr,"dgram: get_message: unknown msgid",`msgid`
            msg = None
        return msg
    

CONN_STATE_INIT = 0
CONN_STATE_WAIT4HIS = 1
CONN_STATE_WAIT4MINE = 2
CONN_STATE_ESTABLISHED = 3 

CHAN_ID_ZERO = ChannelID.from_bytes('\x00' * 4)


DGRAM_MAX_RECV = 65536

class Channel:
    def __init__(self,t,addr,localinit):
        self.t = t
        self.addr = addr
        self.localinit = localinit
        self.mychanid = ChannelID.from_bytes('6778')
        self.hischanid = CHAN_ID_ZERO 
        
    def send(self,d):
        self.t.get_socket().sendto(d,self.addr)
        
    def recv(self,d):
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"chan: Parsed",`msg`
            if msg.get_id() == MSG_ID_HANDSHAKE:
                self.hischanid = msg.chanid
      
    def get_my_chanid(self):
        return self.mychanid
      
    def get_his_chanid(self):
        return self.hischanid
    
    def set_his_chanid(self,chanid):
        self.hischanid = chanid

        
        
class Socket:
    def __init__(self,myaddr,family=socket.AF_INET):
        self.myaddr = myaddr
        socket.setdefaulttimeout(10.0)
        self.sock = socket.socket(family, socket.SOCK_DGRAM)
        print >>sys.stderr,"Socket: __init__: bind",myaddr,family
        self.sock.bind(myaddr)

    def recv(self):
        [data,addr] = self.sock.recvfrom(DGRAM_MAX_RECV)
        print >>sys.stderr,"Socket: recv len",len(data)

        return [addr,Datagram(None,data)]
        
    def sendto(self,d,addr):
        data = d.to_bytes()
        return self.sock.sendto(data,addr)
    
    def listen(self,swarmid,autochanid=True,hs=True):
        [addr,d] = self.recv()
        s = SwiftConnection(self.myaddr,addr,swarmid,listensock=self,hs=hs)
        if autochanid:
            chanid = d.get_channel_id()
            print >>sys.stderr,"Socket: listen: Got ChannelID",`chanid`
        d.set_t(s.t)
        return [s,d]
        
        
class SwiftConnection:
    def __init__(self,myaddr,hisaddr,swarmid,listensock=None,hs=True,ver=POPT_VER_PPSP,minver=POPT_VER_PPSP,cipm=POPT_CIPM_MERKLE,mhf=POPT_MHF_SHA1,lsa=None,cam=CHUNK_SPEC_ID_CHUNK32,ldw=None,msgdata=None,chunkspec=ChunkRange(0,0)):
        
        if listensock is None:
            self.s = Socket(myaddr)
        else:
            self.s = listensock
        #t = Transfer(swarmid,POPT_VER_PPSP,BIN_ALL,self.s)
        self.t = Transfer(swarmid,ver,chunkspec,cipm,lsa,self.s)
        self.c = Channel(self.t,hisaddr,listensock is None)
        
        if hs:
            d = Datagram(self.t)
            if self.t.version == POPT_VER_SWIFT:
                d.add( self.c.get_his_chanid() )
                d.add( IntegrityMessage(BIN_ALL,self.t.get_swarm_id()) )
                d.add( HandshakeMessage(self.c.get_my_chanid(),POPT_VER_SWIFT) )
                self.c.send(d)
            else:
                d.add( self.c.get_his_chanid() )
                d.add( HandshakeMessage(self.c.get_my_chanid(),ver=ver,minver=minver,swarmid=self.t.get_swarm_id(),cipm=cipm,mhf=mhf,lsa=lsa,cam=cam,ldw=ldw,msgdata=msgdata) )
                self.c.send(d)

    def makeDatagram(self,autochanid=True):
        d = Datagram(self.t)
        if autochanid:
            d.add( self.c.get_his_chanid() )
        return d

    def send(self,d):
        self.c.send(d)
        
    def recv(self,autochanid=True):
        [addr,d] = self.s.recv()
        d.set_t(self.t)
        if autochanid:
            chanid = d.get_channel_id()
        return d



if __name__ == "__main__":
    
    x = inet_pton6("::1")
    print >>sys.stderr,"FINAL",`x`

    #x = inet_pton6("2001:0:5ef5:79fb:385c:2235:3f57:ff99")
    #print >>sys.stderr,"FINAL",`x`

    #x = inet_pton6("fe80::385c:2235:3f57:ff99")
    #print >>sys.stderr,"FINAL",x

    #x = inet_pton6("::ffff:130.37.193.64")
    #print >>sys.stderr,"FINAL",x

    
