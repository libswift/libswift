/*
 *  livesig.h
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#include "swift.h"

#include <event2/buffer.h>

#ifdef OPENSSL

#include <openssl/rsa.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/pem.h> // for file I/O

// To prevent runtime error OPENSSL_Uplink(10111000,08): no OPENSSL_Applink
#include <openssl/applink.c>
#endif

using namespace swift;

// From lib/dns/include/dns/keyvalues.h

#define DNS_SIG_ECDSA256SIZE	64
#define DNS_SIG_ECDSA384SIZE	96

#define DNS_KEY_ECDSA256SIZE	64
#define DNS_KEY_ECDSA384SIZE	96


/*
 * Global class variables
 */

const Signature Signature::NOSIG = Signature();
const SwarmPubKey SwarmPubKey::NOSPUBKEY = SwarmPubKey();


/*
 * Local functions (dummy implementations when compiled without OpenSSL
 */

static int fake_openssl_write_private_key(std::string keypairfilename, EVP_PKEY *pkey);
static EVP_PKEY *fake_openssl_read_private_key(std::string keypairfilename, popt_live_sig_alg_t *algptr);

// RSA
static EVP_MD_CTX *opensslrsa_createctx();
static void opensslrsa_destroyctx(EVP_MD_CTX *evp_md_ctx);
static int opensslrsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength);
static int opensslrsa_sign(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb);
static int opensslrsa_verify2(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, int maxbits, unsigned char *sigdata, unsigned int siglen);
static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback);
static int opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey);
static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb);

//ECDSA
static EVP_MD_CTX *opensslecdsa_createctx(popt_live_sig_alg_t alg);
static void opensslecdsa_destroyctx(EVP_MD_CTX *evp_md_ctx);
static int opensslecdsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength);
static int BN_bn2bin_fixed(BIGNUM *bn, unsigned char *buf, int size);
static int opensslecdsa_sign(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb);
static int opensslecdsa_verify(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, unsigned char *sigdata, unsigned int gotsiglen);
static EVP_PKEY *opensslecdsa_generate(popt_live_sig_alg_t alg,simple_openssl_callback_t callback);
static int opensslecdsa_todns(struct evbuffer *evb,EVP_PKEY *pkey);
static EVP_PKEY *opensslecdsa_fromdns(popt_live_sig_alg_t alg,struct evbuffer *evb);


/*
 * Signature
 */


Signature::Signature(uint8_t *sb, uint16_t len) : sigbits_(NULL), siglen_(0)
{
    if (len == 0)
        return;
    siglen_ = len;
    sigbits_ = new uint8_t[siglen_];
    memcpy(sigbits_,sb,siglen_);
}

Signature::Signature(const Signature &copy) : sigbits_(NULL), siglen_(0)
{
    if (copy.siglen_ == 0)
        return;

    siglen_ = copy.siglen_;
    sigbits_ = new uint8_t[siglen_];
    memcpy(sigbits_,copy.sigbits_,siglen_);
}

Signature::Signature(bool hex, const uint8_t *sb, uint16_t len)
{
    if (len == 0)
        return;
    if (hex)
    {
	siglen_ = len/2;
	sigbits_ = new uint8_t[siglen_];

        int val;
        for(int i=0; i<siglen_; i++)
        {
            if (sscanf((const char *)(sb+i*2), "%2x", &val)!=1)
            {
                memset(sigbits_,0,siglen_);
                return;
            }
            sigbits_[i] = val;
        }
        assert(this->hex()==std::string((const char *)sb));
    }
    else
    {
	siglen_ = len;
	sigbits_ = new uint8_t[siglen_];
	memcpy(sigbits_,sb,siglen_);
    }
}


Signature::~Signature()
{
    if (sigbits_ != NULL)
        delete sigbits_;
    sigbits_ = NULL;
}

Signature & Signature::operator= (const Signature & source)
{
    if (this != &source)
    {
        if (source.siglen_ == 0)
        {
            siglen_ = 0;
            if (sigbits_ != NULL)
                delete sigbits_;
            sigbits_ = NULL;
        }
        else
        {
            siglen_ = source.siglen_;
            sigbits_ = new uint8_t[source.siglen_];
            memcpy(sigbits_,source.sigbits_,source.siglen_);
        }
    }
    return *this;
}


