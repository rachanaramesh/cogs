/* Copyright (c) 2001-2004, Roger Dingledine.
 * Copyright (c) 2004-2006, Roger Dingledine, Nick Mathewson.
 * Copyright (c) 2007-2011, The Tor Project, Inc. */
/* See LICENSE for licensing information */

#include "orconfig.h"
#define CRYPTO_PRIVATE
#include "or.h"
#include "test.h"

/** Run unit tests for Diffie-Hellman functionality. */
static void
test_crypto_dh(void)
{
  crypto_dh_env_t *dh1 = crypto_dh_new(DH_TYPE_CIRCUIT);
  crypto_dh_env_t *dh2 = crypto_dh_new(DH_TYPE_CIRCUIT);
  char p1[DH_BYTES];
  char p2[DH_BYTES];
  char s1[DH_BYTES];
  char s2[DH_BYTES];
  ssize_t s1len, s2len;

  test_eq(crypto_dh_get_bytes(dh1), DH_BYTES);
  test_eq(crypto_dh_get_bytes(dh2), DH_BYTES);

  memset(p1, 0, DH_BYTES);
  memset(p2, 0, DH_BYTES);
  test_memeq(p1, p2, DH_BYTES);
  test_assert(! crypto_dh_get_public(dh1, p1, DH_BYTES));
  test_memneq(p1, p2, DH_BYTES);
  test_assert(! crypto_dh_get_public(dh2, p2, DH_BYTES));
  test_memneq(p1, p2, DH_BYTES);

  memset(s1, 0, DH_BYTES);
  memset(s2, 0xFF, DH_BYTES);
  s1len = crypto_dh_compute_secret(LOG_WARN, dh1, p2, DH_BYTES, s1, 50);
  s2len = crypto_dh_compute_secret(LOG_WARN, dh2, p1, DH_BYTES, s2, 50);
  test_assert(s1len > 0);
  test_eq(s1len, s2len);
  test_memeq(s1, s2, s1len);

  {
    /* XXXX Now fabricate some bad values and make sure they get caught,
     * Check 0, 1, N-1, >= N, etc.
     */
  }

 done:
  crypto_dh_free(dh1);
  crypto_dh_free(dh2);
}

/** Run unit tests for our random number generation function and its wrappers.
 */
static void
test_crypto_rng(void)
{
  int i, j, allok;
  char data1[100], data2[100];
  double d;

  /* Try out RNG. */
  test_assert(! crypto_seed_rng(0));
  crypto_rand(data1, 100);
  crypto_rand(data2, 100);
  test_memneq(data1,data2,100);
  allok = 1;
  for (i = 0; i < 100; ++i) {
    uint64_t big;
    char *host;
    j = crypto_rand_int(100);
    if (j < 0 || j >= 100)
      allok = 0;
    big = crypto_rand_uint64(U64_LITERAL(1)<<40);
    if (big >= (U64_LITERAL(1)<<40))
      allok = 0;
    big = crypto_rand_uint64(U64_LITERAL(5));
    if (big >= 5)
      allok = 0;
    d = crypto_rand_double();
    test_assert(d >= 0);
    test_assert(d < 1.0);
    host = crypto_random_hostname(3,8,"www.",".onion");
    if (strcmpstart(host,"www.") ||
        strcmpend(host,".onion") ||
        strlen(host) < 13 ||
        strlen(host) > 18)
      allok = 0;
    tor_free(host);
  }
  test_assert(allok);
 done:
  ;
}

