/*
 * Copyright (c) 2026 Michael David Adams.
 * All rights reserved.
 */

/* __START_OF_JASPER_LICENSE__
 *
 * JasPer License Version 2.0
 *
 * Copyright (c) 2001-2006 Michael David Adams
 * Copyright (c) 1999-2000 Image Power, Inc.
 * Copyright (c) 1999-2000 The University of British Columbia
 *
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person (the
 * "User") obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * 1. The above copyright notices and this permission notice (which
 * includes the disclaimer below) shall be included in all copies or
 * substantial portions of the Software.
 *
 * 2. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * __END_OF_JASPER_LICENSE__
 */

/*
 * Test the conversion of sample values between components whose
 * signedness and/or precision differ (e.g., as occurs during color
 * space conversion).
 *
 * The rules that must be satisfied are:
 *
 *   1. A conversion between signed and unsigned representations must
 *      apply the appropriate bias of 2^(prec - 1) (i.e., convert
 *      to/from the offset-binary representation).
 *
 *   2. A reduction in precision must round (to nearest, ties away
 *      from zero for nonnegative values) and the result must be
 *      saturated to the range representable with the target precision
 *      and signedness.  In particular, a negative value must never
 *      wrap around to a large unsigned value.
 *
 *   3. An increase in precision must shift left by the difference
 *      in bit widths.
 *
 *   4. A conversion between components with identical signedness and
 *      precision must not alter the sample values at all.
 */

#include <stdio.h>
#include <stdlib.h>

#include <jasper/jasper.h>

static int num_failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, \
			  __LINE__, #cond); \
			++num_failures; \
		} \
	} while (0)

/*
 * Create a single-component image with the specified signedness and
 * precision, containing the given sample values (in a single row).
 */
static jas_image_t *make_gray_image(int sgnd, unsigned prec,
  const long *vals, unsigned numvals)
{
	jas_image_cmptparm_t cmptparm;
	cmptparm.tlx = 0;
	cmptparm.tly = 0;
	cmptparm.hstep = 1;
	cmptparm.vstep = 1;
	cmptparm.width = numvals;
	cmptparm.height = 1;
	cmptparm.prec = prec;
	cmptparm.sgnd = sgnd;
	jas_image_t *image = jas_image_create(1, &cmptparm, JAS_CLRSPC_SGRAY);
	if (!image) {
		return 0;
	}
	jas_image_setcmpttype(image, 0, JAS_IMAGE_CT_COLOR(0));
	jas_cmprof_t *prof = jas_cmprof_createfromclrspc(JAS_CLRSPC_SGRAY);
	if (!prof) {
		jas_image_destroy(image);
		return 0;
	}
	jas_image_setcmprof(image, prof);
	for (unsigned i = 0; i < numvals; ++i) {
		jas_image_writecmptsample(image, 0, i, 0, vals[i]);
	}
	return image;
}

/*
 * Convert the single component of the given image to the specified
 * signedness and precision (via jas_image_sampcmpt) and check that the
 * resulting sample values match the expected ones.
 */
static void check_sampcmpt(const char *testname, int oldsgnd, unsigned oldprec,
  const long *invals, unsigned numvals, int newsgnd, unsigned newprec,
  const long *expected)
{
	jas_image_t *image = make_gray_image(oldsgnd, oldprec, invals, numvals);
	if (!image) {
		fprintf(stderr, "%s: cannot create image\n", testname);
		++num_failures;
		return;
	}
	if (jas_image_sampcmpt(image, 0, 1, 0, 0, 1, 1, newsgnd, newprec)) {
		fprintf(stderr, "%s: jas_image_sampcmpt failed\n", testname);
		++num_failures;
		jas_image_destroy(image);
		return;
	}
	/* The new component must have exactly the requested precision and
	  signedness. */
	CHECK(jas_image_cmptprec(image, 1) == (int) newprec);
	CHECK(jas_image_cmptsgnd(image, 1) == newsgnd);
	for (unsigned i = 0; i < numvals; ++i) {
		const long v = jas_image_readcmptsample(image, 1, i, 0);
		if (v != expected[i]) {
			fprintf(stderr,
			  "%s: sample %u: input %ld, expected %ld, got %ld\n",
			  testname, i, invals[i], expected[i], v);
			++num_failures;
		}
	}
	jas_image_destroy(image);
}

/*
 * Test the conversion of signed 16-bit grayscale sample values to
 * unsigned 8-bit ones.  The expected values were computed independently
 * of the JasPer code as follows: add the bias 2^(16 - 1) = 32768 to
 * convert from signed to offset-binary (unsigned) form, then reduce the
 * precision from 16 to 8 bits with rounding (i.e., add 2^7 and shift
 * right by 8), and finally saturate to the range [0, 255].
 */