std::string    Signature::hex() const {
    char *hex = new char[siglen_*2+1];
    for(int i=0; i<siglen_; i++)
        sprintf(hex+i*2, "%02x", (int)(unsigned char)sigbits_[i]);
    std::string s(hex,siglen_*2);
    delete hex;
    return s;
}


/*
 * KeyPair
 */

KeyPair *KeyPair::Generate( popt_live_sig_alg_t alg, uint16_t keysize, simple_openssl_callback_t callback)
{
    EVP_PKEY *evp = NULL;
    if (alg == POPT_LIVE_SIG_ALG_RSASHA1)
	evp = opensslrsa_generate(keysize, 0, callback);
    else
	evp = opensslecdsa_generate(alg,callback);

    if (evp == NULL)
	return NULL;

    KeyPair *kp = new KeyPair(alg,evp);
    return kp;
}


SwarmPubKey *KeyPair::GetSwarmPubKey()
{
    struct evbuffer *evb = evbuffer_new();

    // Add AlgorithmID
    evbuffer_add_8(evb,alg_);
    if (alg_ == POPT_LIVE_SIG_ALG_RSASHA1)
    {
	opensslrsa_todns(evb,evp_);
    }
    else
    {
	opensslecdsa_todns(evb,evp_);
    }

    if (evbuffer_get_length(evb) == 1)
    {
	evbuffer_free(evb);
	return NULL;
    }

    uint8_t *bindata = (uint8_t *)evbuffer_pullup(evb,evbuffer_get_length(evb));
    if (bindata == NULL)
    {
	evbuffer_free(evb);
	return NULL;
    }
    else
    {
	SwarmPubKey *spubkey = new SwarmPubKey(bindata,evbuffer_get_length(evb));
	evbuffer_free(evb);
	return spubkey;
    }
}



Signature *KeyPair::Sign(uint8_t *data, uint16_t datalength)
{
    if (alg_ == POPT_LIVE_SIG_ALG_RSASHA1)
    {
	EVP_MD_CTX *ctx = opensslrsa_createctx();
	if (ctx == NULL)
	    return NULL;

	int ret = opensslrsa_adddata(ctx,data,datalength);
	if (ret == 0)
	{
	    opensslrsa_destroyctx(ctx);
	    return NULL;
	}
	struct evbuffer *evb = evbuffer_new();
	ret = opensslrsa_sign(evp_,ctx, evb);
	if (ret == 0)
	{
	    evbuffer_free(evb);
	    opensslrsa_destroyctx(ctx);
	    return NULL;
	}

	Signature *sig = NULL;
	uint8_t *sigdata = (uint8_t *)evbuffer_pullup(evb,evbuffer_get_length(evb));
	if (sigdata != NULL)
	    sig = new Signature(sigdata,evbuffer_get_length(evb));

	evbuffer_free(evb);
	opensslrsa_destroyctx(ctx);
	return sig;
    }
    else
    {
	EVP_MD_CTX *ctx = opensslecdsa_createctx(alg_);
	if (ctx == NULL)
	    return NULL;

	int ret = opensslecdsa_adddata(ctx,data,datalength);
	if (ret == 0)
	{
	    opensslecdsa_destroyctx(ctx);
	    return NULL;
	}
	struct evbuffer *evb = evbuffer_new();
	ret = opensslecdsa_sign(alg_,evp_,ctx, evb);
	if (ret == 0)
	{
	    evbuffer_free(evb);
	    opensslecdsa_destroyctx(ctx);
	    return NULL;
	}

	Signature *sig = NULL;
	uint8_t *sigdata = (uint8_t *)evbuffer_pullup(evb,evbuffer_get_length(evb));
	if (sigdata != NULL)
	    sig = new Signature(sigdata,evbuffer_get_length(evb));

	evbuffer_free(evb);
	opensslecdsa_destroyctx(ctx);
	return sig;
    }
}


bool KeyPair::Verify(uint8_t *data, uint16_t datalength,Signature &sig)
{
    if (alg_ == POPT_LIVE_SIG_ALG_RSASHA1)
    {
	EVP_MD_CTX *ctx = opensslrsa_createctx();
	if (ctx == NULL)
	    return false;

	int ret = opensslrsa_adddata(ctx,data,datalength);
	if (ret == 0)
	{
	    opensslrsa_destroyctx(ctx);
	    return false;
	}

	ret = opensslrsa_verify2(evp_,ctx,0,sig.bits(),sig.length());
	opensslrsa_destroyctx(ctx);
	return (ret == 1);
    }
    else
    {
	EVP_MD_CTX *ctx = opensslecdsa_createctx(alg_);
	if (ctx == NULL)
	    return false;

	int ret = opensslecdsa_adddata(ctx,data,datalength);
	if (ret == 0)
	{
	    opensslecdsa_destroyctx(ctx);
	    return false;
	}

	ret = opensslecdsa_verify(alg_,evp_,ctx,sig.bits(),sig.length());
	opensslrsa_destroyctx(ctx);
	return (ret == 1);
    }
}


