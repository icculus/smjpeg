/*
 * jdmerge.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains code for merged upsampling/color conversion.
 *
 * This file combines functions from jdsample.c and jdcolor.c;
 * read those files first to understand what's going on.
 *
 * When the chroma components are to be upsampled by simple replication
 * (ie, box filtering), we can save some work in color conversion by
 * calculating all the output pixels corresponding to a pair of chroma
 * samples at one time.  In the conversion equations
 *	R = Y           + K1 * Cr
 *	G = Y + K2 * Cb + K3 * Cr
 *	B = Y + K4 * Cb
 * only the Y term varies among the group of pixels corresponding to a pair
 * of chroma samples, so the rest of the terms can be calculated just once.
 * At typical sampling ratios, this eliminates half or three-quarters of the
 * multiplications needed for color conversion.
 *
 * This file currently provides implementations for the following cases:
 *	YCbCr => RGB color conversion only.
 *	Sampling ratios of 2h1v or 2h2v.
 *	No scaling needed at upsample time.
 *	Corner-aligned (non-CCIR601) sampling alignment.
 * Other special cases could be added, but in most applications these are
 * the only common cases.  (For uncommon cases we fall back on the more
 * general code in jdsample.c and jdcolor.c.)
 */

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"

#ifdef UPSAMPLE_MERGING_SUPPORTED


/* Private subobject */

typedef struct {
  struct jpeg_upsampler pub;	/* public fields */

  /* Pointer to routine to do actual upsampling/conversion of one row group */
  JMETHOD(void, upmethod, (j_decompress_ptr cinfo,
			   JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
			   JSAMPARRAY output_buf));

  /* Private state for YCC->RGB conversion */
#ifdef CALCULATE_TABLES
  int * Cr_r_tab;		/* => table for Cr to R conversion */
  int * Cb_b_tab;		/* => table for Cb to B conversion */
  int * Cr_g_tab;		/* => table for Cr to G conversion */
  int * Cb_g_tab;		/* => table for Cb to G conversion */
#else
  const int * Cr_r_tab;		/* => table for Cr to R conversion */
  const int * Cb_b_tab;		/* => table for Cb to B conversion */
  const int * Cr_g_tab;		/* => table for Cr to G conversion */
  const int * Cb_g_tab;		/* => table for Cb to G conversion */
#endif

  /* For 2:1 vertical sampling, we produce two output rows at a time.
   * We need a "spare" row buffer to hold the second output row if the
   * application provides just a one-row buffer; we also use the spare
   * to discard the dummy last row if the image height is odd.
   */
  JSAMPROW spare_row;
  boolean spare_full;		/* T if spare buffer is occupied */

  JDIMENSION out_row_width;	/* samples per output row */
  JDIMENSION rows_to_go;	/* counts rows remaining in image */
} my_upsampler;

typedef my_upsampler * my_upsample_ptr;

#define SCALEBITS	16	/* speediest right-shift on some machines */
#define ONE_HALF	((INT32) 1 << (SCALEBITS-1))
#define FIX(x)		((INT32) ((x) * (1L<<SCALEBITS) + 0.5))

/* zebaoth specific */
unsigned int hicolor_r[256];
unsigned int hicolor_g[256];
unsigned int hicolor_b[256];

