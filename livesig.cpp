/*
 *  livesig.h
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#include "swift.h"

#include <event2/buffer.h>

using namespace swift;

/*
 * Global class variables
 */

const Signature Signature::NOSIG = Signature();
const SwarmPubKey SwarmPubKey::NOSPUBKEY = SwarmPubKey();


/*
 * Local functions (dummy implementations when compiled without OpenSSL
 */

static EVP_MD_CTX *opensslrsa_createctx();
static void opensslrsa_destroyctx(EVP_MD_CTX *evp_md_ctx);
static int opensslrsa_adddata(EVP_MD_CTX *evp_md_ctx, unsigned char *data, unsigned int datalength);
static int opensslrsa_sign(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, struct evbuffer *evb);
static int opensslrsa_verify2(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, int maxbits, unsigned char *sig, unsigned int siglen);
static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback);
static struct evbuffer *opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey);
static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb);



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
    {
	evp = opensslrsa_generate(keysize, 0, callback);
    }
    else
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
	evb = opensslrsa_todns(evb,evp_);

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

    fprintf(stderr,"SIGN len %u ", evbuffer_get_length(evb) );
    for (int i=0; i<evbuffer_get_length(evb); i++)
    {
	fprintf(stderr,"%02x ",sigdata[i] );
    }
    fprintf(stderr,"\n");

    evbuffer_free(evb);
    opensslrsa_destroyctx(ctx);
    return sig;
}


bool KeyPair::Verify(uint8_t *data, uint16_t datalength,Signature &sig)
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

    fprintf(stderr,"VERIFY len %u ", sig.length() );
    uint8_t *sigdata = sig.bits();
    for (int i=0; i<sig.length(); i++)
    {
	fprintf(stderr,"%02x ",sigdata[i] );
    }
    fprintf(stderr,"\n");

    ret = opensslrsa_verify2(evp_,ctx,0,sig.bits(),sig.length());

    fprintf(stderr,"KeyPair::Verify ret %d\n", ret );

    opensslrsa_destroyctx(ctx);
    return (ret == 1);
}


uint16_t KeyPair::GetSigSizeInBytes()
{
    return EVP_PKEY_size(evp_);
}



/*
 * Signature
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

    EVP_PKEY *evp = opensslrsa_fromdns(evb);
    if (evp == NULL)
	return NULL;

    return new KeyPair(alg,evp);
}


/*
 * Implementations of crypto
 */


#ifdef OPENSSL

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
    unsigned char *sig = new unsigned char[EVP_PKEY_size(pkey)];
    if (sig == NULL)
	return 0;

    if (!EVP_SignFinal(evp_md_ctx, sig, &siglen, pkey)) {
	delete sig;
	return 0;
    }

    evbuffer_add(evb,sig,siglen);
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

    fprintf(stderr,"opensslrsa_verify2: evp %p maxbits %d siglen %u\n", pkey, maxbits, siglen );
    for (int i=0; i<siglen; i++)
    {
	fprintf(stderr,"%02X ",sigdata[i] );
    }
    fprintf(stderr,"\n");


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
    BN_GENCB_set(&cb, &generate_progress_cb, callback);

    if (RSA_generate_key_ex(rsa, keysize, e, &cb)) {
	    BN_free(e);
	    SET_FLAGS(rsa);
	    RSA_free(rsa);
	    return pkey;
    }
}


static struct evbuffer *opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    unsigned int e_bytes;
    unsigned int mod_bytes;
    RSA *rsa;

    if (pkey == NULL)
	return evb;
    rsa = EVP_PKEY_get1_RSA(pkey);
    if (rsa == NULL)
	return evb;

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

    return evb;
}


static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb)
{
    RSA *rsa;
    unsigned int e_bytes;
    EVP_PKEY *pkey;

    fprintf(stderr,"fromdns: total %u\n", evbuffer_get_length(evb));

    rsa = RSA_new();
    if (rsa == NULL)
	return NULL;
    SET_FLAGS(rsa);

    if (evbuffer_get_length(evb) < 1) {
	RSA_free(rsa);
	return NULL;
    }
    e_bytes = evbuffer_remove_8(evb);

    fprintf(stderr,"fromdns: e_bytes is %u\n", e_bytes );

    // RFC3110
    if (e_bytes == 0) {
	if (evbuffer_get_length(evb) < 2) {
	    RSA_free(rsa);
	    return NULL;
	}
	e_bytes = evbuffer_remove_16be(evb);

	fprintf(stderr,"fromdns: big e_bytes is %u\n", e_bytes );
    }

    if (evbuffer_get_length(evb) < e_bytes) {
	    RSA_free(rsa);
	    return NULL;
    }

    uint8_t *bindata = new uint8_t[e_bytes];

    evbuffer_remove(evb,bindata,e_bytes);
    rsa->e = BN_bin2bn(bindata, e_bytes, NULL);


    fprintf(stderr,"fromdns: e bytes %x %x %x\n", bindata[0], bindata[1], bindata[2] );

    delete bindata;

    unsigned int n_bytes = evbuffer_get_length(evb);

    fprintf(stderr,"fromdns: n_bytes is %u\n", n_bytes );

    bindata = new uint8_t[n_bytes];

    evbuffer_remove(evb,bindata,n_bytes);
    rsa->n = BN_bin2bn(bindata, n_bytes, NULL);

    fprintf(stderr,"fromdns: n bytes %x %x %x\n", bindata[0], bindata[1], bindata[2] );

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

#else

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
static int opensslrsa_verify2(EVP_PKEY *pkey, EVP_MD_CTX *evp_md_ctx, int maxbits, unsigned char *sig, unsigned int siglen)
{
    return 0;
}
static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback)
{
    return NULL;
}
static struct evbuffer *opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey)
{
    return NULL;
}
static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb)
{
    return NULL;
}

#endif
