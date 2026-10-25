/**
 * This file is a part of ThetaGP.
 *
 * ThetaGP is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * ThetaGP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.
 *
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// Only what a challenge signature needs: big-number arithmetic, the RSA key
// itself with its PSS padding, and message digests. The TLS and X.509 layers
// are not built: an authentication channel here is a challenge and the
// signature over it, not a session.

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_PKCS1_V15

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C

#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C

// The random source is the callback a signature call hands in.
#define MBEDTLS_NO_PLATFORM_ENTROPY