static void test_signed16_to_unsigned8(void)
{
	static const long invals[] = {
	  -32768, -20000, -1000, -1, 0, 1, 1000, 20000, 30000, 32767
	};
	static const long expected[] = {
	  0, 50, 124, 128, 128, 128, 132, 206, 245, 255
	};
	const unsigned n = sizeof(invals) / sizeof(invals[0]);
	check_sampcmpt("s16->u8", 1, 16, invals, n, 0, 8, expected);
}

/*
 * A conversion between components with identical signedness and
 * precision must not alter the sample values (i.e., it must be
 * bit-exact).
 */
static void test_same_format_is_identity(void)
{
	static const long u8vals[] = {0, 1, 127, 128, 200, 255};
	static const long s16vals[] = {-32768, -20000, -1, 0, 1, 20000, 32767};
	check_sampcmpt("u8->u8", 0, 8, u8vals, 6, 0, 8, u8vals);
	check_sampcmpt("s16->s16", 1, 16, s16vals, 7, 1, 16, s16vals);
}

/*
 * An increase in precision must shift left by the difference in
 * bit widths.
 */
static void test_precision_increase(void)
{
	static const long invals[] = {0, 1, 128, 255};
	static const long expected[] = {0, 256, 32768, 65280};
	check_sampcmpt("u8->u16", 0, 8, invals, 4, 0, 16, expected);
}

/*
 * A reduction in precision must round and saturate to the
 * representable range.
 */
static void test_precision_decrease(void)
{
	static const long invals[] = {0, 127, 128, 255, 32768, 65407, 65535};
	static const long expected[] = {0, 0, 1, 1, 128, 255, 255};
	check_sampcmpt("u16->u8", 0, 16, invals, 7, 0, 8, expected);
}

/*
 * A change in signedness (with no change in precision) must apply
 * the bias 2^(prec - 1).
 */
static void test_signedness_change(void)
{
	static const long u8in[] = {0, 1, 128, 255};
	static const long s8out[] = {-128, -127, 0, 127};
	static const long s8in[] = {-128, -1, 0, 127};
	static const long u8out[] = {0, 127, 128, 255};
	check_sampcmpt("u8->s8", 0, 8, u8in, 4, 1, 8, s8out);
	check_sampcmpt("s8->u8", 1, 8, s8in, 4, 0, 8, u8out);
}

/*
 * Test the conversion of a signed 16-bit grayscale image to the sRGB
 * color space (i.e., grayscale to RGB).  The output must have three
 * unsigned 8-bit components, and the output sample values must be
 * monotonically nondecreasing for an increasing input ramp (i.e.,
 * darker inputs must not wrap around to bright outputs).
 */
static void test_gray_to_rgb(void)
{
	static const long invals[] = {
	  -32768, -20000, -1000, -1, 0, 1, 1000, 20000, 30000, 32767
	};
	const unsigned n = sizeof(invals) / sizeof(invals[0]);
	jas_image_t *image = make_gray_image(1, 16, invals, n);
	if (!image) {
		fprintf(stderr, "gray->rgb: cannot create image\n");
		++num_failures;
		return;
	}
	jas_cmprof_t *outprof = jas_cmprof_createfromclrspc(JAS_CLRSPC_SRGB);
	if (!outprof) {
		fprintf(stderr, "gray->rgb: cannot create sRGB profile\n");
		++num_failures;
		jas_image_destroy(image);
		return;
	}
	jas_image_t *outimage = jas_image_chclrspc(image, outprof,
	  JAS_CMXFORM_INTENT_PER);
	jas_cmprof_destroy(outprof);
	jas_image_destroy(image);
	if (!outimage) {
		fprintf(stderr, "gray->rgb: jas_image_chclrspc failed\n");
		++num_failures;
		return;
	}
	/* The output must have three unsigned 8-bit components. */
	CHECK(jas_image_numcmpts(outimage) == 3);
	for (int cmpt = 0; cmpt < 3; ++cmpt) {
		CHECK(jas_image_cmptprec(outimage, cmpt) == 8);
		CHECK(jas_image_cmptsgnd(outimage, cmpt) == 0);
	}
	long prev = -1;
	for (unsigned i = 0; i < n; ++i) {
		const long r = jas_image_readcmptsample(outimage, 0, i, 0);
		const long g = jas_image_readcmptsample(outimage, 1, i, 0);
		const long b = jas_image_readcmptsample(outimage, 2, i, 0);
		/* The output must be in the 8-bit unsigned range. */
		CHECK(r >= 0 && r <= 255);
		CHECK(g >= 0 && g <= 255);
		CHECK(b >= 0 && b <= 255);
		/* A grayscale input must yield (nearly) equal color components;
		  the values may differ by one due to rounding. */
		CHECK(labs(r - g) <= 1 && labs(g - b) <= 1);
		/* The output must be monotonically nondecreasing (i.e., no
		  wraparound of dark values to bright ones). */
		CHECK(r >= prev);
		prev = r;
	}
	jas_image_destroy(outimage);
}