uint16_t KeyPair::GetSigSizeInBytes()
{
    if (alg_ == POPT_LIVE_SIG_ALG_RSASHA1)
	return EVP_PKEY_size(evp_);
    else if (alg_ == POPT_LIVE_SIG_ALG_ECDSAP256SHA256) // EVP_PKEY_size reports +8 ?!
	return DNS_SIG_ECDSA256SIZE;
    else
	return DNS_SIG_ECDSA384SIZE;
}


KeyPair *KeyPair::ReadPrivateKey(std::string keypairfilename)
{
    popt_live_sig_alg_t alg=POPT_LIVE_SIG_ALG_PRIVATEDNS;
    EVP_PKEY *pkey = fake_openssl_read_private_key(keypairfilename,&alg);
    if (pkey == NULL)
	return NULL;
    else
	return new KeyPair(alg,pkey);
}

int KeyPair::WritePrivateKey(std::string keypairfilename)
{
    return fake_openssl_write_private_key(keypairfilename,evp_);
}


/*
 * SwarmPubKey
 */


SwarmPubKey::SwarmPubKey(uint8_t *bits, uint16_t len)
{
    if (len == 0)
	return;
    len_ = len;
    bits_ = new uint8_t[len_];
    memcpy(bits_,bits,len_);
}

SwarmPubKey::SwarmPubKey(const SwarmPubKey& copy) : bits_(NULL), len_(0)
{
    if (copy.len_ == 0)
	return;

    len_ = copy.len_;
    bits_ = new uint8_t[len_];
    memcpy(bits_,copy.bits_,len_);
}

SwarmPubKey::SwarmPubKey(std::string hexstr)
{
    int val;
    uint16_t    len = hexstr.length()/2;
    char *hexcstr = new char[hexstr.length()+1];
    strcpy(hexcstr,hexstr.c_str());
    uint8_t *bits = new uint8_t[len];

    int i=0;
    for(i=0; i<len; i++)
    {
	if (sscanf(hexcstr+i*2, "%2x", &val)!=1)
	    break;
	bits[i] = val;
    }
    if (i == len)
    {
	bits_ = bits;
	len_ = len;
    }
    else
    {
	bits_ = NULL;
	len_ = 0;
	delete bits;
    }
    delete hexcstr;
}

SwarmPubKey::~SwarmPubKey()
{
    if (bits_ != NULL)
	delete bits_;
    bits_ = NULL;
}

SwarmPubKey & SwarmPubKey::operator= (const SwarmPubKey & source)
{
    if (this != &source)
    {
        if (source.len_ == 0)
        {
            len_ = 0;
            if (bits_ != NULL)
                delete bits_;
            bits_ = NULL;
        }
        else
        {
            len_ = source.len_;
            bits_ = new uint8_t[source.len_];
            memcpy(bits_,source.bits_,source.len_);
        }
    }
    return *this;
}

bool    SwarmPubKey::operator == (const SwarmPubKey& b) const
{ 
    if (len_ == 0 && b.len_ == 0)
        return true;
    else if (len_ != b.len_)
        return false;
    return 0==memcmp(bits_,b.bits_,len_); 
}

std::string SwarmPubKey::hex() const
{
    char *hex = new char[len_*2+1];
    for(int i=0; i<len_; i++)
	sprintf(hex+i*2, "%02x", (int)(unsigned char)bits_[i]);
    std::string s(hex,len_*2);
    delete hex;
    return s;
}

KeyPair *SwarmPubKey::GetPublicKeyPair() const
{
    if (len_ < 1)
	return NULL;

    popt_live_sig_alg_t alg = (popt_live_sig_alg_t)bits_[0];
    struct evbuffer *evb = evbuffer_new();
    evbuffer_add(evb,&bits_[1],len_-1);

    EVP_PKEY *evp = NULL;
    if (alg == POPT_LIVE_SIG_ALG_RSASHA1)
	evp = opensslrsa_fromdns(evb);
    else
	evp = opensslecdsa_fromdns(alg,evb);

    fprintf(stderr,"SwarmPubKey: GetPublicKeyPair: evp %p\n", evp );

    if (evp == NULL)
	return NULL;

    return new KeyPair(alg,evp);
}