/** Run unit tests for our AES functionality */
static void
test_crypto_aes(void)
{
  char *data1 = NULL, *data2 = NULL, *data3 = NULL;
  crypto_cipher_env_t *env1 = NULL, *env2 = NULL;
  int i, j;
  char *mem_op_hex_tmp=NULL;

  data1 = tor_malloc(1024);
  data2 = tor_malloc(1024);
  data3 = tor_malloc(1024);

  /* Now, test encryption and decryption with stream cipher. */
  data1[0]='\0';
  for (i = 1023; i>0; i -= 35)
    strncat(data1, "Now is the time for all good onions", i);

  memset(data2, 0, 1024);
  memset(data3, 0, 1024);
  env1 = crypto_new_cipher_env();
  test_neq(env1, 0);
  env2 = crypto_new_cipher_env();
  test_neq(env2, 0);
  j = crypto_cipher_generate_key(env1);
  crypto_cipher_set_key(env2, crypto_cipher_get_key(env1));
  crypto_cipher_encrypt_init_cipher(env1);
  crypto_cipher_decrypt_init_cipher(env2);

  /* Try encrypting 512 chars. */
  crypto_cipher_encrypt(env1, data2, data1, 512);
  crypto_cipher_decrypt(env2, data3, data2, 512);
  test_memeq(data1, data3, 512);
  test_memneq(data1, data2, 512);

  /* Now encrypt 1 at a time, and get 1 at a time. */
  for (j = 512; j < 560; ++j) {
    crypto_cipher_encrypt(env1, data2+j, data1+j, 1);
  }
  for (j = 512; j < 560; ++j) {
    crypto_cipher_decrypt(env2, data3+j, data2+j, 1);
  }
  test_memeq(data1, data3, 560);
  /* Now encrypt 3 at a time, and get 5 at a time. */
  for (j = 560; j < 1024-5; j += 3) {
    crypto_cipher_encrypt(env1, data2+j, data1+j, 3);
  }
  for (j = 560; j < 1024-5; j += 5) {
    crypto_cipher_decrypt(env2, data3+j, data2+j, 5);
  }
  test_memeq(data1, data3, 1024-5);
  /* Now make sure that when we encrypt with different chunk sizes, we get
     the same results. */
  crypto_free_cipher_env(env2);
  env2 = NULL;

  memset(data3, 0, 1024);
  env2 = crypto_new_cipher_env();
  test_neq(env2, 0);
  crypto_cipher_set_key(env2, crypto_cipher_get_key(env1));
  crypto_cipher_encrypt_init_cipher(env2);
  for (j = 0; j < 1024-16; j += 17) {
    crypto_cipher_encrypt(env2, data3+j, data1+j, 17);
  }
  for (j= 0; j < 1024-16; ++j) {
    if (data2[j] != data3[j]) {
      printf("%d:  %d\t%d\n", j, (int) data2[j], (int) data3[j]);
    }
  }
  test_memeq(data2, data3, 1024-16);
  crypto_free_cipher_env(env1);
  env1 = NULL;
  crypto_free_cipher_env(env2);
  env2 = NULL;

  /* NIST test vector for aes. */
  env1 = crypto_new_cipher_env(); /* IV starts at 0 */
  crypto_cipher_set_key(env1, "\x80\x00\x00\x00\x00\x00\x00\x00"
                              "\x00\x00\x00\x00\x00\x00\x00\x00");
  crypto_cipher_encrypt_init_cipher(env1);
  crypto_cipher_encrypt(env1, data1,
                        "\x00\x00\x00\x00\x00\x00\x00\x00"
                        "\x00\x00\x00\x00\x00\x00\x00\x00", 16);
  test_memeq_hex(data1, "0EDD33D3C621E546455BD8BA1418BEC8");

  /* Now test rollover.  All these values are originally from a python
   * script. */
  crypto_cipher_set_iv(env1, "\x00\x00\x00\x00\x00\x00\x00\x00"
                             "\xff\xff\xff\xff\xff\xff\xff\xff");
  memset(data2, 0,  1024);
  crypto_cipher_encrypt(env1, data1, data2, 32);
  test_memeq_hex(data1, "335fe6da56f843199066c14a00a40231"
                        "cdd0b917dbc7186908a6bfb5ffd574d3");

  crypto_cipher_set_iv(env1, "\x00\x00\x00\x00\xff\xff\xff\xff"
                             "\xff\xff\xff\xff\xff\xff\xff\xff");
  memset(data2, 0,  1024);
  crypto_cipher_encrypt(env1, data1, data2, 32);
  test_memeq_hex(data1, "e627c6423fa2d77832a02b2794094b73"
                        "3e63c721df790d2c6469cc1953a3ffac");

  crypto_cipher_set_iv(env1, "\xff\xff\xff\xff\xff\xff\xff\xff"
                             "\xff\xff\xff\xff\xff\xff\xff\xff");
  memset(data2, 0,  1024);
  crypto_cipher_encrypt(env1, data1, data2, 32);
  test_memeq_hex(data1, "2aed2bff0de54f9328efd070bf48f70a"
                        "0EDD33D3C621E546455BD8BA1418BEC8");

  /* Now check rollover on inplace cipher. */
  crypto_cipher_set_iv(env1, "\xff\xff\xff\xff\xff\xff\xff\xff"
                             "\xff\xff\xff\xff\xff\xff\xff\xff");
  crypto_cipher_crypt_inplace(env1, data2, 64);
  test_memeq_hex(data2, "2aed2bff0de54f9328efd070bf48f70a"
                        "0EDD33D3C621E546455BD8BA1418BEC8"
                        "93e2c5243d6839eac58503919192f7ae"
                        "1908e67cafa08d508816659c2e693191");
  crypto_cipher_set_iv(env1, "\xff\xff\xff\xff\xff\xff\xff\xff"
                             "\xff\xff\xff\xff\xff\xff\xff\xff");
  crypto_cipher_crypt_inplace(env1, data2, 64);
  test_assert(tor_mem_is_zero(data2, 64));

 done:
  tor_free(mem_op_hex_tmp);
  if (env1)
    crypto_free_cipher_env(env1);
  if (env2)
    crypto_free_cipher_env(env2);
  tor_free(data1);
  tor_free(data2);
  tor_free(data3);
}

