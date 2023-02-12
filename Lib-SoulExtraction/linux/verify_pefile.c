// SPDX-License-Identifier: GPL-2.0-or-later
/* Parse a signed PE binary
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

//#define pr_fmt(fmt) "PEFILE: "fmt
//#include <linux/module.h>
//#include <linux/kernel.h>
//#include <linux/slab.h>
//#include <linux/err.h>
//#include <linux/pe.h>
//#include <linux/asn1.h>
//#include <linux/verification.h>
//#include <crypto/hash.h>
//#include "verify_pefile.h"

#include "verify_pefile.h"
#include "errno.h"
#include "asn1.h"

#include "config.h"

#include "pe.h"

#include "../rewrite/Lib.SoulExtraction.rewrite.h"

//
// macro
//

#define GFP_ATOMIC /*(__GFP_HIGH|__GFP_ATOMIC|__GFP_KSWAPD_RECLAIM)*/ 1
#define GFP_KERNEL /*(__GFP_RECLAIM | __GFP_IO | __GFP_FS)*/ 2
#define GFP_KERNEL_ACCOUNT /*(GFP_KERNEL | __GFP_ACCOUNT)*/ 3
#define GFP_NOWAIT /*(__GFP_KSWAPD_RECLAIM)*/ 4
#define GFP_NOIO /*(__GFP_RECLAIM)*/ 5
#define GFP_NOFS /*(__GFP_RECLAIM | __GFP_IO)*/ 6
#define GFP_USER /*(__GFP_RECLAIM | __GFP_IO | __GFP_FS | __GFP_HARDWALL)*/ 7
#define GFP_DMA /*__GFP_DMA*/ 8
#define GFP_DMA32 /*__GFP_DMA32*/ 9
#define GFP_HIGHUSER /*(GFP_USER | __GFP_HIGHMEM)*/ 10
#define GFP_HIGHUSER_MOVABLE /*(GFP_HIGHUSER | __GFP_MOVABLE)*/ 11
#define GFP_TRANSHUGE_LIGHT 12
#define GFP_TRANSHUGE /*(GFP_TRANSHUGE_LIGHT | __GFP_DIRECT_RECLAIM)*/ 13

#define ERR_PTR(err) ((void *)((long)(err)))
#define PTR_ERR(ptr) ((long)(ptr))
#define IS_ERR(ptr) ((unsigned long)(ptr) > (unsigned long)(-1000))

///*
// * This looks more complex than it should be. But we need to
// * get the type for the ~ right in round_down (it needs to be
// * as wide as the result!), and we want to evaluate the macro
// * arguments just once each.
// */
//#define __round_mask(x, y) ((__typeof__(x))((y)-1))
//
///**
// * round_up - round up to next specified power of 2
// * @x: the value to round
// * @y: multiple to round up to (must be a power of 2)
// *
// * Rounds @x up to next multiple of @y (which must be a power of 2).
// * To perform arbitrary rounding up, use roundup() below.
// */
//#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
///**
//* round_up - round up to next specified power of 2
//* @x: the value to round
//* @y: multiple to round up to (must be a power of 2)
//*
//* Rounds @x up to next multiple of @y (which must be a power of 2).
//* To perform arbitrary rounding up, use roundup() below.
//*/
//#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
///**
//* round_down - round down to next specified power of 2
//* @x: the value to round
//* @y: multiple to round down to (must be a power of 2)
//*
//* Rounds @x down to next multiple of @y (which must be a power of 2).
//* To perform arbitrary rounding down, use rounddown() below.
//*/
//#define round_down(x, y) ((x) & ~__round_mask(x, y))

/* n must be power of 2 */
#define round_up(x, n) (((x) + (n)-1u) & ~((n)-1u))

/*
 * Parse a PE binary.
 */