/*
 * Implementations of crypto
 */

#ifdef OPENSSL


static int fake_openssl_write_private_key(std::string keypairfilename, EVP_PKEY *pkey)
{
    FILE *fp = fopen_utf8(keypairfilename.c_str(),"wb");
    if (fp == NULL)
	return -1;

    int ret = PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, 0, NULL);
    fclose(fp);
    if (ret == 0)
	return -1;
    else
	return 0;
}


static EVP_PKEY *fake_openssl_read_private_key(std::string keypairfilename, popt_live_sig_alg_t *algptr)
{
    FILE *fp = fopen_utf8(keypairfilename.c_str(),"rb");
    if (fp == NULL)
	return NULL;

    EVP_PKEY *pkey=NULL;
    pkey = PEM_read_PrivateKey(fp, &pkey, NULL, NULL );
    fclose(fp);

    if (pkey == NULL)
	return NULL;

    int keytype = EVP_PKEY_type(pkey->type);

    if (keytype == EVP_PKEY_RSA)
	*algptr = POPT_LIVE_SIG_ALG_RSASHA1;
    else if (keytype == EVP_PKEY_EC)
    {
	int siglen = EVP_PKEY_size(pkey);
	if (siglen == DNS_SIG_ECDSA256SIZE + 8 ) // EVP_PKEY_size reports +8 ?!
	    *algptr = POPT_LIVE_SIG_ALG_ECDSAP256SHA256;
	else if (siglen == DNS_SIG_ECDSA384SIZE + 8) // EVP_PKEY_size reports +8 ?!
	    *algptr = POPT_LIVE_SIG_ALG_ECDSAP384SHA384;
	else
	    fprintf(stderr,"fake_openssl_read_private_key: unknown siglen %d\n", siglen );
    }

    return pkey;
}


// Adapted from BIND9 opensslrsa_link.c
/*
 * Copyright (C) 2004-2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(RSA_FLAG_NO_BLINDING)
#define SET_FLAGS(rsa) \
	do { \
		(rsa)->flags &= ~RSA_FLAG_BLINDING; \
		(rsa)->flags |= RSA_FLAG_NO_BLINDING; \
	} while (0)
#else
#define SET_FLAGS(rsa) \
	do { \
		(rsa)->flags &= ~RSA_FLAG_BLINDING; \
	} while (0)
#endif



static EVP_MD_CTX *opensslrsa_createctx()
{
    EVP_MD_CTX *evp_md_ctx=NULL;
    const EVP_MD *type = EVP_sha1();	/* SHA1 + RSA */;
    evp_md_ctx = EVP_MD_CTX_create();
    if (evp_md_ctx == NULL)
	return NULL;

    if (!EVP_DigestInit_ex(evp_md_ctx, type, NULL)) {
	    EVP_MD_CTX_destroy(evp_md_ctx);
	    return NULL;
    }
    return evp_md_ctx;
}

static void
opensslrsa_destroyctx(EVP_MD_CTX *evp_md_ctx)
{
    if (evp_md_ctx != NULL) {
	EVP_MD_CTX_destroy(evp_md_ctx);
    }
}

static int
opensslrsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength)
{
    if (!EVP_DigestUpdate(evp_md_ctx, data, datalength)) {
	    return 0;
    }
    return 1;
}

static int
opensslrsa_sign(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb)
{
    unsigned int siglen = 0;
    unsigned char *sigdata = new unsigned char[EVP_PKEY_size(pkey)];
    if (sigdata == NULL)
	return 0;

    if (!EVP_SignFinal(evp_md_ctx, sigdata, &siglen, pkey)) {
	delete sigdata;
	return 0;
    }

    evbuffer_add(evb,sigdata,siglen);
    delete sigdata;

    return 1;
}


static int
opensslrsa_verify2(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, int maxbits, unsigned char *sigdata, unsigned int siglen)
{
    int status = 0;
    RSA *rsa;
    int bits;

    rsa = EVP_PKEY_get1_RSA(pkey);
    if (rsa == NULL)
	return 0;
    bits = BN_num_bits(rsa->e);
    RSA_free(rsa);
    if (bits > maxbits && maxbits != 0)
	return 0;

    return EVP_VerifyFinal(evp_md_ctx, sigdata, siglen, pkey);
}