/** Run unit tests for our SHA-1 functionality */
static void
test_crypto_sha(void)
{
  crypto_digest_env_t *d1 = NULL, *d2 = NULL;
  int i;
  char key[80];
  char digest[32];
  char data[50];
  char d_out1[DIGEST_LEN], d_out2[DIGEST256_LEN];
  char *mem_op_hex_tmp=NULL;

  /* Test SHA-1 with a test vector from the specification. */
  i = crypto_digest(data, "abc", 3);
  test_memeq_hex(data, "A9993E364706816ABA3E25717850C26C9CD0D89D");
  tt_int_op(i, ==, 0);

  /* Test SHA-256 with a test vector from the specification. */
  i = crypto_digest256(data, "abc", 3, DIGEST_SHA256);
  test_memeq_hex(data, "BA7816BF8F01CFEA414140DE5DAE2223B00361A3"
                       "96177A9CB410FF61F20015AD");
  tt_int_op(i, ==, 0);

  /* Test HMAC-SHA-1 with test cases from RFC2202. */

  /* Case 1. */
  memset(key, 0x0b, 20);
  crypto_hmac_sha1(digest, key, 20, "Hi There", 8);
  test_streq(hex_str(digest, 20),
             "B617318655057264E28BC0B6FB378C8EF146BE00");
  /* Case 2. */
  crypto_hmac_sha1(digest, "Jefe", 4, "what do ya want for nothing?", 28);
  test_streq(hex_str(digest, 20),
             "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79");

  /* Case 4. */
  base16_decode(key, 25,
                "0102030405060708090a0b0c0d0e0f10111213141516171819", 50);
  memset(data, 0xcd, 50);
  crypto_hmac_sha1(digest, key, 25, data, 50);
  test_streq(hex_str(digest, 20),
             "4C9007F4026250C6BC8414F9BF50C86C2D7235DA");

  /* Case 5. */
  memset(key, 0xaa, 80);
  crypto_hmac_sha1(digest, key, 80,
                   "Test Using Larger Than Block-Size Key - Hash Key First",
                   54);
  test_streq(hex_str(digest, 20),
             "AA4AE5E15272D00E95705637CE8A3B55ED402112");

  /* Incremental digest code. */
  d1 = crypto_new_digest_env();
  test_assert(d1);
  crypto_digest_add_bytes(d1, "abcdef", 6);
  d2 = crypto_digest_dup(d1);
  test_assert(d2);
  crypto_digest_add_bytes(d2, "ghijkl", 6);
  crypto_digest_get_digest(d2, d_out1, sizeof(d_out1));
  crypto_digest(d_out2, "abcdefghijkl", 12);
  test_memeq(d_out1, d_out2, DIGEST_LEN);
  crypto_digest_assign(d2, d1);
  crypto_digest_add_bytes(d2, "mno", 3);
  crypto_digest_get_digest(d2, d_out1, sizeof(d_out1));
  crypto_digest(d_out2, "abcdefmno", 9);
  test_memeq(d_out1, d_out2, DIGEST_LEN);
  crypto_digest_get_digest(d1, d_out1, sizeof(d_out1));
  crypto_digest(d_out2, "abcdef", 6);
  test_memeq(d_out1, d_out2, DIGEST_LEN);
  crypto_free_digest_env(d1);
  crypto_free_digest_env(d2);

  /* Incremental digest code with sha256 */
  d1 = crypto_new_digest256_env(DIGEST_SHA256);
  test_assert(d1);
  crypto_digest_add_bytes(d1, "abcdef", 6);
  d2 = crypto_digest_dup(d1);
  test_assert(d2);
  crypto_digest_add_bytes(d2, "ghijkl", 6);
  crypto_digest_get_digest(d2, d_out1, sizeof(d_out1));
  crypto_digest256(d_out2, "abcdefghijkl", 12, DIGEST_SHA256);
  test_memeq(d_out1, d_out2, DIGEST_LEN);
  crypto_digest_assign(d2, d1);
  crypto_digest_add_bytes(d2, "mno", 3);
  crypto_digest_get_digest(d2, d_out1, sizeof(d_out1));
  crypto_digest256(d_out2, "abcdefmno", 9, DIGEST_SHA256);
  test_memeq(d_out1, d_out2, DIGEST_LEN);
  crypto_digest_get_digest(d1, d_out1, sizeof(d_out1));
  crypto_digest256(d_out2, "abcdef", 6, DIGEST_SHA256);
  test_memeq(d_out1, d_out2, DIGEST_LEN);

 done:
  if (d1)
    crypto_free_digest_env(d1);
  if (d2)
    crypto_free_digest_env(d2);
  tor_free(mem_op_hex_tmp);
}