int
pefile_parse_binary(const void *pebuf, unsigned int pelen, struct pefile_context *ctx)
{
    const struct mz_hdr *mz = pebuf;
    const struct pe_hdr *pe;
    const struct pe32_opt_hdr *pe32;
    const struct pe32plus_opt_hdr *pe64;
    const struct data_directory *ddir;
    const struct data_dirent *dde;
    const struct section_header *secs, *sec;
    size_t cursor, datalen = pelen;

    kenter("");

#define chkaddr(base, x, s) \
    do \
    { \
        if ((x) < base || (s) >= datalen || (x) > datalen - (s)) \
            return -ELIBBAD; \
    } while (0)

    // chkaddr(0, 0, sizeof(*mz));

    if (sizeof(*mz) >= datalen)
        return -ELIBBAD;

    if (mz->magic != MZ_MAGIC)
        return -ELIBBAD;
    cursor = sizeof(*mz);

    chkaddr(cursor, mz->peaddr, sizeof(*pe));
    pe = (struct pe_hdr *)((unsigned char *)pebuf + mz->peaddr);
    if (pe->magic != PE_MAGIC)
        return -ELIBBAD;
    cursor = mz->peaddr + sizeof(*pe);

    // chkaddr(0, cursor, sizeof(pe32->magic));

    if ((sizeof(pe32->magic)) >= datalen || cursor > datalen - sizeof(pe32->magic))
        return -ELIBBAD;

    pe32 = (struct pe32_opt_hdr *)((unsigned char *)pebuf + cursor);
    pe64 = (struct pe32plus_opt_hdr *)((unsigned char *)pebuf + cursor);

    switch (pe32->magic)
    {
    case PE_OPT_MAGIC_PE32:
        // chkaddr(0, cursor, sizeof(*pe32));
        if ((sizeof(*pe32)) >= datalen || cursor > datalen - sizeof(*pe32))
            return -ELIBBAD;

        ctx->image_checksum_offset = (unsigned)((unsigned char *)&pe32->csum - (unsigned char *)pebuf);
        ctx->header_size = pe32->header_size;
        cursor += sizeof(*pe32);
        ctx->n_data_dirents = pe32->data_dirs;
        break;

    case PE_OPT_MAGIC_PE32PLUS:
        // chkaddr(0, cursor, sizeof(*pe64));
        if ((sizeof(*pe64)) >= datalen || cursor > datalen - sizeof(*pe64))
            return -ELIBBAD;
        ctx->image_checksum_offset = (unsigned)((unsigned char *)&pe64->csum - (unsigned char *)pebuf);
        ctx->header_size = pe64->header_size;
        cursor += sizeof(*pe64);
        ctx->n_data_dirents = pe64->data_dirs;
        break;

    default:
        pr_debug("Unknown PEOPT magic = %04hx\n", pe32->magic);
        return -ELIBBAD;
    }

    pr_debug("checksum @ %x\n", ctx->image_checksum_offset);
    pr_debug("header size = %x\n", ctx->header_size);

    if (cursor >= ctx->header_size || ctx->header_size >= datalen)
        return -ELIBBAD;

    if (ctx->n_data_dirents > (ctx->header_size - cursor) / sizeof(*dde))
        return -ELIBBAD;

    ddir = (struct data_directory *)((unsigned char *)pebuf + cursor);
    cursor += sizeof(*dde) * ctx->n_data_dirents;

    ctx->cert_dirent_offset = (unsigned)((unsigned char *)&ddir->certs - (unsigned char *)pebuf);
    ctx->certs_size = ddir->certs.size;

    if (!ddir->certs.virtual_address || !ddir->certs.size)
    {
        pr_debug("Unsigned PE binary\n");
        return -EKEYREJECTED;
    }

    chkaddr(ctx->header_size, ddir->certs.virtual_address, ddir->certs.size);
    ctx->sig_offset = ddir->certs.virtual_address;
    ctx->sig_len = ddir->certs.size;
    pr_debug(
        "cert = %x @%x [%p]\n", ctx->sig_len, ctx->sig_offset, ctx->sig_len, (unsigned char *)pebuf + ctx->sig_offset);

    ctx->n_sections = pe->sections;
    if (ctx->n_sections > (ctx->header_size - cursor) / sizeof(*sec))
        return -ELIBBAD;
    ctx->secs = secs = (struct section_header *)((unsigned char *)pebuf + cursor);

    return 0;
}