/* These tables are precalculated translation table values */
static const int gCr_r_tab[] = {
-179, -178, -177, -175, -174, -172, -171, -170, -168, -167, -165, -164, -163, -161, -160, -158, -157, -156, -154, -153, -151, -150, -149, -147, -146, -144, -143, -142, -140, -139, -137, -136, -135, -133, -132, -130, -129, -128, -126, -125, -123, -122, -121, -119, -118, -116, -115, -114, -112, -111, -109, -108, -107, -105, -104, -102, -101, -100, -98, -97, -95, -94, -93, -91, -90, -88, -87, -86, -84, -83, -81, -80, -79, -77, -76, -74, -73, -72, -70, -69, -67, -66, -64, -63, -62, -60, -59, -57, -56, -55, -53, -52, -50, -49, -48, -46, -45, -43, -42, -41, -39, -38, -36, -35, -34, -32, -31, -29, -28, -27, -25, -24, -22, -21, -20, -18, -17, -15, -14, -13, -11, -10, -8, -7, -6, -4, -3, -1, 0, 1, 3, 4, 6, 7, 8, 10, 11, 13, 14, 15, 17, 18, 20, 21, 22, 24, 25, 27, 28, 29, 31, 32, 34, 35, 36, 38, 39, 41, 42, 43, 45, 46, 48, 49, 50, 52, 53, 55, 56, 57, 59, 60, 62, 63, 64, 66, 67, 69, 70, 72, 73, 74, 76, 77, 79, 80, 81, 83, 84, 86, 87, 88, 90, 91, 93, 94, 95, 97, 98, 100, 101, 102, 104, 105, 107, 108, 109, 111, 112, 114, 115, 116, 118, 119, 121, 122, 123, 125, 126, 128, 129, 130, 132, 133, 135, 136, 137, 139, 140, 142, 143, 144, 146, 147, 149, 150, 151, 153, 154, 156, 157, 158, 160, 161, 163, 164, 165, 167, 168, 170, 171, 172, 174, 175, 177, 178, 
};
static const int gCb_b_tab[] = {
-227, -225, -223, -222, -220, -218, -216, -214, -213, -211, -209, -207, -206, -204, -202, -200, -198, -197, -195, -193, -191, -190, -188, -186, -184, -183, -181, -179, -177, -175, -174, -172, -170, -168, -167, -165, -163, -161, -159, -158, -156, -154, -152, -151, -149, -147, -145, -144, -142, -140, -138, -136, -135, -133, -131, -129, -128, -126, -124, -122, -120, -119, -117, -115, -113, -112, -110, -108, -106, -105, -103, -101, -99, -97, -96, -94, -92, -90, -89, -87, -85, -83, -82, -80, -78, -76, -74, -73, -71, -69, -67, -66, -64, -62, -60, -58, -57, -55, -53, -51, -50, -48, -46, -44, -43, -41, -39, -37, -35, -34, -32, -30, -28, -27, -25, -23, -21, -19, -18, -16, -14, -12, -11, -9, -7, -5, -4, -2, 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 25, 27, 28, 30, 32, 34, 35, 37, 39, 41, 43, 44, 46, 48, 50, 51, 53, 55, 57, 58, 60, 62, 64, 66, 67, 69, 71, 73, 74, 76, 78, 80, 82, 83, 85, 87, 89, 90, 92, 94, 96, 97, 99, 101, 103, 105, 106, 108, 110, 112, 113, 115, 117, 119, 120, 122, 124, 126, 128, 129, 131, 133, 135, 136, 138, 140, 142, 144, 145, 147, 149, 151, 152, 154, 156, 158, 159, 161, 163, 165, 167, 168, 170, 172, 174, 175, 177, 179, 181, 183, 184, 186, 188, 190, 191, 193, 195, 197, 198, 200, 202, 204, 206, 207, 209, 211, 213, 214, 216, 218, 220, 222, 223, 225, 
};
static const int gCr_g_tab[] = {
5990656, 5943854, 5897052, 5850250, 5803448, 5756646, 5709844, 5663042, 5616240, 5569438, 5522636, 5475834, 5429032, 5382230, 5335428, 5288626, 5241824, 5195022, 5148220, 5101418, 5054616, 5007814, 4961012, 4914210, 4867408, 4820606, 4773804, 4727002, 4680200, 4633398, 4586596, 4539794, 4492992, 4446190, 4399388, 4352586, 4305784, 4258982, 4212180, 4165378, 4118576, 4071774, 4024972, 3978170, 3931368, 3884566, 3837764, 3790962, 3744160, 3697358, 3650556, 3603754, 3556952, 3510150, 3463348, 3416546, 3369744, 3322942, 3276140, 3229338, 3182536, 3135734, 3088932, 3042130, 2995328, 2948526, 2901724, 2854922, 2808120, 2761318, 2714516, 2667714, 2620912, 2574110, 2527308, 2480506, 2433704, 2386902, 2340100, 2293298, 2246496, 2199694, 2152892, 2106090, 2059288, 2012486, 1965684, 1918882, 1872080, 1825278, 1778476, 1731674, 1684872, 1638070, 1591268, 1544466, 1497664, 1450862, 1404060, 1357258, 1310456, 1263654, 1216852, 1170050, 1123248, 1076446, 1029644, 982842, 936040, 889238, 842436, 795634, 748832, 702030, 655228, 608426, 561624, 514822, 468020, 421218, 374416, 327614, 280812, 234010, 187208, 140406, 93604, 46802, 0, -46802, -93604, -140406, -187208, -234010, -280812, -327614, -374416, -421218, -468020, -514822, -561624, -608426, -655228, -702030, -748832, -795634, -842436, -889238, -936040, -982842, -1029644, -1076446, -1123248, -1170050, -1216852, -1263654, -1310456, -1357258, -1404060, -1450862, -1497664, -1544466, -1591268, -1638070, -1684872, -1731674, -1778476, -1825278, -1872080, -1918882, -1965684, -2012486, -2059288, -2106090, -2152892, -2199694, -2246496, -2293298, -2340100, -2386902, -2433704, -2480506, -2527308, -2574110, -2620912, -2667714, -2714516, -2761318, -2808120, -2854922, -2901724, -2948526, -2995328, -3042130, -3088932, -3135734, -3182536, -3229338, -3276140, -3322942, -3369744, -3416546, -3463348, -3510150, -3556952, -3603754, -3650556, -3697358, -3744160, -3790962, -3837764, -3884566, -3931368, -3978170, -4024972, -4071774, -4118576, -4165378, -4212180, -4258982, -4305784, -4352586, -4399388, -4446190, -4492992, -4539794, -4586596, -4633398, -4680200, -4727002, -4773804, -4820606, -4867408, -4914210, -4961012, -5007814, -5054616, -5101418, -5148220, -5195022, -5241824, -5288626, -5335428, -5382230, -5429032, -5475834, -5522636, -5569438, -5616240, -5663042, -5709844, -5756646, -5803448, -5850250, -5897052, -5943854, 
};
static const int gCb_g_tab[] = {
2919680, 2897126, 2874572, 2852018, 2829464, 2806910, 2784356, 2761802, 2739248, 2716694, 2694140, 2671586, 2649032, 2626478, 2603924, 2581370, 2558816, 2536262, 2513708, 2491154, 2468600, 2446046, 2423492, 2400938, 2378384, 2355830, 2333276, 2310722, 2288168, 2265614, 2243060, 2220506, 2197952, 2175398, 2152844, 2130290, 2107736, 2085182, 2062628, 2040074, 2017520, 1994966, 1972412, 1949858, 1927304, 1904750, 1882196, 1859642, 1837088, 1814534, 1791980, 1769426, 1746872, 1724318, 1701764, 1679210, 1656656, 1634102, 1611548, 1588994, 1566440, 1543886, 1521332, 1498778, 1476224, 1453670, 1431116, 1408562, 1386008, 1363454, 1340900, 1318346, 1295792, 1273238, 1250684, 1228130, 1205576, 1183022, 1160468, 1137914, 1115360, 1092806, 1070252, 1047698, 1025144, 1002590, 980036, 957482, 934928, 912374, 889820, 867266, 844712, 822158, 799604, 777050, 754496, 731942, 709388, 686834, 664280, 641726, 619172, 596618, 574064, 551510, 528956, 506402, 483848, 461294, 438740, 416186, 393632, 371078, 348524, 325970, 303416, 280862, 258308, 235754, 213200, 190646, 168092, 145538, 122984, 100430, 77876, 55322, 32768, 10214, -12340, -34894, -57448, -80002, -102556, -125110, -147664, -170218, -192772, -215326, -237880, -260434, -282988, -305542, -328096, -350650, -373204, -395758, -418312, -440866, -463420, -485974, -508528, -531082, -553636, -576190, -598744, -621298, -643852, -666406, -688960, -711514, -734068, -756622, -779176, -801730, -824284, -846838, -869392, -891946, -914500, -937054, -959608, -982162, -1004716, -1027270, -1049824, -1072378, -1094932, -1117486, -1140040, -1162594, -1185148, -1207702, -1230256, -1252810, -1275364, -1297918, -1320472, -1343026, -1365580, -1388134, -1410688, -1433242, -1455796, -1478350, -1500904, -1523458, -1546012, -1568566, -1591120, -1613674, -1636228, -1658782, -1681336, -1703890, -1726444, -1748998, -1771552, -1794106, -1816660, -1839214, -1861768, -1884322, -1906876, -1929430, -1951984, -1974538, -1997092, -2019646, -2042200, -2064754, -2087308, -2109862, -2132416, -2154970, -2177524, -2200078, -2222632, -2245186, -2267740, -2290294, -2312848, -2335402, -2357956, -2380510, -2403064, -2425618, -2448172, -2470726, -2493280, -2515834, -2538388, -2560942, -2583496, -2606050, -2628604, -2651158, -2673712, -2696266, -2718820, -2741374, -2763928, -2786482, -2809036, -2831590, 
};