/** Run unit tests for our public key crypto functions */
static void
test_crypto_pk(void)
{
  crypto_pk_env_t *pk1 = NULL, *pk2 = NULL;
  char *encoded = NULL;
  char data1[1024], data2[1024], data3[1024];
  size_t size;
  int i, j, p, len;

  /* Public-key ciphers */
  pk1 = pk_generate(0);
  pk2 = crypto_new_pk_env();
  test_assert(pk1 && pk2);
  test_assert(! crypto_pk_write_public_key_to_string(pk1, &encoded, &size));
  test_assert(! crypto_pk_read_public_key_from_string(pk2, encoded, size));
  test_eq(0, crypto_pk_cmp_keys(pk1, pk2));

  test_eq(128, crypto_pk_keysize(pk1));
  test_eq(1024, crypto_pk_num_bits(pk1));
  test_eq(128, crypto_pk_keysize(pk2));
  test_eq(1024, crypto_pk_num_bits(pk2));

  test_eq(128, crypto_pk_public_encrypt(pk2, data1, sizeof(data1),
                                        "Hello whirled.", 15,
                                        PK_PKCS1_OAEP_PADDING));
  test_eq(128, crypto_pk_public_encrypt(pk1, data2, sizeof(data1),
                                        "Hello whirled.", 15,
                                        PK_PKCS1_OAEP_PADDING));
  /* oaep padding should make encryption not match */
  test_memneq(data1, data2, 128);
  test_eq(15, crypto_pk_private_decrypt(pk1, data3, sizeof(data3), data1, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  test_streq(data3, "Hello whirled.");
  memset(data3, 0, 1024);
  test_eq(15, crypto_pk_private_decrypt(pk1, data3, sizeof(data3), data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  test_streq(data3, "Hello whirled.");
  /* Can't decrypt with public key. */
  test_eq(-1, crypto_pk_private_decrypt(pk2, data3, sizeof(data3), data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));
  /* Try again with bad padding */
  memcpy(data2+1, "XYZZY", 5);  /* This has fails ~ once-in-2^40 */
  test_eq(-1, crypto_pk_private_decrypt(pk1, data3, sizeof(data3), data2, 128,
                                        PK_PKCS1_OAEP_PADDING,1));

  /* File operations: save and load private key */
  test_assert(! crypto_pk_write_private_key_to_filename(pk1,
                                                        get_fname("pkey1")));
  /* failing case for read: can't read. */
  test_assert(crypto_pk_read_private_key_from_filename(pk2,
                                                   get_fname("xyzzy")) < 0);
  write_str_to_file(get_fname("xyzzy"), "foobar", 6);
  /* Failing case for read: no key. */
  test_assert(crypto_pk_read_private_key_from_filename(pk2,
                                                   get_fname("xyzzy")) < 0);
  test_assert(! crypto_pk_read_private_key_from_filename(pk2,
                                                         get_fname("pkey1")));
  test_eq(15, crypto_pk_private_decrypt(pk2, data3, sizeof(data3), data1, 128,
                                        PK_PKCS1_OAEP_PADDING,1));

  /* Now try signing. */
  strlcpy(data1, "Ossifrage", 1024);
  test_eq(128, crypto_pk_private_sign(pk1, data2, sizeof(data2), data1, 10));
  test_eq(10,
          crypto_pk_public_checksig(pk1, data3, sizeof(data3), data2, 128));
  test_streq(data3, "Ossifrage");
  /* Try signing digests. */
  test_eq(128, crypto_pk_private_sign_digest(pk1, data2, sizeof(data2),
                                             data1, 10));
  test_eq(20,
          crypto_pk_public_checksig(pk1, data3, sizeof(data3), data2, 128));
  test_eq(0, crypto_pk_public_checksig_digest(pk1, data1, 10, data2, 128));
  test_eq(-1, crypto_pk_public_checksig_digest(pk1, data1, 11, data2, 128));

  /*XXXX test failed signing*/

  /* Try encoding */
  crypto_free_pk_env(pk2);
  pk2 = NULL;
  i = crypto_pk_asn1_encode(pk1, data1, 1024);
  test_assert(i>0);
  pk2 = crypto_pk_asn1_decode(data1, i);
  test_assert(crypto_pk_cmp_keys(pk1,pk2) == 0);

  /* Try with hybrid encryption wrappers. */
  crypto_rand(data1, 1024);
  for (i = 0; i < 3; ++i) {
    for (j = 85; j < 140; ++j) {
      memset(data2,0,1024);
      memset(data3,0,1024);
      if (i == 0 && j < 129)
        continue;
      p = (i==0)?PK_NO_PADDING:
        (i==1)?PK_PKCS1_PADDING:PK_PKCS1_OAEP_PADDING;
      len = crypto_pk_public_hybrid_encrypt(pk1,data2,sizeof(data2),
                                            data1,j,p,0);
      test_assert(len>=0);
      len = crypto_pk_private_hybrid_decrypt(pk1,data3,sizeof(data3),
                                             data2,len,p,1);
      test_eq(len,j);
      test_memeq(data1,data3,j);
    }
  }

  /* Try copy_full */
  crypto_free_pk_env(pk2);
  pk2 = crypto_pk_copy_full(pk1);
  test_assert(pk2 != NULL);
  test_neq_ptr(pk1, pk2);
  test_assert(crypto_pk_cmp_keys(pk1,pk2) == 0);

 done:
  if (pk1)
    crypto_free_pk_env(pk1);
  if (pk2)
    crypto_free_pk_env(pk2);
  tor_free(encoded);
}

/** Run unit tests for misc crypto formatting functionality (base64, base32,
 * fingerprints, etc) */
static void
test_crypto_formats(void)
{
  char *data1 = NULL, *data2 = NULL, *data3 = NULL;
  int i, j, idx;

  data1 = tor_malloc(1024);
  data2 = tor_malloc(1024);
  data3 = tor_malloc(1024);
  test_assert(data1 && data2 && data3);

  /* Base64 tests */
  memset(data1, 6, 1024);
  for (idx = 0; idx < 10; ++idx) {
    i = base64_encode(data2, 1024, data1, idx);
    test_assert(i >= 0);
    j = base64_decode(data3, 1024, data2, i);
    test_eq(j,idx);
    test_memeq(data3, data1, idx);
  }

  strlcpy(data1, "Test string that contains 35 chars.", 1024);
  strlcat(data1, " 2nd string that contains 35 chars.", 1024);

  i = base64_encode(data2, 1024, data1, 71);
  test_assert(i >= 0);
  j = base64_decode(data3, 1024, data2, i);
  test_eq(j, 71);
  test_streq(data3, data1);
  test_assert(data2[i] == '\0');

  crypto_rand(data1, DIGEST_LEN);
  memset(data2, 100, 1024);
  digest_to_base64(data2, data1);
  test_eq(BASE64_DIGEST_LEN, strlen(data2));
  test_eq(100, data2[BASE64_DIGEST_LEN+2]);
  memset(data3, 99, 1024);
  test_eq(digest_from_base64(data3, data2), 0);
  test_memeq(data1, data3, DIGEST_LEN);
  test_eq(99, data3[DIGEST_LEN+1]);

  test_assert(digest_from_base64(data3, "###") < 0);

  /* Encoding SHA256 */
  crypto_rand(data2, DIGEST256_LEN);
  memset(data2, 100, 1024);
  digest256_to_base64(data2, data1);
  test_eq(BASE64_DIGEST256_LEN, strlen(data2));
  test_eq(100, data2[BASE64_DIGEST256_LEN+2]);
  memset(data3, 99, 1024);
  test_eq(digest256_from_base64(data3, data2), 0);
  test_memeq(data1, data3, DIGEST256_LEN);
  test_eq(99, data3[DIGEST256_LEN+1]);

  /* Base32 tests */
  strlcpy(data1, "5chrs", 1024);
  /* bit pattern is:  [35 63 68 72 73] ->
   *        [00110101 01100011 01101000 01110010 01110011]
   * By 5s: [00110 10101 10001 10110 10000 11100 10011 10011]
   */
  base32_encode(data2, 9, data1, 5);
  test_streq(data2, "gvrwq4tt");

  strlcpy(data1, "\xFF\xF5\x6D\x44\xAE\x0D\x5C\xC9\x62\xC4", 1024);
  base32_encode(data2, 30, data1, 10);
  test_streq(data2, "772w2rfobvomsywe");

  /* Base16 tests */
  strlcpy(data1, "6chrs\xff", 1024);
  base16_encode(data2, 13, data1, 6);
  test_streq(data2, "3663687273FF");

  strlcpy(data1, "f0d678affc000100", 1024);
  i = base16_decode(data2, 8, data1, 16);
  test_eq(i,0);
  test_memeq(data2, "\xf0\xd6\x78\xaf\xfc\x00\x01\x00",8);

  /* now try some failing base16 decodes */
  test_eq(-1, base16_decode(data2, 8, data1, 15)); /* odd input len */
  test_eq(-1, base16_decode(data2, 7, data1, 16)); /* dest too short */
  strlcpy(data1, "f0dz!8affc000100", 1024);
  test_eq(-1, base16_decode(data2, 8, data1, 16));

  tor_free(data1);
  tor_free(data2);
  tor_free(data3);

  /* Add spaces to fingerprint */
  {
    data1 = tor_strdup("ABCD1234ABCD56780000ABCD1234ABCD56780000");
    test_eq(strlen(data1), 40);
    data2 = tor_malloc(FINGERPRINT_LEN+1);
    add_spaces_to_fp(data2, FINGERPRINT_LEN+1, data1);
    test_streq(data2, "ABCD 1234 ABCD 5678 0000 ABCD 1234 ABCD 5678 0000");
    tor_free(data1);
    tor_free(data2);
  }

  /* Check fingerprint */
  {
    test_assert(crypto_pk_check_fingerprint_syntax(
                "ABCD 1234 ABCD 5678 0000 ABCD 1234 ABCD 5678 0000"));
    test_assert(!crypto_pk_check_fingerprint_syntax(
                "ABCD 1234 ABCD 5678 0000 ABCD 1234 ABCD 5678 000"));
    test_assert(!crypto_pk_check_fingerprint_syntax(
                "ABCD 1234 ABCD 5678 0000 ABCD 1234 ABCD 5678 00000"));
    test_assert(!crypto_pk_check_fingerprint_syntax(
                "ABCD 1234 ABCD 5678 0000 ABCD1234 ABCD 5678 0000"));
    test_assert(!crypto_pk_check_fingerprint_syntax(
                "ABCD 1234 ABCD 5678 0000 ABCD1234 ABCD 5678 00000"));
    test_assert(!crypto_pk_check_fingerprint_syntax(
                "ACD 1234 ABCD 5678 0000 ABCD 1234 ABCD 5678 00000"));
  }

 done:
  tor_free(data1);
  tor_free(data2);
  tor_free(data3);
}

/** Run unit tests for our secret-to-key passphrase hashing functionality. */
static void
test_crypto_s2k(void)
{
  char buf[29];
  char buf2[29];
  char *buf3 = NULL;
  int i;

  memset(buf, 0, sizeof(buf));
  memset(buf2, 0, sizeof(buf2));
  buf3 = tor_malloc(65536);
  memset(buf3, 0, 65536);

  secret_to_key(buf+9, 20, "", 0, buf);
  crypto_digest(buf2+9, buf3, 1024);
  test_memeq(buf, buf2, 29);

  memcpy(buf,"vrbacrda",8);
  memcpy(buf2,"vrbacrda",8);
  buf[8] = 96;
  buf2[8] = 96;
  secret_to_key(buf+9, 20, "12345678", 8, buf);
  for (i = 0; i < 65536; i += 16) {
    memcpy(buf3+i, "vrbacrda12345678", 16);
  }
  crypto_digest(buf2+9, buf3, 65536);
  test_memeq(buf, buf2, 29);

 done:
  tor_free(buf3);
}

/** Test AES-CTR encryption and decryption with IV. */
static void
test_crypto_aes_iv(void)
{
  crypto_cipher_env_t *cipher;
  char *plain, *encrypted1, *encrypted2, *decrypted1, *decrypted2;
  char plain_1[1], plain_15[15], plain_16[16], plain_17[17];
  char key1[16], key2[16];
  ssize_t encrypted_size, decrypted_size;

  plain = tor_malloc(4095);
  encrypted1 = tor_malloc(4095 + 1 + 16);
  encrypted2 = tor_malloc(4095 + 1 + 16);
  decrypted1 = tor_malloc(4095 + 1);
  decrypted2 = tor_malloc(4095 + 1);

  crypto_rand(plain, 4095);
  crypto_rand(key1, 16);
  crypto_rand(key2, 16);
  crypto_rand(plain_1, 1);
  crypto_rand(plain_15, 15);
  crypto_rand(plain_16, 16);
  crypto_rand(plain_17, 17);
  key1[0] = key2[0] + 128; /* Make sure that contents are different. */
  /* Encrypt and decrypt with the same key. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 4095,
                                                 plain, 4095);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 4095);
  tor_assert(encrypted_size > 0); /* This is obviously true, since 4111 is
                                   * greater than 0, but its truth is not
                                   * obvious to all analysis tools. */
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(decrypted_size, 4095);
  tor_assert(decrypted_size > 0);
  test_memeq(plain, decrypted1, 4095);
  /* Encrypt a second time (with a new random initialization vector). */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted2, 16 + 4095,
                                             plain, 4095);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 4095);
  tor_assert(encrypted_size > 0);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted2, 4095,
                                             encrypted2, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(decrypted_size, 4095);
  tor_assert(decrypted_size > 0);
  test_memeq(plain, decrypted2, 4095);
  test_memneq(encrypted1, encrypted2, encrypted_size);
  /* Decrypt with the wrong key. */
  cipher = crypto_create_init_cipher(key2, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted2, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_memneq(plain, decrypted2, encrypted_size);
  /* Alter the initialization vector. */
  encrypted1[0] += 42;
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 4095,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_memneq(plain, decrypted2, 4095);
  /* Special length case: 1. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 1,
                                             plain_1, 1);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 1);
  tor_assert(encrypted_size > 0);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 1,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(decrypted_size, 1);
  tor_assert(decrypted_size > 0);
  test_memeq(plain_1, decrypted1, 1);
  /* Special length case: 15. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 15,
                                             plain_15, 15);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 15);
  tor_assert(encrypted_size > 0);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 15,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(decrypted_size, 15);
  tor_assert(decrypted_size > 0);
  test_memeq(plain_15, decrypted1, 15);
  /* Special length case: 16. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 16,
                                             plain_16, 16);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 16);
  tor_assert(encrypted_size > 0);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 16,
                                             encrypted1, encrypted_size);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(decrypted_size, 16);
  tor_assert(decrypted_size > 0);
  test_memeq(plain_16, decrypted1, 16);
  /* Special length case: 17. */
  cipher = crypto_create_init_cipher(key1, 1);
  encrypted_size = crypto_cipher_encrypt_with_iv(cipher, encrypted1, 16 + 17,
                                             plain_17, 17);
  crypto_free_cipher_env(cipher);
  cipher = NULL;
  test_eq(encrypted_size, 16 + 17);
  tor_assert(encrypted_size > 0);
  cipher = crypto_create_init_cipher(key1, 0);
  decrypted_size = crypto_cipher_decrypt_with_iv(cipher, decrypted1, 17,
                                             encrypted1, encrypted_size);
  test_eq(decrypted_size, 17);
  tor_assert(decrypted_size > 0);
  test_memeq(plain_17, decrypted1, 17);

 done:
  /* Free memory. */
  tor_free(plain);
  tor_free(encrypted1);
  tor_free(encrypted2);
  tor_free(decrypted1);
  tor_free(decrypted2);
  if (cipher)
    crypto_free_cipher_env(cipher);
}

/** Test base32 decoding. */
static void
test_crypto_base32_decode(void)
{
  char plain[60], encoded[96 + 1], decoded[60];
  int res;
  crypto_rand(plain, 60);
  /* Encode and decode a random string. */
  base32_encode(encoded, 96 + 1, plain, 60);
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memeq(plain, decoded, 60);
  /* Encode, uppercase, and decode a random string. */
  base32_encode(encoded, 96 + 1, plain, 60);
  tor_strupper(encoded);
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memeq(plain, decoded, 60);
  /* Change encoded string and decode. */
  if (encoded[0] == 'A' || encoded[0] == 'a')
    encoded[0] = 'B';
  else
    encoded[0] = 'A';
  res = base32_decode(decoded, 60, encoded, 96);
  test_eq(res, 0);
  test_memneq(plain, decoded, 60);
  /* Bad encodings. */
  encoded[0] = '!';
  res = base32_decode(decoded, 60, encoded, 96);
  test_assert(res < 0);

 done:
  ;
}

#define CRYPTO_LEGACY(name)                                            \
  { #name, legacy_test_helper, 0, &legacy_setup, test_crypto_ ## name }

struct testcase_t crypto_tests[] = {
  CRYPTO_LEGACY(formats),
  CRYPTO_LEGACY(rng),
  CRYPTO_LEGACY(aes),
  CRYPTO_LEGACY(sha),
  CRYPTO_LEGACY(pk),
  CRYPTO_LEGACY(dh),
  CRYPTO_LEGACY(s2k),
  CRYPTO_LEGACY(aes_iv),
  CRYPTO_LEGACY(base32_decode),
  END_OF_TESTCASES
};