static int generate_progress_cb(int p, int n, BN_GENCB *cb)
{
    simple_openssl_callback_t	func;

    if (cb->arg != NULL)
    {
	func = (simple_openssl_callback_t)cb->arg;
	func(p);
    }
    return 1;
}



static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback)
{
    BN_GENCB cb;
    RSA *rsa = RSA_new();
    BIGNUM *e = BN_new();
    EVP_PKEY *pkey = EVP_PKEY_new();

    if (rsa == NULL || e == NULL || pkey == NULL)
    {
	if (pkey != NULL)
	    EVP_PKEY_free(pkey);
	if (e != NULL)
	    BN_free(e);
	if (rsa != NULL)
	    RSA_free(rsa);
	return NULL;
    }
    if (!EVP_PKEY_set1_RSA(pkey, rsa))
    {
	if (pkey != NULL)
	    EVP_PKEY_free(pkey);
	if (e != NULL)
	    BN_free(e);
	if (rsa != NULL)
	    RSA_free(rsa);
	return NULL;
    }
    if (exp == 0) {
	    /* RSA_F4 0x10001 */
	    BN_set_bit(e, 0);
	    BN_set_bit(e, 16);
    } else {
	    /* (phased-out) F5 0x100000001 */
	    BN_set_bit(e, 0);
	    BN_set_bit(e, 32);
    }
    BN_GENCB_set(&cb, &generate_progress_cb, (void *)callback);

    if (RSA_generate_key_ex(rsa, keysize, e, &cb)) {
	    BN_free(e);
	    SET_FLAGS(rsa);
	    RSA_free(rsa);
	    return pkey;
    }
}


static int opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    unsigned int e_bytes;
    unsigned int mod_bytes;
    RSA *rsa;

    if (pkey == NULL)
	return 0;
    rsa = EVP_PKEY_get1_RSA(pkey);
    if (rsa == NULL)
	return 0;

    e_bytes = BN_num_bytes(rsa->e);
    mod_bytes = BN_num_bytes(rsa->n);

    // RFC3110
    if (e_bytes < 256) {	/*%< key exponent is <= 2040 bits */
	evbuffer_add_8(evb,(uint8_t) e_bytes);

	fprintf(stderr,"adding l 1\n");

    } else {
	evbuffer_add_8(evb,0);
	evbuffer_add_16be(evb,(uint16_t) e_bytes);

	fprintf(stderr,"adding l 3\n");
    }

    unsigned char *space = new unsigned char[BN_num_bytes(rsa->e)];
    BN_bn2bin(rsa->e, space);
    evbuffer_add(evb,space,BN_num_bytes(rsa->e));

    fprintf(stderr,"adding e bytes %x %x %x\n", space[0], space[1], space[2] );

    delete space;

    fprintf(stderr,"adding e %u\n", BN_num_bytes(rsa->e));

    space = new unsigned char[BN_num_bytes(rsa->n)];
    BN_bn2bin(rsa->n, space);
    evbuffer_add(evb,space,BN_num_bytes(rsa->n));

    fprintf(stderr,"adding n bytes %x %x %x\n", space[0], space[1], space[2] );

    delete space;

    fprintf(stderr,"adding n %u\n", BN_num_bytes(rsa->n));


    if (rsa != NULL)
	RSA_free(rsa);

    fprintf(stderr,"added total %u\n", evbuffer_get_length(evb));
    return 1;
}


static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb)
{
    RSA *rsa;
    unsigned int e_bytes;
    EVP_PKEY *pkey;

    rsa = RSA_new();
    if (rsa == NULL)
	return NULL;
    SET_FLAGS(rsa);

    if (evbuffer_get_length(evb) < 1) {
	RSA_free(rsa);
	return NULL;
    }
    e_bytes = evbuffer_remove_8(evb);

    // RFC3110
    if (e_bytes == 0) {
	if (evbuffer_get_length(evb) < 2) {
	    RSA_free(rsa);
	    return NULL;
	}
	e_bytes = evbuffer_remove_16be(evb);
    }

    if (evbuffer_get_length(evb) < e_bytes) {
	    RSA_free(rsa);
	    return NULL;
    }

    uint8_t *bindata = new uint8_t[e_bytes];
    evbuffer_remove(evb,bindata,e_bytes);
    rsa->e = BN_bin2bn(bindata, e_bytes, NULL);
    delete bindata;

    unsigned int n_bytes = evbuffer_get_length(evb);
    bindata = new uint8_t[n_bytes];
    evbuffer_remove(evb,bindata,n_bytes);
    rsa->n = BN_bin2bn(bindata, n_bytes, NULL);
    delete bindata;

    pkey = EVP_PKEY_new();
    if (pkey == NULL) {
	    RSA_free(rsa);
	    return NULL;
    }
    if (!EVP_PKEY_set1_RSA(pkey, rsa)) {
	    EVP_PKEY_free(pkey);
	    RSA_free(rsa);
	    return NULL;
    }
    RSA_free(rsa);

    return pkey;
}