/*
 * Check and strip the PE wrapper from around the signature and check that the
 * remnant looks something like PKCS#7.
 */
int
pefile_strip_sig_wrapper(const void *pebuf, struct pefile_context *ctx)
{
    struct win_certificate wrapper;
    const unsigned char *pkcs7;
    unsigned len;
    unsigned char pkcs7_1;

    if (ctx->sig_len < sizeof(wrapper))
    {
        pr_debug("Signature wrapper too short\n");
        return -ELIBBAD;
    }

    memcpy(&wrapper, (void *)((unsigned char *)pebuf + ctx->sig_offset), sizeof(wrapper));
    pr_debug("sig wrapper = { %x, %x, %x }\n", wrapper.length, wrapper.revision, wrapper.cert_type);

    /* Both pesign and sbsign round up the length of certificate table
     * (in optional header data directories) to 8 byte alignment.
     */
    if (round_up(wrapper.length, 8) != ctx->sig_len)
    {
        pr_debug("Signature wrapper len wrong\n");
        return -ELIBBAD;
    }

    if (wrapper.revision != WIN_CERT_REVISION_2_0)
    {
        pr_debug("Signature is not revision 2.0\n");
        return -ENOTSUPP;
    }
    if (wrapper.cert_type != WIN_CERT_TYPE_PKCS_SIGNED_DATA)
    {
        pr_debug("Signature certificate type is not PKCS\n");
        return -ENOTSUPP;
    }

    /* It looks like the pkcs signature length in wrapper->length and the
     * size obtained from the data dir entries, which lists the total size
     * of certificate table, are both aligned to an octaword boundary, so
     * we may have to deal with some padding.
     */
    ctx->sig_len = wrapper.length;
    ctx->sig_offset += sizeof(wrapper);
    ctx->sig_len -= sizeof(wrapper);
    if (ctx->sig_len < 4)
    {
        pr_debug("Signature data missing\n");
        return -EKEYREJECTED;
    }

    /* What's left should be a PKCS#7 cert */
    pkcs7 = (unsigned char *)((unsigned char *)pebuf + ctx->sig_offset);
    if (pkcs7[0] != (ASN1_CONS_BIT | ASN1_SEQ))
        goto not_pkcs7;

    pkcs7_1 = pkcs7[1];
    if (pkcs7_1 >= 0 && pkcs7_1 <= 0x7f)
    {
        len = pkcs7[1] + 2;
        goto check_len;
    }
    else if (pkcs7_1 == ASN1_INDEFINITE_LENGTH)
    {
        return 0;
    }
    else if (pkcs7_1 == 0x81)
    {
        len = pkcs7[2] + 3;
        goto check_len;
    }
    else if (pkcs7_1 == 0x82)
    {
        len = ((pkcs7[2] << 8) | pkcs7[3]) + 4;
        goto check_len;
    }
    else if (pkcs7_1 >= 0x83 && pkcs7_1 <= 0xff)
    {
        return -EMSGSIZE;
    }
    else
    {
        goto not_pkcs7;
    }

    /*switch (pkcs7[1]) {
    case 0 ... 0x7f:
        len = pkcs7[1] + 2;
        goto check_len;
    case ASN1_INDEFINITE_LENGTH:
        return 0;
    case 0x81:
        len = pkcs7[2] + 3;
        goto check_len;
    case 0x82:
        len = ((pkcs7[2] << 8) | pkcs7[3]) + 4;
        goto check_len;
    case 0x83 ... 0xff:
        return -EMSGSIZE;
    default:
        goto not_pkcs7;
    }*/

check_len:
    if (len <= ctx->sig_len)
    {
        /* There may be padding */
        ctx->sig_len = len;
        return 0;
    }
not_pkcs7:
    pr_debug("Signature data not PKCS#7\n");
    return -ELIBBAD;
}

/*
 * Compare two sections for canonicalisation.
 */
