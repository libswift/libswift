/*
 *  livesig.h
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#include "swift.h"

#include <event2/buffer.h>

using namespace swift;

#ifdef OPENSSL

static EVP_PKEY *opensslrsa_generate(uint16_t keysize, int exp, simple_openssl_callback_t callback);
static struct evbuffer *opensslrsa_todns(struct evbuffer *evb,EVP_PKEY *pkey);
static EVP_PKEY *opensslrsa_fromdns(struct evbuffer *evb);

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


SwarmLiveID *KeyPair::GetSwarmLiveID()
{
    struct evbuffer *evb = evbuffer_new();

    // Add AlgorithmID
    evbuffer_add_8(evb,alg_);
    if (alg_ == POPT_LIVE_SIG_ALG_RSASHA1)
	evb = opensslrsa_todns(evb,evp_);

    if (evbuffer_get_length(evb) == 1)
	return NULL;

    uint8_t *bindata = (uint8_t *)evbuffer_pullup(evb,evbuffer_get_length(evb));
    if (bindata == NULL)
	return NULL;
    else
	return new SwarmLiveID(bindata,evbuffer_get_length(evb));
}


KeyPair *SwarmLiveID::GetPublicKey()
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



#endif