/*
 * Initialize tables for YCC->RGB colorspace conversion.
 * This is taken directly from jdcolor.c; see that file for more info.
 */

LOCAL(void)
build_ycc_rgb_table (j_decompress_ptr cinfo)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  int i;
#ifdef CALCULATE_TABLES
  INT32 x;
  SHIFT_TEMPS

  upsample->Cr_r_tab = (int *)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				(MAXJSAMPLE+1) * SIZEOF(int));
  upsample->Cb_b_tab = (int *)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				(MAXJSAMPLE+1) * SIZEOF(int));
  upsample->Cr_g_tab = (INT32 *)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				(MAXJSAMPLE+1) * SIZEOF(INT32));
  upsample->Cb_g_tab = (INT32 *)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				(MAXJSAMPLE+1) * SIZEOF(INT32));

  for (i = 0, x = -CENTERJSAMPLE; i <= MAXJSAMPLE; i++, x++) {
    /* i is the actual input pixel value, in the range 0..MAXJSAMPLE */
    /* The Cb or Cr value we are thinking of is x = i - CENTERJSAMPLE */
    /* Cr=>R value is nearest int to 1.40200 * x */
    upsample->Cr_r_tab[i] = (int)
		    RIGHT_SHIFT(FIX(1.40200) * x + ONE_HALF, SCALEBITS);
    /* Cb=>B value is nearest int to 1.77200 * x */
    upsample->Cb_b_tab[i] = (int)
		    RIGHT_SHIFT(FIX(1.77200) * x + ONE_HALF, SCALEBITS);
    /* Cr=>G value is scaled-up -0.71414 * x */
    upsample->Cr_g_tab[i] = (- FIX(0.71414)) * x;
    /* Cb=>G value is scaled-up -0.34414 * x */
    /* We also add in ONE_HALF so that need not do it in inner loop */
    upsample->Cb_g_tab[i] = (- FIX(0.34414)) * x + ONE_HALF;
  }