// static int pefile_compare_shdrs(const void *a, const void *b)
//{
//	const struct section_header *shdra = a;
//	const struct section_header *shdrb = b;
//	int rc;
//
//	if (shdra->data_addr > shdrb->data_addr)
//		return 1;
//	if (shdrb->data_addr > shdra->data_addr)
//		return -1;
//
//	if (shdra->virtual_address > shdrb->virtual_address)
//		return 1;
//	if (shdrb->virtual_address > shdra->virtual_address)
//		return -1;
//
//	rc = strcmp(shdra->name, shdrb->name);
//	if (rc != 0)
//		return rc;
//
//	if (shdra->virtual_size > shdrb->virtual_size)
//		return 1;
//	if (shdrb->virtual_size > shdra->virtual_size)
//		return -1;
//
//	if (shdra->raw_data_size > shdrb->raw_data_size)
//		return 1;
//	if (shdrb->raw_data_size > shdra->raw_data_size)
//		return -1;
//
//	return 0;
// }

/*
 * Load the contents of the PE binary into the digest, leaving out the image
 * checksum and the certificate data block.
 */
// static int pefile_digest_pe_contents(const void *pebuf, unsigned int pelen,
//				     struct pefile_context *ctx,
//				     struct shash_desc *desc)
//{
//	unsigned *canon, tmp, loop, i, hashed_bytes;
//	int ret;
//
//	/* Digest the header and data directory, but leave out the image
//	 * checksum and the data dirent for the signature.
//	 */
//	ret = crypto_shash_update(desc, pebuf, ctx->image_checksum_offset);
//	if (ret < 0)
//		return ret;
//
//	tmp = ctx->image_checksum_offset + sizeof(uint32_t);
//	ret = crypto_shash_update(desc, pebuf + tmp,
//				  ctx->cert_dirent_offset - tmp);
//	if (ret < 0)
//		return ret;
//
//	tmp = ctx->cert_dirent_offset + sizeof(struct data_dirent);
//	ret = crypto_shash_update(desc, pebuf + tmp, ctx->header_size - tmp);
//	if (ret < 0)
//		return ret;
//
//	canon = kcalloc(ctx->n_sections, sizeof(unsigned), GFP_KERNEL);
//	if (!canon)
//		return -ENOMEM;
//
//	/* We have to canonicalise the section table, so we perform an
//	 * insertion sort.
//	 */
//	canon[0] = 0;
//	for (loop = 1; loop < ctx->n_sections; loop++) {
//		for (i = 0; i < loop; i++) {
//			if (pefile_compare_shdrs(&ctx->secs[canon[i]],
//						 &ctx->secs[loop]) > 0) {
//				memmove(&canon[i + 1], &canon[i],
//					(loop - i) * sizeof(canon[0]));
//				break;
//			}
//		}
//		canon[i] = loop;
//	}
//
//	hashed_bytes = ctx->header_size;
//	for (loop = 0; loop < ctx->n_sections; loop++) {
//		i = canon[loop];
//		if (ctx->secs[i].raw_data_size == 0)
//			continue;
//		ret = crypto_shash_update(desc,
//					  pebuf + ctx->secs[i].data_addr,
//					  ctx->secs[i].raw_data_size);
//		if (ret < 0) {
//			kfree(canon);
//			return ret;
//		}
//		hashed_bytes += ctx->secs[i].raw_data_size;
//	}
//	kfree(canon);
//
//	if (pelen > hashed_bytes) {
//		tmp = hashed_bytes + ctx->certs_size;
//		ret = crypto_shash_update(desc,
//					  pebuf + hashed_bytes,
//					  pelen - tmp);
//		if (ret < 0)
//			return ret;
//	}
//
//	return 0;
// }

/*
 * Digest the contents of the PE binary, leaving out the image checksum and the
 * certificate data block.
 */
