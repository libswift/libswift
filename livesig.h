/*
 *  crypto.h
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#ifndef SWIFT_LIVESIG_H_
#define SWIFT_LIVESIG_H_

#ifdef OPENSSL

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>

#endif

namespace swift {

// http://www.iana.org/assignments/dns-sec-alg-numbers/dns-sec-alg-numbers.xml
typedef enum {
	POPT_LIVE_SIG_ALG_DH = 2,
	POPT_LIVE_SIG_ALG_DSA = 3,
	POPT_LIVE_SIG_ALG_RSASHA1 = 5,
	POPT_LIVE_SIG_ALG_DSA_NSEC3_SHA1 = 6,
	POPT_LIVE_SIG_ALG_RSASHA1_NSEC3_SHA1 = 7,
	POPT_LIVE_SIG_ALG_RSASHA256 = 8,
	POPT_LIVE_SIG_ALG_RSASHA512 = 10,
	POPT_LIVE_SIG_ALG_ECC_GOST = 12,
	POPT_LIVE_SIG_ALG_ECDSAP256SHA256 = 13,
	POPT_LIVE_SIG_ALG_ECDSAP384SHA384 = 14,
	POPT_LIVE_SIG_ALG_PRIVATEDNS = 253     // supported. Hacks ECDSA with SHA1
} popt_live_sig_alg_t;


#ifdef OPENSSL

#define SWIFT_RSA_DEFAULT_KEYSIZE	1024

struct SwarmLiveID;

typedef void (*simple_openssl_callback_t)(int);

struct KeyPair
{
    popt_live_sig_alg_t	alg_;
    EVP_PKEY		*evp_;

    KeyPair(popt_live_sig_alg_t alg,EVP_PKEY *rsa)
    {
	alg_ = alg;
	evp_ = rsa;
    }
    ~KeyPair()
    {
	if (evp_ != NULL)
	    EVP_PKEY_free(evp_);
	evp_ = NULL;

    }

    static KeyPair *Generate(popt_live_sig_alg_t alg, uint16_t keysize=SWIFT_RSA_DEFAULT_KEYSIZE, simple_openssl_callback_t callback=NULL);
    EVP_PKEY       *GetEVP() { return evp_; }
    SwarmLiveID    *GetSwarmLiveID();
};


struct SwarmLiveID
{
    uint8_t	*bits_;
    uint16_t    len_;
    SwarmLiveID(uint8_t	*bits, uint16_t len)
    {
	if (len == 0)
	    return;
	len_ = len;
	bits_ = new uint8_t[len_];
	memcpy(bits_,bits,len_);
    }
    ~SwarmLiveID()
    {
	if (bits_ != NULL)
	    delete bits_;
	bits_ = NULL;
    }
    uint8_t  *bits()  { return bits_; }
    uint16_t length() { return len_; }
    std::string hex() const
    {
	char *hex = new char[len_*2+1];
	for(int i=0; i<len_; i++)
	    sprintf(hex+i*2, "%02x", (int)(unsigned char)bits_[i]);
	std::string s(hex,len_*2);
	delete hex;
	return s;
    }

    KeyPair *GetPublicKey();
};


typedef int privkey_t;
typedef Sha1Hash pubkey_t;


#else

typedef int privkey_t;
typedef Sha1Hash pubkey_t;

#endif

}

#endif /* SWIFT_LIVESIG_H_ */