#ifdef DUMP_TABLES
printf("static const int gCr_r_tab[] = {\n");
  for (i = 0; i <= MAXJSAMPLE; i++) {
    printf("%d, ", upsample->Cr_r_tab[i]);
  }
printf("\n");
printf("};\n");

printf("static const int gCb_b_tab[] = {\n");
  for (i = 0; i <= MAXJSAMPLE; i++) {
    printf("%d, ", upsample->Cb_b_tab[i]);
  }
printf("\n");
printf("};\n");

printf("static const int gCr_g_tab[] = {\n");
  for (i = 0; i <= MAXJSAMPLE; i++) {
    printf("%d, ", upsample->Cr_g_tab[i]);
  }
printf("\n");
printf("};\n");

printf("static const int gCb_g_tab[] = {\n");
  for (i = 0; i <= MAXJSAMPLE; i++) {
    printf("%d, ", upsample->Cb_g_tab[i]);
  }
printf("\n");
printf("};\n");
exit(0);
#endif
#else
    upsample->Cr_r_tab = gCr_r_tab;
    upsample->Cb_b_tab = gCb_b_tab;
    upsample->Cr_g_tab = gCr_g_tab;
    upsample->Cb_g_tab = gCb_g_tab;
#endif /* CALCULATE_TABLES */
  
  /* hicolor Zebaoth specific: */
  if (cinfo->out_color_space == JCS_RGB16_555 || cinfo->out_color_space == JCS_RGB16_555_DBL)
    for (i = 0; i < 256; i++) {
      hicolor_r[i] = (i >> 3) << 10;
      hicolor_g[i] = (i >> 3) << 5;
      hicolor_b[i] = (i >> 3) ;
    }

  else if (cinfo->out_color_space == JCS_RGB16_565 || cinfo->out_color_space == JCS_RGB16_565_DBL)
    for (i = 0; i < 256; i++) {
      hicolor_r[i] = (i >> 3) << 11;
      hicolor_g[i] = (i >> 2) << 5;
      hicolor_b[i] = (i >> 3) ;
    }
  /* Optimization - double all pixels for free. :) */
  for (i = 0; i < 256; i++) {
    hicolor_r[i] = (hicolor_r[i]<<16)|hicolor_r[i];
    hicolor_g[i] = (hicolor_g[i]<<16)|hicolor_g[i];
    hicolor_b[i] = (hicolor_b[i]<<16)|hicolor_b[i];
  }
}