/*
 * ECDSA
 */

#ifndef NID_X9_62_prime256v1
#error "P-256 group is not known (NID_X9_62_prime256v1)"
#endif
#ifndef NID_secp384r1
#error "P-384 group is not known (NID_secp384r1)"
#endif


static EVP_MD_CTX *opensslecdsa_createctx(popt_live_sig_alg_t alg)
{
    EVP_MD_CTX *evp_md_ctx;
    const EVP_MD *type = NULL;

    evp_md_ctx = EVP_MD_CTX_create();
    if (evp_md_ctx == NULL)
	return NULL;
    if (alg == POPT_LIVE_SIG_ALG_ECDSAP256SHA256)
	type = EVP_sha256();
    else
	type = EVP_sha384();

    if (!EVP_DigestInit_ex(evp_md_ctx, type, NULL)) {
	    EVP_MD_CTX_destroy(evp_md_ctx);
	    return NULL;
    }

    return evp_md_ctx;
}

static void opensslecdsa_destroyctx(EVP_MD_CTX *evp_md_ctx)
{
    if (evp_md_ctx != NULL) {
	EVP_MD_CTX_destroy(evp_md_ctx);
    }
}

static int opensslecdsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength)
{
    if (!EVP_DigestUpdate(evp_md_ctx, data, datalength))
	return 0;

    return 1;
}

static int BN_bn2bin_fixed(BIGNUM *bn, unsigned char *buf, int size)
{
	int bytes = size - BN_num_bytes(bn);

	while (bytes-- > 0)
		*buf++ = 0;
	BN_bn2bin(bn, buf);
	return (size);
}

static int opensslecdsa_sign(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb)
{
    ECDSA_SIG *ecdsasig;
    EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(pkey);
    unsigned int dgstlen, siglen;
    unsigned char digest[EVP_MAX_MD_SIZE];

    if (eckey == NULL)
	return 0;

    if (alg == POPT_LIVE_SIG_ALG_ECDSAP256SHA256)
	siglen = DNS_SIG_ECDSA256SIZE;
    else
	siglen = DNS_SIG_ECDSA384SIZE;

    unsigned char *sigdata = new unsigned char[siglen];
    if (sigdata == NULL)
    {
	EC_KEY_free(eckey);
    	return 0;
    }

    if (!EVP_DigestFinal(evp_md_ctx, digest, &dgstlen))
    {
	EC_KEY_free(eckey);
	return 0;
    }

    ecdsasig = ECDSA_do_sign(digest, dgstlen, eckey);
    if (ecdsasig == NULL)
    {
	EC_KEY_free(eckey);
	return 0;
    }

    BN_bn2bin_fixed(ecdsasig->r, sigdata, siglen / 2);
    BN_bn2bin_fixed(ecdsasig->s, sigdata+siglen/2, siglen / 2);
    ECDSA_SIG_free(ecdsasig);

    evbuffer_add(evb,sigdata,siglen);
    delete sigdata;

    EC_KEY_free(eckey);

    return 1;
}