/*
 * Test the conversion of an RGB image to grayscale.  Black must map
 * to black, white must map to white, and an arbitrary color must map
 * to a value in between.
 */
static void test_rgb_to_gray(void)
{
	jas_image_cmptparm_t cmptparm;
	cmptparm.tlx = 0;
	cmptparm.tly = 0;
	cmptparm.hstep = 1;
	cmptparm.vstep = 1;
	cmptparm.width = 3;
	cmptparm.height = 1;
	cmptparm.prec = 8;
	cmptparm.sgnd = 0;
	jas_image_cmptparm_t cmptparms[3];
	for (int i = 0; i < 3; ++i) {
		cmptparms[i] = cmptparm;
	}
	jas_image_t *image = jas_image_create(3, cmptparms, JAS_CLRSPC_SRGB);
	if (!image) {
		fprintf(stderr, "rgb->gray: cannot create image\n");
		++num_failures;
		return;
	}
	for (int i = 0; i < 3; ++i) {
		jas_image_setcmpttype(image, i, JAS_IMAGE_CT_COLOR(i));
	}
	jas_cmprof_t *inprof = jas_cmprof_createfromclrspc(JAS_CLRSPC_SRGB);
	if (!inprof) {
		fprintf(stderr, "rgb->gray: cannot create sRGB profile\n");
		++num_failures;
		jas_image_destroy(image);
		return;
	}
	jas_image_setcmprof(image, inprof);
	/* pixel 0: black; pixel 1: white; pixel 2: red */
	static const unsigned char rgb[3][3] = {
	  {0, 0, 0}, {255, 255, 255}, {255, 0, 0}
	};
	for (int x = 0; x < 3; ++x) {
		for (int c = 0; c < 3; ++c) {
			jas_image_writecmptsample(image, c, x, 0, rgb[x][c]);
		}
	}
	jas_cmprof_t *outprof = jas_cmprof_createfromclrspc(JAS_CLRSPC_SGRAY);
	if (!outprof) {
		fprintf(stderr, "rgb->gray: cannot create sGray profile\n");
		++num_failures;
		jas_image_destroy(image);
		return;
	}
	jas_image_t *outimage = jas_image_chclrspc(image, outprof,
	  JAS_CMXFORM_INTENT_PER);
	jas_cmprof_destroy(outprof);
	jas_image_destroy(image);
	if (!outimage) {
		fprintf(stderr, "rgb->gray: jas_image_chclrspc failed\n");
		++num_failures;
		return;
	}
	/* The output must have a single unsigned 8-bit component. */
	CHECK(jas_image_numcmpts(outimage) == 1);
	CHECK(jas_image_cmptprec(outimage, 0) == 8);
	CHECK(jas_image_cmptsgnd(outimage, 0) == 0);
	const long black = jas_image_readcmptsample(outimage, 0, 0, 0);
	const long white = jas_image_readcmptsample(outimage, 0, 1, 0);
	const long red = jas_image_readcmptsample(outimage, 0, 2, 0);
	CHECK(black == 0);
	CHECK(white == 255);
	CHECK(red > black && red < white);
	jas_image_destroy(outimage);
}

int main(int argc, char **argv)
{
	JAS_UNUSED(argc);
	JAS_UNUSED(argv);

	jas_conf_clear();
	jas_conf_set_max_mem_usage(jas_get_total_mem_size());
	jas_conf_set_debug_level(0);
	if (jas_init_library()) {
		fprintf(stderr, "cannot initialize JasPer library\n");
		return EXIT_FAILURE;
	}
	if (jas_init_thread()) {
		fprintf(stderr, "cannot initialize thread\n");
		return EXIT_FAILURE;
	}

	test_signed16_to_unsigned8();
	test_same_format_is_identity();
	test_precision_increase();
	test_precision_decrease();
	test_signedness_change();
	test_gray_to_rgb();
	test_rgb_to_gray();

	jas_cleanup_thread();
	jas_cleanup_library();

	if (num_failures > 0) {
		fprintf(stderr, "FAILED: %d check(s) failed\n", num_failures);
		return EXIT_FAILURE;
	}
	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