/*
 * Initialize for an upsampling pass.
 */

METHODDEF(void)
start_pass_merged_upsample (j_decompress_ptr cinfo)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;

  /* Mark the spare buffer empty */
  upsample->spare_full = FALSE;
  /* Initialize total-height counter for detecting bottom of image */
  upsample->rows_to_go = cinfo->output_height;
}


/*
 * Control routine to do upsampling (and color conversion).
 *
 * The control routine just handles the row buffering considerations.
 */

METHODDEF(void)
merged_2v_upsample (j_decompress_ptr cinfo,
		    JSAMPIMAGE input_buf, JDIMENSION *in_row_group_ctr,
		    JDIMENSION in_row_groups_avail,
		    JSAMPARRAY output_buf, JDIMENSION *out_row_ctr,
		    JDIMENSION out_rows_avail)
/* 2:1 vertical sampling case: may need a spare row. */
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  JSAMPROW work_ptrs[2];
  JDIMENSION num_rows;		/* number of rows returned to caller */

  if (upsample->spare_full) {
    /* If we have a spare row saved from a previous cycle, just return it. */
    jcopy_sample_rows(& upsample->spare_row, 0, output_buf + *out_row_ctr, 0,
		      1, upsample->out_row_width);
    num_rows = 1;
    upsample->spare_full = FALSE;
  } else {
    /* Figure number of rows to return to caller. */
    num_rows = 2;
    /* Not more than the distance to the end of the image. */
    if (num_rows > upsample->rows_to_go)
      num_rows = upsample->rows_to_go;
    /* And not more than what the client can accept: */
    out_rows_avail -= *out_row_ctr;
    if (num_rows > out_rows_avail)
      num_rows = out_rows_avail;
    /* Create output pointer array for upsampler. */
    work_ptrs[0] = output_buf[*out_row_ctr];
    if (num_rows > 1) {
      work_ptrs[1] = output_buf[*out_row_ctr + 1];
    } else {
      work_ptrs[1] = upsample->spare_row;
      upsample->spare_full = TRUE;
    }
    /* Now do the upsampling. */
    (*upsample->upmethod) (cinfo, input_buf, *in_row_group_ctr, work_ptrs);
  }

  /* Adjust counts */
  *out_row_ctr += num_rows;
  upsample->rows_to_go -= num_rows;
  /* When the buffer is emptied, declare this input row group consumed */
  if (! upsample->spare_full)
    (*in_row_group_ctr)++;
}


METHODDEF(void)
merged_1v_upsample (j_decompress_ptr cinfo,
		    JSAMPIMAGE input_buf, JDIMENSION *in_row_group_ctr,
		    JDIMENSION in_row_groups_avail,
		    JSAMPARRAY output_buf, JDIMENSION *out_row_ctr,
		    JDIMENSION out_rows_avail)