static int opensslecdsa_verify(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, unsigned char *sigdata, unsigned int gotsiglen)
{
    int status;
    unsigned char *cp = sigdata;
    ECDSA_SIG *ecdsasig = NULL;
    EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(pkey);
    unsigned int dgstlen, siglen;
    unsigned char digest[EVP_MAX_MD_SIZE];

    if (eckey == NULL)
	return 0;

    if (alg == POPT_LIVE_SIG_ALG_ECDSAP256SHA256)
	siglen = DNS_SIG_ECDSA256SIZE;
    else
	siglen = DNS_SIG_ECDSA384SIZE;

    if (gotsiglen != siglen)
    {
	EC_KEY_free(eckey);
	return 0;
    }

    if (!EVP_DigestFinal_ex(evp_md_ctx, digest, &dgstlen))
    {
	EC_KEY_free(eckey);
	return 0;
    }

    ecdsasig = ECDSA_SIG_new();
    if (ecdsasig == NULL)
    {
	EC_KEY_free(eckey);
	return 0;
    }
    if (ecdsasig->r != NULL)
	BN_free(ecdsasig->r);
    ecdsasig->r = BN_bin2bn(cp, siglen / 2, NULL);
    cp += siglen / 2;
    if (ecdsasig->s != NULL)
	BN_free(ecdsasig->s);
    ecdsasig->s = BN_bin2bn(cp, siglen / 2, NULL);
    /* cp += siglen / 2; */

    status = ECDSA_do_verify(digest, dgstlen, ecdsasig, eckey);

    if (ecdsasig != NULL)
	ECDSA_SIG_free(ecdsasig);
    if (eckey != NULL)
	EC_KEY_free(eckey);

    return status;
}


static EVP_PKEY *opensslecdsa_generate(popt_live_sig_alg_t alg,simple_openssl_callback_t callback)
{
    EVP_PKEY *pkey;
    EC_KEY *eckey = NULL;
    int group_nid;

    if (alg == POPT_LIVE_SIG_ALG_ECDSAP256SHA256)
	group_nid = NID_X9_62_prime256v1;
    else
	group_nid = NID_secp384r1;

    eckey = EC_KEY_new_by_curve_name(group_nid);
    if (eckey == NULL)
	return NULL;

    if (EC_KEY_generate_key(eckey) != 1)
    {
	EC_KEY_free(eckey);
	return NULL;
    }

    pkey = EVP_PKEY_new();
    if (pkey == NULL)
    {
	EC_KEY_free(eckey);
	return NULL;
    }

    if (!EVP_PKEY_set1_EC_KEY(pkey, eckey)) {
	EVP_PKEY_free(pkey);
	EC_KEY_free(eckey);
	return NULL;
    }

    if (eckey != NULL)
	EC_KEY_free(eckey);

    return pkey;
}


static int opensslecdsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    EC_KEY *eckey = NULL;
    int len;
    unsigned char *cp;
    unsigned char buf[DNS_KEY_ECDSA384SIZE + 1];

    eckey = EVP_PKEY_get1_EC_KEY(pkey);
    if (eckey == NULL)
	return 0;
    len = i2o_ECPublicKey(eckey, NULL);
    /* skip form */
    len--;

    cp = buf;
    if (!i2o_ECPublicKey(eckey, &cp))
    {
	EC_KEY_free(eckey);
	return 0;
    }

    evbuffer_add(evb,buf+1,len);

    EC_KEY_free(eckey);

    return 1;
}

static EVP_PKEY *opensslecdsa_fromdns(popt_live_sig_alg_t alg,struct evbuffer *evb)
{
    EVP_PKEY *pkey;
    EC_KEY *eckey = NULL;
    int group_nid;
    unsigned int len;
    const unsigned char *cp;
    unsigned char buf[DNS_KEY_ECDSA384SIZE + 1];

    fprintf(stderr,"ecdsa_fromdns: alg %d got %u\n", alg, evbuffer_get_length(evb) );

    if (alg == POPT_LIVE_SIG_ALG_ECDSAP256SHA256)
    {
	len = DNS_KEY_ECDSA256SIZE;
	group_nid = NID_X9_62_prime256v1;
    } else {
	len = DNS_KEY_ECDSA384SIZE;
	group_nid = NID_secp384r1;
    }

    fprintf(stderr,"ecdsa_fromdns: exp len %u\n", len );

    if (evbuffer_get_length(evb) == 0)
	return NULL;
    if (evbuffer_get_length(evb) < len)
	return NULL;

    fprintf(stderr,"ecdsa_fromdns: before by curve\n" );

    eckey = EC_KEY_new_by_curve_name(group_nid);
    if (eckey == NULL)
	return NULL;


    uint8_t *bindata = new uint8_t[len];
    evbuffer_remove(evb,bindata,len);

    buf[0] = POINT_CONVERSION_UNCOMPRESSED;
    memcpy(buf + 1, bindata, len);
    delete bindata;
    cp = buf;
    if (o2i_ECPublicKey(&eckey,
			(const unsigned char **) &cp,
			(long) len + 1) == NULL)
    {
	fprintf(stderr,"ecdsa_fromdns: GET LEN\n" );

	EC_KEY_free(eckey);
	return NULL;
    }

    pkey = EVP_PKEY_new();
    if (pkey == NULL)
    {
	fprintf(stderr,"ecdsa_fromdns: CREATE KEY\n" );
	EC_KEY_free(eckey);
	return NULL;
    }
    if (!EVP_PKEY_set1_EC_KEY(pkey, eckey))
    {
	fprintf(stderr,"ecdsa_fromdns: SET KEY\n" );

	EVP_PKEY_free(pkey);
	EC_KEY_free(eckey);
	return NULL;
    }

    EC_KEY_free(eckey);

    return pkey;
}