// static int pefile_digest_pe(const void *pebuf, unsigned int pelen,
//			    struct pefile_context *ctx)
//{
//	struct crypto_shash *tfm;
//	struct shash_desc *desc;
//	size_t digest_size, desc_size;
//	void *digest;
//	int ret;
//
//	kenter(",%s", ctx->digest_algo);
//
//	/* Allocate the hashing algorithm we're going to need and find out how
//	 * big the hash operational data will be.
//	 */
//	tfm = crypto_alloc_shash(ctx->digest_algo, 0, 0);
//	if (IS_ERR(tfm))
//		return (PTR_ERR(tfm) == -ENOENT) ? -ENOPKG : PTR_ERR(tfm);
//
//	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
//	digest_size = crypto_shash_digestsize(tfm);
//
//	if (digest_size != ctx->digest_len) {
//		pr_debug("Digest size mismatch (%zx != %x)\n",
//			 digest_size, ctx->digest_len);
//		ret = -EBADMSG;
//		goto error_no_desc;
//	}
//	pr_debug("Digest: desc=%zu size=%zu\n", desc_size, digest_size);
//
//	ret = -ENOMEM;
//	desc = kzalloc(desc_size + digest_size, GFP_KERNEL);
//	if (!desc)
//		goto error_no_desc;
//
//	desc->tfm   = tfm;
//	ret = crypto_shash_init(desc);
//	if (ret < 0)
//		goto error;
//
//	ret = pefile_digest_pe_contents(pebuf, pelen, ctx, desc);
//	if (ret < 0)
//		goto error;
//
//	digest = (void *)desc + desc_size;
//	ret = crypto_shash_final(desc, digest);
//	if (ret < 0)
//		goto error;
//
//	pr_debug("Digest calc = [%*ph]\n", ctx->digest_len, digest);
//
//	/* Check that the PE file digest matches that in the MSCODE part of the
//	 * PKCS#7 certificate.
//	 */
//	if (memcmp(digest, ctx->digest, ctx->digest_len) != 0) {
//		pr_debug("Digest mismatch\n");
//		ret = -EKEYREJECTED;
//	} else {
//		pr_debug("The digests match!\n");
//	}
//
// error:
//	kzfree(desc);
// error_no_desc:
//	crypto_free_shash(tfm);
//	kleave(" = %d", ret);
//	return ret;
// }

/**
 * verify_pefile_signature - Verify the signature on a PE binary image
 * @pebuf: Buffer containing the PE binary image
 * @pelen: Length of the binary image
 * @trust_keys: Signing certificate(s) to use as starting points
 * @usage: The use to which the key is being put.
 *
 * Validate that the certificate chain inside the PKCS#7 message inside the PE
 * binary image intersects keys we already know and trust.
 *
 * Returns, in order of descending priority:
 *
 *  (*) -ELIBBAD if the image cannot be parsed, or:
 *
 *  (*) -EKEYREJECTED if a signature failed to match for which we have a valid
 *	key, or:
 *
 *  (*) 0 if at least one signature chain intersects with the keys in the trust
 *	keyring, or:
 *
 *  (*) -ENOPKG if a suitable crypto module couldn't be found for a check on a
 *	chain.
 *
 *  (*) -ENOKEY if we couldn't find a match for any of the signature chains in
 *	the message.
 *
 * May also return -ENOMEM.
 */
int
verify_pefile_signature(const void *pebuf, unsigned pelen, struct key *trusted_keys, enum key_being_used_for usage)
{
    struct pefile_context ctx;
    int ret;

    kenter("");

    memset(&ctx, 0, sizeof(ctx));
    ret = pefile_parse_binary(pebuf, pelen, &ctx);
    if (ret < 0)
        return ret;

    ret = pefile_strip_sig_wrapper(pebuf, &ctx);
    if (ret < 0)
        return ret;

    // we dont need this (gmh said)
    /*ret = verify_pkcs7_signature(NULL, 0,
                     pebuf + ctx.sig_offset, ctx.sig_len,
                     trusted_keys, usage,
                     mscode_parse, &ctx);
    if (ret < 0)
        goto error;

    pr_debug("Digest: %u [%*ph]\n",
         ctx.digest_len, ctx.digest_len, ctx.digest);
         */

    /* Generate the digest and check against the PKCS7 certificate
     * contents.
     */

    // we dont need this (gmh said)
    // ret = pefile_digest_pe(pebuf, pelen, &ctx);

    // error:
    if (ctx.digest)
    {
        kzfree(ctx.digest);
    }

    return ret;
}