/* 1:1 vertical sampling case: much easier, never need a spare row. */
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;

  /* Just do the upsampling. */
  (*upsample->upmethod) (cinfo, input_buf, *in_row_group_ctr,
			 output_buf + *out_row_ctr);
  /* Adjust counts */
  (*out_row_ctr)++;
  (*in_row_group_ctr)++;
}


/*
 * These are the routines invoked by the control routines to do
 * the actual upsampling/conversion.  One row group is processed per call.
 *
 * Note: since we may be writing directly into application-supplied buffers,
 * we have to be honest about the output width; we can't assume the buffer
 * has been rounded up to an even width.
 */


/*
 * Upsample and color convert for the case of 2:1 horizontal and 1:1 vertical.
 */

METHODDEF(void)
h2v1_merged_upsample (j_decompress_ptr cinfo,
		      JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
		      JSAMPARRAY output_buf)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  register int y, cred, cgreen, cblue;
  int cb, cr;
  register JSAMPROW outptr;
  JSAMPROW inptr0, inptr1, inptr2;
  JDIMENSION col;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  const int * Crrtab = upsample->Cr_r_tab;
  const int * Cbbtab = upsample->Cb_b_tab;
  const int * Crgtab = upsample->Cr_g_tab;
  const int * Cbgtab = upsample->Cb_g_tab;
  SHIFT_TEMPS

  inptr0 = input_buf[0][in_row_group_ctr];
  inptr1 = input_buf[1][in_row_group_ctr];
  inptr2 = input_buf[2][in_row_group_ctr];
  outptr = output_buf[0];
  /* Loop for each pair of output pixels */
  for (col = cinfo->output_width >> 1; col > 0; col--) {
    /* Do the chroma part of the calculation */
    cb = GETJSAMPLE(*inptr1++);
    cr = GETJSAMPLE(*inptr2++);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    /* Fetch 2 Y values and emit 2 pixels */
    y  = GETJSAMPLE(*inptr0++);
    outptr[RGB_RED] =   range_limit[y + cred];
    outptr[RGB_GREEN] = range_limit[y + cgreen];
    outptr[RGB_BLUE] =  range_limit[y + cblue];
    outptr += RGB_PIXELSIZE;
    y  = GETJSAMPLE(*inptr0++);
    outptr[RGB_RED] =   range_limit[y + cred];
    outptr[RGB_GREEN] = range_limit[y + cgreen];
    outptr[RGB_BLUE] =  range_limit[y + cblue];
    outptr += RGB_PIXELSIZE;
  }
  /* If image width is odd, do the last output column separately */
  if (cinfo->output_width & 1) {
    cb = GETJSAMPLE(*inptr1);
    cr = GETJSAMPLE(*inptr2);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    y  = GETJSAMPLE(*inptr0);
    outptr[RGB_RED] =   range_limit[y + cred];
    outptr[RGB_GREEN] = range_limit[y + cgreen];
    outptr[RGB_BLUE] =  range_limit[y + cblue];
  }
}


/*
 * Upsample and color convert for the case of 2:1 horizontal and 2:1 vertical.
 */