// No OPENSSL
#else

static int fake_openssl_write_private_key(std::string keypairfilename, EVP_PKEY *pkey)
{
    FILE *fp = fopen_utf8(keypairfilename.c_str(),"wb");
    if (fp == NULL)
	return -1;

    // Write dummy single byte private key
    int ret = fputc(pkey[0],fp);
    fclose(fp);

    if (ret == EOF)
	return -1;
    else
	return 0;
}


static EVP_PKEY *fake_openssl_read_private_key(std::string keypairfilename, popt_live_sig_alg_t *algptr)
{
    *algptr = POPT_LIVE_SIG_ALG_RSASHA1; // Not

    FILE *fp = fopen_utf8(keypairfilename.c_str(),"rb");
    if (fp == NULL)
	return NULL;

    EVP_PKEY *pkey = opensslrsa_generate(0,0,NULL);
    // Read dummy single byte private key
    int ret = fgetc(fp);
    if (ret == EOF)
    {
	delete pkey;
	return NULL;
    }
    else
	pkey[0] = ret;

    fclose(fp);
    return pkey;
}

static EVP_MD_CTX *opensslrsa_createctx()
{
    return NULL;
}
static void opensslrsa_destroyctx(EVP_MD_CTX *evp_md_ctx)
{
}
static int opensslrsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength)
{
    return 0;
}
static int opensslrsa_sign(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb)
{
    return 0;
}
static int opensslrsa_verify2(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, int maxbits, unsigned char *sigdata, unsigned int siglen)
{
    return 0;
}
static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback)
{
    // Create random 1 char key
    EVP_PKEY *pkey = new EVP_PKEY[1];
    pkey[0] = (EVP_PKEY)(rand() % 256);
    return pkey;
}
static int opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    // Repeat random 1 char key N times to get key
    for (int i=0; i<SWIFT_CIPM_NONE_KEYLEN; i++)
	evbuffer_add_8(evb,pkey[0]);
    return 1;
}
static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb)
{
    uint8_t val = evbuffer_remove_8(evb);
    EVP_PKEY *pkey = new EVP_PKEY[1];
    pkey[0] = val;
    return pkey;
}


static EVP_MD_CTX *opensslecdsa_createctx(popt_live_sig_alg_t alg)
{
    return NULL;
}
static void opensslecdsa_destroyctx(EVP_MD_CTX *evp_md_ctx)
{
}
static int opensslecdsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength)
{
    return 0;
}
static int BN_bn2bin_fixed(BIGNUM *bn, unsigned char *buf, int size)
{
    return 0;
}
static int opensslecdsa_sign(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb)
{
    return 0;
}
static int opensslecdsa_verify(popt_live_sig_alg_t alg,EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, unsigned char *sigdata, unsigned int gotsiglen)
{
    return 0;
}
static EVP_PKEY *opensslecdsa_generate(popt_live_sig_alg_t alg,simple_openssl_callback_t callback)
{
    // Create random 1 char key
    EVP_PKEY *pkey = new EVP_PKEY[1];
    pkey[0] = (EVP_PKEY)(rand() % 256);
    return pkey;
}
static int opensslecdsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    // Repeat random 1 char key N times to get key
    for (int i=0; i<SWIFT_CIPM_NONE_KEYLEN; i++)
	evbuffer_add_8(evb,pkey[0]);
    return 1;
}
static EVP_PKEY *opensslecdsa_fromdns(popt_live_sig_alg_t alg,struct evbuffer *evb)
{
    uint8_t val = evbuffer_remove_8(evb);
    EVP_PKEY *pkey = new EVP_PKEY[1];
    pkey[0] = val;
    return pkey;
}


#endif
