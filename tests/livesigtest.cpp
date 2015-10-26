/*
 *  livesigtest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 Vrije Universiteit Amsterdam. All rights reserved.
 *
 */
#include <gtest/gtest.h>
#include "swift.h"
#include "livesig.h"

#include <openssl/rsa.h>

using namespace swift;

void rsasha1_check_sign_verify(EVP_PKEY *evp, int keysize);

TEST(TLiveSig,RSASHA1Default)
{

    KeyPair *kp = KeyPair::Generate(POPT_LIVE_SIG_ALG_RSASHA1);
    ASSERT_FALSE(kp == NULL);

    rsasha1_check_sign_verify(kp->GetEVP(),SWIFT_RSA_DEFAULT_KEYSIZE);
}


TEST(TLiveSig,RSASHA1768)
{

    int keysize = 768;
    KeyPair *kp = KeyPair::Generate(POPT_LIVE_SIG_ALG_RSASHA1,keysize);
    ASSERT_FALSE(kp == NULL);

    rsasha1_check_sign_verify(kp->GetEVP(),keysize);
}


void rsasha1_check_sign_verify(EVP_PKEY *evp, int keysize)
{
    // From EVP_PKEY_sign manual page

    EVP_PKEY_CTX *ctx;
    unsigned char *md=NULL, *sig=NULL;
    size_t mdlen, siglen;

    EVP_PKEY *signing_key = evp;
    mdlen = 160/8; // sha1
    md = new unsigned char[mdlen];

    /* NB: assumes signing_key, md and mdlen are already set up
     * and that signing_key is an RSA private key
     */
    ctx = EVP_PKEY_CTX_new(signing_key,NULL);
    if (!ctx)
        ASSERT_TRUE(false);
    if (EVP_PKEY_sign_init(ctx) <= 0)
        ASSERT_TRUE(false);
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0)
        ASSERT_TRUE(false);
    if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha1()) <= 0)
        ASSERT_TRUE(false);

    /* Determine buffer length */
    if (EVP_PKEY_sign(ctx, NULL, &siglen, md, mdlen) <= 0)
        ASSERT_TRUE(false);

    fprintf(stderr,"siglen " PRISIZET "\n", siglen);
    ASSERT_EQ(keysize,siglen * 8);

    sig = (unsigned char *)OPENSSL_malloc(siglen);

    if (!sig)
        ASSERT_TRUE(false);

    // Arno: get max sig len
    if (EVP_PKEY_sign(ctx, sig, &siglen, md, mdlen) <= 0)
        ASSERT_TRUE(false);

    /* Signature is siglen bytes written to buffer sig */

    // From EVP_PKEY_verify manual page
    EVP_PKEY *verify_key = evp;

    EVP_PKEY_CTX *ctx2 = EVP_PKEY_CTX_new(verify_key,NULL);
    if (!ctx2)
        ASSERT_TRUE(false);
    if (EVP_PKEY_verify_init(ctx2) <= 0)
        ASSERT_TRUE(false);
    if (EVP_PKEY_CTX_set_rsa_padding(ctx2, RSA_PKCS1_PADDING) <= 0)
        ASSERT_TRUE(false);
    if (EVP_PKEY_CTX_set_signature_md(ctx2, EVP_sha1()) <= 0)
        ASSERT_TRUE(false);

    /* Perform operation */
    int ret = EVP_PKEY_verify(ctx2, sig, siglen, md, mdlen);

    /* ret == 1 indicates success, 0 verify failure and < 0 for some
     * other error.
     */
    ASSERT_EQ(1,ret);
}


TEST(TLiveSig,RSASHA1_SwarmLiveID)
{

    int keysize = SWIFT_RSA_DEFAULT_KEYSIZE;

    KeyPair *kp = KeyPair::Generate(POPT_LIVE_SIG_ALG_RSASHA1,keysize);
    ASSERT_FALSE(kp == NULL);

    SwarmPubKey *spubkey = kp->GetSwarmPubKey();
    int rsa_f4_exp_size_in_bytes = 3;
    ASSERT_EQ(1+1+rsa_f4_exp_size_in_bytes+keysize/8,spubkey->length());

    KeyPair *gotkp = spubkey->GetPublicKeyPair();

    // Compares public key components, gotkp only has pub.
    int ret = EVP_PKEY_cmp(kp->GetEVP(),gotkp->GetEVP());
    ASSERT_EQ(1,ret);
}


int main(int argc, char** argv)
{

    swift::LibraryInit();
    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;

}