METHODDEF(void)
h2v2_merged_upsample (j_decompress_ptr cinfo,
		      JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
		      JSAMPARRAY output_buf)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  register int y, cred, cgreen, cblue;
  int cb, cr;
  register JSAMPROW outptr0, outptr1;
  JSAMPROW inptr00, inptr01, inptr1, inptr2;
  JDIMENSION col;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  const int * Crrtab = upsample->Cr_r_tab;
  const int * Cbbtab = upsample->Cb_b_tab;
  const int * Crgtab = upsample->Cr_g_tab;
  const int * Cbgtab = upsample->Cb_g_tab;
  SHIFT_TEMPS

  inptr00 = input_buf[0][in_row_group_ctr*2];
  inptr01 = input_buf[0][in_row_group_ctr*2 + 1];
  inptr1 = input_buf[1][in_row_group_ctr];
  inptr2 = input_buf[2][in_row_group_ctr];
  outptr0 = output_buf[0];
  outptr1 = output_buf[1];
  /* Loop for each group of output pixels */
  for (col = cinfo->output_width >> 1; col > 0; col--) {
    /* Do the chroma part of the calculation */
    cb = GETJSAMPLE(*inptr1++);
    cr = GETJSAMPLE(*inptr2++);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    /* Fetch 4 Y values and emit 4 pixels */
    y  = GETJSAMPLE(*inptr00++);
    outptr0[RGB_RED] =   range_limit[y + cred];
    outptr0[RGB_GREEN] = range_limit[y + cgreen];
    outptr0[RGB_BLUE] =  range_limit[y + cblue];
    outptr0 += RGB_PIXELSIZE;
    y  = GETJSAMPLE(*inptr00++);
    outptr0[RGB_RED] =   range_limit[y + cred];
    outptr0[RGB_GREEN] = range_limit[y + cgreen];
    outptr0[RGB_BLUE] =  range_limit[y + cblue];
    outptr0 += RGB_PIXELSIZE;
    y  = GETJSAMPLE(*inptr01++);
    outptr1[RGB_RED] =   range_limit[y + cred];
    outptr1[RGB_GREEN] = range_limit[y + cgreen];
    outptr1[RGB_BLUE] =  range_limit[y + cblue];
    outptr1 += RGB_PIXELSIZE;
    y  = GETJSAMPLE(*inptr01++);
    outptr1[RGB_RED] =   range_limit[y + cred];
    outptr1[RGB_GREEN] = range_limit[y + cgreen];
    outptr1[RGB_BLUE] =  range_limit[y + cblue];
    outptr1 += RGB_PIXELSIZE;
  }
  /* If image width is odd, do the last output column separately */
  if (cinfo->output_width & 1) {
    cb = GETJSAMPLE(*inptr1);
    cr = GETJSAMPLE(*inptr2);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    y  = GETJSAMPLE(*inptr00);
    outptr0[RGB_RED] =   range_limit[y + cred];
    outptr0[RGB_GREEN] = range_limit[y + cgreen];
    outptr0[RGB_BLUE] =  range_limit[y + cblue];
    y  = GETJSAMPLE(*inptr01);
    outptr1[RGB_RED] =   range_limit[y + cred];
    outptr1[RGB_GREEN] = range_limit[y + cgreen];
    outptr1[RGB_BLUE] =  range_limit[y + cblue];
  }
}




/* For hicolor 5,5,5 or 5,6,5 */


METHODDEF(void)
h2v2_merged_upsample_hicolor (j_decompress_ptr cinfo,
		      JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
		      JSAMPARRAY output_buf)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  register int y, cred, cgreen, cblue;
  int cb, cr;
  register unsigned short *outptr0, *outptr1;
  JSAMPROW inptr00, inptr01, inptr1, inptr2;
  JDIMENSION col;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  const int * Crrtab = upsample->Cr_r_tab;
  const int * Cbbtab = upsample->Cb_b_tab;
  const int * Crgtab = upsample->Cr_g_tab;
  const int * Cbgtab = upsample->Cb_g_tab;
  SHIFT_TEMPS

  inptr00 = input_buf[0][in_row_group_ctr*2];
  inptr01 = input_buf[0][in_row_group_ctr*2 + 1];
  inptr1 = input_buf[1][in_row_group_ctr];
  inptr2 = input_buf[2][in_row_group_ctr];
  outptr0 = (unsigned short*) output_buf[0];
  outptr1 = (unsigned short*) output_buf[1];
  /* Loop for each group of output pixels */
  for (col = cinfo->output_width >> 1; col > 0; col--) {
    /* Do the chroma part of the calculation */
    cb = GETJSAMPLE(*inptr1++);
    cr = GETJSAMPLE(*inptr2++);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    /* Fetch 4 Y values and emit 4 pixels */
    y  = GETJSAMPLE(*inptr00++);
    *outptr0 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr0++; 
    y  = GETJSAMPLE(*inptr00++);
    *outptr0 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr0++;
    y  = GETJSAMPLE(*inptr01++);
    *outptr1 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr1++;
    y  = GETJSAMPLE(*inptr01++);
    *outptr1 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr1++;
  }
}







/* For hicolor double pixels (2x1) */

METHODDEF(void)
h2v2_merged_upsample_hicolor_dbl (j_decompress_ptr cinfo,
		      JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
		      JSAMPARRAY output_buf)
{
  my_upsample_ptr upsample = (my_upsample_ptr) cinfo->upsample;
  register int y, cred, cgreen, cblue;
  int cb, cr;
  register unsigned int *outptr0, *outptr1;
  JSAMPROW inptr00, inptr01, inptr1, inptr2;
  JDIMENSION col;
  /* copy these pointers into registers if possible */
  register JSAMPLE * range_limit = cinfo->sample_range_limit;
  const int * Crrtab = upsample->Cr_r_tab;
  const int * Cbbtab = upsample->Cb_b_tab;
  const int * Crgtab = upsample->Cr_g_tab;
  const int * Cbgtab = upsample->Cb_g_tab;
  SHIFT_TEMPS

  inptr00 = input_buf[0][in_row_group_ctr*2];
  inptr01 = input_buf[0][in_row_group_ctr*2 + 1];
  inptr1 = input_buf[1][in_row_group_ctr];
  inptr2 = input_buf[2][in_row_group_ctr];
  outptr0 = (unsigned int*) output_buf[0];
  outptr1 = (unsigned int*) output_buf[1];
  /* Loop for each group of output pixels */
  for (col = cinfo->output_width >> 1; col > 0; col--) {
    /* Do the chroma part of the calculation */
    cb = GETJSAMPLE(*inptr1++);
    cr = GETJSAMPLE(*inptr2++);
    cred = Crrtab[cr];
    cgreen = (int) RIGHT_SHIFT(Cbgtab[cb] + Crgtab[cr], SCALEBITS);
    cblue = Cbbtab[cb];
    /* Fetch 4 Y values and emit 4 pixels */
    y  = GETJSAMPLE(*inptr00++);
    *outptr0 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr0++;
    y  = GETJSAMPLE(*inptr00++);
    *outptr0 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr0++;
    y  = GETJSAMPLE(*inptr01++);
    *outptr1 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr1++;
    y  = GETJSAMPLE(*inptr01++);
    *outptr1 = hicolor_r[range_limit[y + cred]] |
               hicolor_g[range_limit[y + cgreen]] | 	      
               hicolor_b[range_limit[y + cblue]]; 
    outptr1++;
  }
}








/*
 * Module initialization routine for merged upsampling/color conversion.
 *
 * NB: this is called under the conditions determined by use_merged_upsample()
 * in jdmaster.c.  That routine MUST correspond to the actual capabilities
 * of this module; no safety checks are made here.
 */

GLOBAL(void)
jinit_merged_upsampler (j_decompress_ptr cinfo)
{
  my_upsample_ptr upsample;

  upsample = (my_upsample_ptr)
    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				SIZEOF(my_upsampler));
  cinfo->upsample = (struct jpeg_upsampler *) upsample;
  upsample->pub.start_pass = start_pass_merged_upsample;
  upsample->pub.need_context_rows = FALSE;

  upsample->out_row_width = cinfo->output_width * cinfo->out_color_components;

  if (cinfo->max_v_samp_factor == 2) {
    upsample->pub.upsample = merged_2v_upsample;
    /* Zebaoth-specific extension: */
    switch (cinfo->out_color_space) {
      case JCS_RGB16_555:
      case JCS_RGB16_565:
        upsample->upmethod = h2v2_merged_upsample_hicolor;
   	    break;
      case JCS_RGB16_555_DBL:
      case JCS_RGB16_565_DBL:
        upsample->upmethod = h2v2_merged_upsample_hicolor_dbl;
   	    break;
      default:
        upsample->upmethod = h2v2_merged_upsample;
    }
    /* Allocate a spare row buffer */
    upsample->spare_row = (JSAMPROW)
      (*cinfo->mem->alloc_large) ((j_common_ptr) cinfo, JPOOL_IMAGE,
		(size_t) (upsample->out_row_width * SIZEOF(JSAMPLE)));
  } else {
    upsample->pub.upsample = merged_1v_upsample;
    upsample->upmethod = h2v1_merged_upsample;
    /* No spare row needed */
    upsample->spare_row = NULL;
  }

  build_ycc_rgb_table(cinfo);
}

#endif /* UPSAMPLE_MERGING_SUPPORTED */
