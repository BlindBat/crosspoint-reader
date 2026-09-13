#!/usr/bin/env python3
"""Deterministic generator for the image decode test fixtures.

Writes the committed fixtures under:
  test/png_decode/resources/   -- hand-rolled PNGs (pure zlib, no imaging lib)
  test/jpeg_to_bmp/resources/  -- tiny JPEGs frozen as byte blobs below

PNGs are constructed chunk-by-chunk so malformed variants (interlace flag,
bogus bit depths, lying length fields, bad CRCs, missing IDAT, ...) are exact
byte surgery, not encoder output. JPEG bytes were produced once with Pillow
(quality/subsampling noted per fixture) and are embedded zlib+ascii85 so
regeneration never depends on a Pillow version; truncation/dimension-lying
JPEG variants are derived in the tests themselves from these bases.

Idempotent: running it twice produces identical bytes.
"""

import base64
import os
import struct
import sys
import zlib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG_DIR = os.path.join(REPO_ROOT, "test", "png_decode", "resources")
JPEG_DIR = os.path.join(REPO_ROOT, "test", "jpeg_to_bmp", "resources")

# ---------------------------------------------------------------------------
# PNG construction
# ---------------------------------------------------------------------------


def chunk(ctype: bytes, data: bytes, bad_crc: bool = False, lie_len: int = None) -> bytes:
    crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
    if bad_crc:
        crc ^= 0xDEADBEEF
    length = len(data) if lie_len is None else lie_len
    return struct.pack(">I", length) + ctype + data + struct.pack(">I", crc)


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def ihdr(width, height, depth, ctype, comp=0, filt=0, interlace=0, bad_crc=False, lie_len=None):
    body = struct.pack(">IIBBBBB", width & 0xFFFFFFFF, height & 0xFFFFFFFF, depth, ctype, comp, filt, interlace)
    return chunk(b"IHDR", body, bad_crc=bad_crc, lie_len=lie_len)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def filter_rows(raw_rows, bpp, filter_types):
    """Forward-apply PNG filters. raw_rows: list[bytes] of equal length."""
    out = b""
    prev = bytes(len(raw_rows[0])) if raw_rows else b""
    for row, ftype in zip(raw_rows, filter_types):
        enc = bytearray(len(row))
        for i in range(len(row)):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 0:
                enc[i] = row[i]
            elif ftype == 1:
                enc[i] = (row[i] - a) & 0xFF
            elif ftype == 2:
                enc[i] = (row[i] - b) & 0xFF
            elif ftype == 3:
                enc[i] = (row[i] - (a + b) // 2) & 0xFF
            elif ftype == 4:
                enc[i] = (row[i] - paeth(a, b, c)) & 0xFF
            else:
                raise ValueError(ftype)
        out += bytes([ftype]) + bytes(enc)
        prev = row
    return out


def idat_payload(raw_rows, bpp, filter_types=None):
    if filter_types is None:
        filter_types = [0] * len(raw_rows)
    return zlib.compress(filter_rows(raw_rows, bpp, filter_types), 9)


def png(width, height, depth, ctype, raw_rows=None, *, filter_types=None, bpp=1,
        interlace=0, comp=0, filt=0, plte=None, idat_raw=None, idat_bad_crc=False,
        ihdr_lie_len=None, no_idat=False, split_idat=0, inter_idat_chunks=(),
        pre_idat_chunks=(), plte_lie_extra=0, idat_lie_len=None):
    """Assemble a PNG. idat_raw overrides the compressed payload entirely."""
    out = PNG_SIG
    out += ihdr(width, height, depth, ctype, comp, filt, interlace, lie_len=ihdr_lie_len)
    if plte is not None:
        body = b"".join(bytes(rgb) for rgb in plte) + b"\x00" * plte_lie_extra
        out += chunk(b"PLTE", body)
    for ct, cdata in pre_idat_chunks:
        out += chunk(ct, cdata)
    if not no_idat:
        payload = idat_raw if idat_raw is not None else idat_payload(raw_rows, bpp, filter_types)
        if split_idat > 0:
            first, rest = payload[:split_idat], payload[split_idat:]
            out += chunk(b"IDAT", first, bad_crc=idat_bad_crc)
            for ct, cdata in inter_idat_chunks:
                out += chunk(ct, cdata)
            out += chunk(b"IDAT", rest)
        else:
            out += chunk(b"IDAT", payload, bad_crc=idat_bad_crc, lie_len=idat_lie_len)
    out += chunk(b"IEND", b"")
    return out


def gray_rows(width, height, fn):
    return [bytes(fn(x, y) & 0xFF for x in range(width)) for y in range(height)]


def ramp(x, y):
    return x * 16 + y


def pack_bits(values, depth):
    """Pack sub-byte pixel values (MSB first) into a row of bytes."""
    row = bytearray()
    acc, nbits = 0, 0
    for v in values:
        acc = (acc << depth) | (v & ((1 << depth) - 1))
        nbits += depth
        if nbits == 8:
            row.append(acc)
            acc, nbits = 0, 0
    if nbits:
        row.append(acc << (8 - nbits))
    return bytes(row)


def write(path_dir, name, data):
    os.makedirs(path_dir, exist_ok=True)
    path = os.path.join(path_dir, name)
    with open(path, "wb") as f:
        f.write(data)
    print("  %-36s %5d bytes" % (name, len(data)))


def gen_pngs():
    print("PNG fixtures -> %s" % PNG_DIR)
    W = write

    def flat(v, w, h):
        return gray_rows(w, h, lambda x, y: v)

    # --- valid: grayscale 8-bit ---
    W(PNG_DIR, "gray8_flat_white_8x8.png", png(8, 8, 8, 0, flat(255, 8, 8)))
    W(PNG_DIR, "gray8_flat_black_8x8.png", png(8, 8, 8, 0, flat(0, 8, 8)))
    W(PNG_DIR, "gray8_flat_white_4x4.png", png(4, 4, 8, 0, flat(255, 4, 4)))
    W(PNG_DIR, "gray8_flat_white_32x32.png", png(32, 32, 8, 0, flat(255, 32, 32)))
    W(PNG_DIR, "gray8_flat_white_16x8.png", png(16, 8, 8, 0, flat(255, 16, 8)))
    ramp16 = gray_rows(16, 16, ramp)
    W(PNG_DIR, "gray8_ramp_16x16.png", png(16, 16, 8, 0, ramp16))
    # Same pixels, every filter type exercised: output must be byte-identical
    # to gray8_ramp_16x16.png after decode.
    ftypes = [0, 1, 2, 3, 4, 1, 2, 3, 4, 2, 3, 4, 1, 4, 3, 2]
    W(PNG_DIR, "gray8_ramp_16x16_filters.png",
      png(16, 16, 8, 0, ramp16, filter_types=ftypes))
    # Same pixels, IDAT split in two + a tEXt chunk between them.
    W(PNG_DIR, "gray8_ramp_16x16_split_idat.png",
      png(16, 16, 8, 0, ramp16, split_idat=20, inter_idat_chunks=[(b"tEXt", b"k\x00v")]))
    # Same pixels, ancillary chunks before IDAT (skipped by the scanner).
    W(PNG_DIR, "gray8_ramp_16x16_ancillary.png",
      png(16, 16, 8, 0, ramp16, pre_idat_chunks=[(b"gAMA", struct.pack(">I", 45455)),
                                                 (b"tEXt", b"Comment\x00hi")]))
    # Same pixels, IDAT CRC corrupted: decoder never checks CRCs (pinned).
    W(PNG_DIR, "gray8_ramp_16x16_idat_bad_crc.png",
      png(16, 16, 8, 0, ramp16, idat_bad_crc=True))
    # Same pixels, IHDR length field lies (says 20, body is 13): parser reads
    # fixed offsets and never uses the length (pinned).
    W(PNG_DIR, "gray8_ramp_16x16_ihdr_len_lying.png",
      png(16, 16, 8, 0, ramp16, ihdr_lie_len=20))
    W(PNG_DIR, "gray8_ramp_32x32.png", png(32, 32, 8, 0, gray_rows(32, 32, lambda x, y: x * 8 + y * 3)))

    # --- valid: other color types, all encoding the gray ramp so their decode
    # must equal gray8_ramp_16x16.png byte-for-byte ---
    rgb_rows = [bytes(b for x in range(16) for b in (ramp(x, y) & 0xFF,) * 3) for y in range(16)]
    W(PNG_DIR, "rgb8_ramp_16x16.png", png(16, 16, 8, 2, rgb_rows, bpp=3))
    rgba_rows = [bytes(b for x in range(16)
                       for b in ((ramp(x, y) & 0xFF,) * 3 + ((x * 31 + y * 7) & 0xFF,)))
                 for y in range(16)]
    W(PNG_DIR, "rgba8_ramp_16x16.png", png(16, 16, 8, 6, rgba_rows, bpp=4))
    graya_rows = [bytes(b for x in range(16) for b in (ramp(x, y) & 0xFF, (x * 5 + y) & 0xFF))
                  for y in range(16)]
    # Unequal channels pin the (25,50,25)/100 luma weights exactly:
    # pure green (0,255,0) -> 255*50/100 = 127.
    green_rows = [bytes(b for _ in range(8) for b in (0, 255, 0)) for _ in range(8)]
    W(PNG_DIR, "rgb8_flat_green_8x8.png", png(8, 8, 8, 2, green_rows, bpp=3))
    W(PNG_DIR, "gray8_flat_127_8x8.png", png(8, 8, 8, 0, flat(127, 8, 8)))
    W(PNG_DIR, "graya8_ramp_16x16.png", png(16, 16, 8, 4, graya_rows, bpp=2))
    gray16_rows = [bytes(b for x in range(16) for b in (ramp(x, y) & 0xFF, 0xAB)) for y in range(16)]
    W(PNG_DIR, "gray16_ramp_16x16.png", png(16, 16, 16, 0, gray16_rows, bpp=2))

    # --- valid: sub-byte grayscale ---
    checker1 = [pack_bits([(x + y) & 1 for x in range(8)], 1) for y in range(8)]
    W(PNG_DIR, "gray1_checker_8x8.png", png(8, 8, 1, 0, checker1))
    W(PNG_DIR, "gray8_checker_8x8.png",
      png(8, 8, 8, 0, gray_rows(8, 8, lambda x, y: 255 if (x + y) & 1 else 0)))
    gray4 = [pack_bits([x & 15 for x in range(8)], 4) for y in range(8)]
    W(PNG_DIR, "gray4_ramp_8x8.png", png(8, 8, 4, 0, gray4))
    W(PNG_DIR, "gray8_ramp4_8x8.png", png(8, 8, 8, 0, gray_rows(8, 8, lambda x, y: (x & 15) * 17)))

    # --- valid: palette ---
    pal = [(0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255)]
    # Index 200 is out of range for the 4-entry palette: decoder clamps to 0.
    pal_idx = [[(x + y) % 4 if x < 6 else 200 for x in range(8)] for y in range(8)]
    W(PNG_DIR, "palette8_oob_8x8.png",
      png(8, 8, 8, 3, [bytes(r) for r in pal_idx], plte=pal))
    W(PNG_DIR, "gray8_palette_ref_8x8.png",
      png(8, 8, 8, 0, gray_rows(8, 8, lambda x, y: ((x + y) % 4) * 85 if x < 6 else 0)))
    pal4_rows = [pack_bits([(x + y) % 3 if x < 3 else 7 for x in range(4)], 4) for y in range(4)]
    W(PNG_DIR, "palette4_oob_4x4.png", png(4, 4, 4, 3, pal4_rows, plte=pal[:3]))
    W(PNG_DIR, "gray8_palette4_ref_4x4.png",
      png(4, 4, 8, 0, gray_rows(4, 4, lambda x, y: ((x + y) % 3) * 85 if x < 3 else 0)))
    # Palette color type with NO PLTE chunk: every index resolves through the
    # zero-initialized palette to black (pinned lenient behavior).
    W(PNG_DIR, "palette8_no_plte_8x8.png",
      png(8, 8, 8, 3, [bytes(r) for r in pal_idx]))
    # PLTE longer than 256 entries: extra bytes are skipped, entries clamp at 256.
    big_pal = pal + [(9, 9, 9)] * 300
    W(PNG_DIR, "palette8_plte_oversized_8x8.png",
      png(8, 8, 8, 3, [bytes(r) for r in pal_idx], plte=big_pal))

    # --- malformed: rejected by header validation ---
    r8 = flat(255, 8, 8)
    W(PNG_DIR, "interlaced_gray8_8x8.png", png(8, 8, 8, 0, r8, interlace=1))
    W(PNG_DIR, "comp_method1.png", png(8, 8, 8, 0, r8, comp=1))
    W(PNG_DIR, "filter_method1.png", png(8, 8, 8, 0, r8, filt=1))
    W(PNG_DIR, "colortype7.png", png(8, 8, 8, 7, r8))
    W(PNG_DIR, "zero_width.png", png(0, 8, 8, 0, [b""] * 8))
    W(PNG_DIR, "zero_height.png", png(8, 0, 8, 0, []))
    W(PNG_DIR, "width_2049.png", png(2049, 1, 8, 0, [bytes(2049)]))
    W(PNG_DIR, "height_3073.png", png(1, 3073, 8, 0, [b"\x00"] * 3073))
    W(PNG_DIR, "width_huge.png", png(0x40000000, 8, 8, 0, idat_raw=b"x"))

    # --- malformed: stream-level ---
    W(PNG_DIR, "corrupt_deflate.png",
      png(8, 8, 8, 0, idat_raw=b"\x78\x9c" + bytes((i * 37 + 11) & 0xFF for i in range(64))))
    W(PNG_DIR, "corrupt_zlib_header.png",
      png(8, 8, 8, 0, idat_raw=b"\x00\x00" + idat_payload(r8, 1)[2:]))
    W(PNG_DIR, "no_idat.png", png(8, 8, 8, 0, no_idat=True))
    # IDAT length field claims 4 bytes more than are present in the file.
    good_payload = idat_payload(r8, 1)
    W(PNG_DIR, "idat_len_lying.png",
      png(8, 8, 8, 0, idat_raw=good_payload, idat_lie_len=len(good_payload) + 4))
    W(PNG_DIR, "not_a_png.bin", b"GIF89a" + bytes(32))

    # --- bug pins: bit depths the decoder never validates (see PngDecodeTest
    # BitDepthValidationGap tests; these are undefined behavior / OOB reads in
    # PngToBmpConverter::convertScanlineToGray) ---
    W(PNG_DIR, "bitdepth0_gray_8x8.png",
      png(8, 8, 0, 0, [b""] * 8))                      # rawRowBytes == 0
    W(PNG_DIR, "bitdepth3_gray_8x4.png",
      png(8, 4, 3, 0, [bytes(3)] * 4))                 # raw = (8*3+7)/8 = 3
    W(PNG_DIR, "bitdepth16_palette_4x2.png",
      png(4, 2, 16, 3, [bytes(8)] * 2, plte=pal))      # ppb = 8/16 = 0
    W(PNG_DIR, "bitdepth4_rgb_4x2.png",
      png(4, 2, 4, 2, [bytes(12)] * 2))                # 16-bit path reads x*6


def gen_jpegs():
    print("JPEG fixtures -> %s" % JPEG_DIR)
    for name, blob in sorted(JPEG_BLOBS.items()):
        data = zlib.decompress(base64.a85decode(blob))
        write(JPEG_DIR, name, data)


# ---------------------------------------------------------------------------
# Frozen JPEG bytes (Pillow 12.1.0, one-time; see module docstring).
#   gray_*            : mode L, 16x16, quality=95
#   rgb_baseline_444  : RGB 16x16, quality=95, subsampling=0 (4:4:4)
#   rgb_*_420         : RGB 16x16, quality=95, subsampling=2 (4:2:0)
#   progressive_rgb_32: RGB 32x32, quality=90, subsampling=2, progressive
#   gray_64x48        : mode L, 64x48, quality=90
# Pixel patterns: gray ramp (x*17+y*11)%256; RGB ((x*23)%256,(y*19)%256,(x*7+y*13)%256).
# ---------------------------------------------------------------------------
JPEG_BLOBS = {
  "gray_64x48.jpg": (
    "GhT9!GuW3\"(B1:p\"Ls@VRjU)\"X=RS_.@3q3C.`i-]COH,QdFbCQOH/3ge'C4/U\\(<>+26ti0'hqM@.nUI=%sOP4gRP;\\G@h3"
    "HO>qq1$-UrHI^Sn%&0Sk:/q8;h4R%B8eX>U&k<EaoE.9J0U_aWqqoOJ<U1(^gI]Z$rpBd!eE_tE#98ne'QUpf$MpoKYb#)Zu"
    "u$5&I8MEb7JB\\O4;]MkUYKk0F2V7E)9EZ!>HQ)Og6uLjkg(;!u4gIj\"9PXX\\bCjTN3\".<Dc[n\"f_G?V9C88o)M;dXLnANR4W"
    "`OK%,cioW;\"4PX%pff!HWMq'\\-a)Z3h)<iCQm'\")S;fj=8uqi;a^@;U)VDiRS+B,=Xn3KM?c:G6W.hiK?i6a)2t8)1Tl)],,"
    "c/+LnjEa!cYAl`7;6Y\\pGUW5f!9?Wni0<]Utq3oF*XfHAbe(E7.dFlM+mcj-)*uGt9*Z5uf('nPbkN4u^/4u_qQG9W)b'pE@"
    "`W4*+II%!6!Z91SRTrW6$N\"V&%-=)H6,b@210f*V79cIn!D]-B[oI1R/C3OtS+QnDjZh\\NcWPJWRJZ;P]Z_!9'1aWTAC_)q("
    "Th+K'Yk8;9@BQ.$_B>cp\"_Q9,3b$RSZr5\\\\OFMB9$^o\"_NNlO.e?N$<0,Qpmg[+P\\!K#pgM&&9DY]I_.5-$n,U'+rC\\-b\\.E"
    ":2?W86I`oJn/dK6R*Xm:RmN\"kKiFr$QW7U_QgD1.hXofYm\"O?e&[r*sW[p;+<[$Sn;Y29T[ubLgRlSN.f7;nF/(pPr<)$/.6"
    "NGHfLXAgR3SIK,tQ2</Rm3m?DH&F0Lh2=a^?U(n/,#G@--+7F'MtL\"V4&4u5,<]ZK)r:b4T>1D$/7J_RSH*mnFInb)4Iro.A"
    "/]]TWBn#F%0,NbutCfsjE9Xhq;!5G9umc?6PSJ:@Tgc/*/2Qfh:2l;3\\*M'tbIs')09sm]:3`Nu,AR$.Mj'i$*0sZXt\"MN!A"
    "%p9<!VM[/N>8=d`E-?*4f6_pSp4d9V/V)q'Z#L\"4!)U4p:q`W\\1jXU#IYIXfp3J\\C])T-#4;sB<ia9GC1ejt<<:B2Sr>Het*"
    "]'@kqk)nt,h5A.TfCX@jL+b^p:_J#=!d.&Y@to_^o\\d7XRA2m!-<0N,HI&JabnlRgXsc$W,i\\=W7r>UA3lf.EI^#@1*B:]'3"
    "o9`Q_s'qVmY#=d6qSl4C4W+-\\DYB$D7[tpV0)0Fa2e6qdc:d?;'A.[O;,as6?a7oJm+E!@[&OU=C)0f_UI\">J$M`@,&Qa0#."
    "k]=oKuSruX8BZ0bC8Y0r9>^Me@R*T%Xk;V2aVJd_@ucDR:9IT\\KI[e%;Y[-FAZP:Brf&=BoUM`.5:m\\ZBtm=cNQLhVu6&TRB"
    "6\\L7I6%Htj4!n9Ne^k4@<Y/Q6uFs3dS7,OHK]2^@?W)MN*;e!FS,$d0bGh/!jE\"@9OL6]tEG5lt\"GFKnom/EM[\",3`F.q)Ua"
    "Cm7siMs\\+Q+Q9bZ<M(?eoGhqs+;[q=NA[pPU1_QM+12ROA@VM!c4!jTAlat/nnBSY+*RLUUXl@pj)GlI_\\i5Ka^-%P2EW5\"O"
    "XNX[:Dd\"m;<'XZc+dLrO:u2n)/l=ehHIA%;nPd4X;/f.=c9=hhp;V#([ZcSO<$IFN\\8BA\"ZDU/Ltk[?qEDG)@ClT^ELTLXUQ"
    "as\\*F[>g2ks<-3t%nfHc';Eor/X,lD(LE?f/3s&A?BVj?T\\i:_^@pUuJ`ZIk;\\2<rdBQ:7BCF;Fa.):VbT***ZLfjd<@*d[>"
    "i`@&rug_]]J[:o<hmmA$cReL]`'&WXE2Pp9Mii0Hn2TF3bPEQ$r:3nd-u4Q3Yl8<2/VIP'ASS<+sNZtq.t$gB^72P/<?qWZ+"
    "LHf'N77QMu?nnoY26?!iH/9%N$H<'%\"(ZG#qfcH0*Ec9fQkk5uFq'0`!f^?997d1^-b0K(k/o/Sc2<W]Y^h-Hq:`SR>G;dY<"
    ":VLgl>Z[6#a9h_bS<P4fbY5tR8JR#e*G60hs'aU/=eEu#59.$c.P7'eeEA[.ZFR;Sg7BsQ]+=Yg1Pa_oMb$s_Rn&V1dJtLKZ"
    "C&paZd#D<3l:5il6loI`WTdFo-,!Fa1R(EHY.Yp%K#qmGVMePrFnL,$6MYJ\\5qMJm<-5b#><$<C0GiD(&m%/YM+uRGQ3/a<a"
    "2Y%AaS%m'?P0Y)@:D)_G.rREbM8@G=VhT%tF-HcFs[!#jaBDO4VB8'',`5q\"rcV`h2/-?!N0!@ZQ)NqL7#*\\aS=JF:\"Ll1e&"
    "RKSuPZ(?Inep\"[GL@49W#2\"t.8Oa(qffq2\\9glEM-ogih$0mq0'A:\"X1`8t56eFfVN&5.Lo0lI6qXRl3lKT+Wqal\\Pil=#X="
    "2Va^KB=Fmu=q<O0R!2/WK332Ua.\\C)DcTDrqi&\\H<3+.f)eM<#@_u2(*'nRkFo@Y5'=),$Vg;jW:0Xq6iQKJ<d'&3*QJ&c8@"
    "^>hJmg[C58<Xg)uYm:[tp=sJ7EUHA$T+us`J;VINWc;k'W9>?lMW\\9\"mApRG6`6[_DTg^qrlqpmZX8T5puV#;Di[(KD\\H^Jf"
    "8O:fIoj?U/k:,R1MSYpT9.@as*>TMF>$fJXK]9A`11stF*[Y2GP\\G3hE0d+TDM4tMU?nSE\\&<>"
  ),
  "gray_baseline.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5b.iZ(q;RC\""
    ";h7Y)$LoW+(:69)U8[J'5.IN0JYAhYd1M9ol:-LC_Zs(aik1#M%s\\5:pq,fcbLaG.\\pbAa<hmoaKAJj@amAW+c!AP`e1!O(I"
    "+i8-P\\LYEf#WG84ok]WG?0^R@'G2>?G&h]=>G_H['i?LP1D97mofmVkg2dB4PThQ^!f$=&`3X\\$WTOGBA\"&RQP0H[-9AYc<@"
    "T\"ajObD=0,Z7C%IXmgU?S'\\a.W$,=de85B68DV9;rC?Ft[Hk,jE\"*^=rHm.lR+Y<IF3r8m7Rmlu:#qL=ES\"6:PNL(ErAeGS4"
    "brb#(&cgYFZIJFE@\\,8ItcG+4%nXnd5XaTJAl#2E.kC;=Rm/>u.a)(dmjf`m5Xj6+ChQeHf=_F?*9YZpTV`h#l[dB7+0k];m"
    "(XL9@lWX<TSo(SMDr&8):&jqeNiBW5p8c/-j?q^MhI?q:q9@(*>F.)sj6bU1$[WL3drrru#hc>CFfB6;@u#.&VuLB0C,Pj3D"
    "uWirW7IDag)lY5KP8nAS%H1'!j\"8UA)[,>TN7PRNGK9#'V2l9s!@^RXOEs"
  ),
  "gray_flat_black.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5b.iZ(q;RC\""
    ";h7Y)$LoW+(:69)U8[J'5.IN0JYAhYd1M9ol:-LC_Zs(aik1#M%s\\5:pq,fcbLaG.\\pbAa<hmoaKAJj@amAW+c!AP`e1!O(I"
    "+i8-P\\LYEf#WG84ok]WG?0^R@'G2>?G&h]=>G_H['i?LP1D97mofmVkg2dB4PThQ^!f$=&`3X\\$WTOGBA\"&RQP0H[-9AYc<@"
    "T\"ajObD=0,Z7C%IXmgU?S'\\a.W$,=de85B68DV9;rC?Ft[Hk,jE\"*^=rHm.lR+Y<IF3r8m7Rmlu:#qL=ES\"6:PNL4AkL(3TS"
    "j#lslRB#4"
  ),
  "gray_flat_white.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5b.iZ(q;RC\""
    ";h7Y)$LoW+(:69)U8[J'5.IN0JYAhYd1M9ol:-LC_Zs(aik1#M%s\\5:pq,fcbLaG.\\pbAa<hmoaKAJj@amAW+c!AP`e1!O(I"
    "+i8-P\\LYEf#WG84ok]WG?0^R@'G2>?G&h]=>G_H['i?LP1D97mofmVkg2dB4PThQ^!f$=&`3X\\$WTOGBA\"&RQP0H[-9AYc<@"
    "T\"ajObD=0,Z7C%IXmgU?S'\\a.W$,=de85B68DV9;rC?Ft[Hk,jE\"*^=rHm.lR+Y<IF3r8m7Rmlu:#qL=ES\"6:PNL;/3RF_Ul"
    "j#lsl^Aq9"
  ),
  "progressive_rgb_32.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3okBM$\\icJ-Ca23ETEe=BN,dg\"A?0\\9N8.\\[]1qq1Jlqq#pN'\\HTnS\"GK9477DLs`"
    "?iGj,_SQR76lS]1'@3RY]@%TEf0@,o88V<rZ>S\\jBtel:gdm7@UKgZA/8_RItU)hkX]<Y3t[\".KoMW@-mQaQaG;%V^rjmlrt"
    "Y\\I:cX^>ZN/@)BC>6>C3aeU1VcgG0OX^6O;WP=(X32\"Pna9K!I>SS.khB6fMhuB`IK:S!6o(cA[8Q(.,+0k.P83OdR\\(hDp3"
    "FWRk'@oI#U=,;Nb0m2tPsgp*(4tRnh84ZZ!M#/1Od)10I0[ljP/')&Scl5q;(D@MfsCR$\\\"7Ssb_gT!:@fhC@\\(?[WglNND;"
    "TYNX*NN%VMZM64pF-Q\"FRmilf&2b<c$mG^(t5\\!ds)&Ro7A4JMBJX&q`Yu+%H0W8sM7BjU%+G66`S-pk_<L!ZdJi:bV<QpH:"
    "&KlF\"h7!92Li.M!B')hcJtJCpGrA@PG0Nh?q/gU=d!SPqBJHj!Yqr+XTVUQm-mYm]GS+W,Ur=8`)F=O]c(7MEH&Kpqk)9^qB"
    ";I`tbA+\\.=Spl'9_#[)XDuFkE./hV%kkH@O6g]t]tHgWiNjWScAo[#)j]@:*P/\"^HJ,<tR4E<Afi-uX0<LF\\N&l31CNMef&b"
    "5ug:U2*52C!j)TVA,i@US02&$U;`=3PZ5<j&At\\h-;/d:8es@aAWl10g&;Yt<j&^tDSMWN!,^[?0iGM^A7/Bn6:!)Zks_'7b"
    "8I#Qb/H=coJ7!Go`o&+Gqu#fr>KYW@3f9<,,`ls-7AaUikcNQap\"N_tl=m'K6,^^Di7HP=\"[BRO[D/XIQ,M>U99MMLN+B3`'"
    "f_!loCZ,)18IUruSbF,#&+ok;cN6'*DBUYWi@UPrriP^M.$P1*;/+>(05A'oJ!jO$h4atk;O560NcY>!jbIZ;)p7Hr]Z)Au#"
    "Vl!JD8C(\\u/)o`<53Cra-;K;[#n=b2<fuALp??U0Rm52<5.QD8OBqR,nc,'Y%Zq>!"
  ),
  "rgb_baseline_420.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5TGk/&?SX-b"
    "fm.nltl#n,_Q+%(^)i9\";h9/)3l&+\"<!U2)?1deO9`@\\($n&U@j-AbM4''_N?.3kDP2T`!^Pp)1BSZB$?S\"![MC:;^`piLUh"
    "Ju5-6/DoY#HfYe2VFkLU,CKK;(XJ5H#DH'fBuOM2=kr8u\\hiCM7INR$sFI=B[_s\\@T4tG^9OKKSG1N6q/K#Uo'u$A7cRCPa7"
    "S9<)tlc['mAdFEUBI4D!fbkqcqs>-rOrdUgU'XB`/Io9QN5[dVkZld4T87unZTEUqVUb?(iQ]7/f&XbVrY5C*+lq2:TC5(<A"
    "+jm0DE5CWS5q2:AdR1TY-qF_C`0.'L8Y_2kmaA`XI+NVF\"BZMO](s&:N.'-t/a<BP\"ET+oiSBAi;+%2<M/olfJQon@H#kAnd"
    "B>f(sM(G:84biIX/I!8.7UY:3$Sp?jr5en=r3srLSc'f5Qi@qF?=0n^=FVQ$r^6:,L*Q<iec09,3ra,@p_E\\NoA?#:B4-#\\p"
    "\\:Ajrs\\lk-Z?-ekH`t'mqVaTq`iU.g\\3qnh#<q>p$K6A;u^-bF-5LtHLC@=5(g3!I.W\\,MVP5KD^9_aF1M*fQGoKJgu81&`P"
    "4&Z%?17&R43HndX+X8SG=dk+1?CDW?p6e*-[hfhOX[jG)gG=0<U\"Y&-'BN!$^8:@f"
  ),
  "rgb_baseline_444.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5TGk/&?SX-b"
    "fm.nltl#n,_Q+%(^)i9\";h9/)$LnF\"<!U2)?1deO9`@\\($n&U@j-AbM4''_N?.3kDP2T`!^Pp)1BSZB$?S\"![MC:;^`piLUh"
    "Ju5-6/DoY#HfYe2VFkLU,CKK;(XJ5H#DH'fBuOM2=kr8u\\hiCM7INR$sFI=B[_s\\@T4tG^9OKKSG1N6q/K#Uo'u$A7cRCPa7"
    "S9<)tlc['mAdFEUBI4D!fbkqcqs>-rOrdUgU'XB`/Io9QN5[dVkZld4T87unZTEUqVUb?(iQ]7/f&XbVrY5C*+lq2:TC5(<A"
    "+jm0DE5CWS5q2:AdR1TY-q?n,GN(fm(?jHLcOf9=r5`g\\\"d?%)D0p\"Q':fti<O[I'$ikgZ[3L,SV5):O#?/uVs0mT_o&`bgR"
    "c\\V0p'3RMOHOSl:>7<X<N53ME(1aX^plYTYphuZ!48VPI0E3il]Lj!So-+.4Y3,2f]B8M[m94_di]fegr'-JH%%mPU!#>EEG"
    "^XABf8!#mn*@a[s(BE^O87m:Z1L85b4Y`5)'!/PhamZ[#F`0h5Jr,ap,i70FPd<ACK.bA5Jr>W0<T\\A54\\[hd$Ra9l@1/[/,"
    "Nb#:sSR]r'0K(r\\I7J;?&;HMZ%DQn%AZ/#E<J7s\"eWhmf0&S^ZEc;D[i<Q51H\\bS,]=h/Up].V1eCbj%K8K5K_RKfl&Ut+85"
    "#gREb\"9(Y:^eCl5hB[FRS>Ut=^O<KDL#$dUAPPL=qn:\\fd`a8Vn7g8b28?,V5IX]_hLrb$I%T)*WdQeRGUec3Aj!;Uq>Hi"
  ),
  "rgb_flat_white_420.jpg": (
    "GhW%Ej8T2c!A?B-2[2`@\"pRO>AGCYD_3ok2&Hk?E'*tfrK1K#Y(1n=?<]2iiB'84T@WjXf%W2bO/eA#gGdt;/5TGk/&?SX-b"
    "fm.nltl#n,_Q+%(^)i9\";h9/)3l&+\"<!U2)?1deO9`@\\($n&U@j-AbM4''_N?.3kDP2T`!^Pp)1BSZB$?S\"![MC:;^`piLUh"
    "Ju5-6/DoY#HfYe2VFkLU,CKK;(XJ5H#DH'fBuOM2=kr8u\\hiCM7INR$sFI=B[_s\\@T4tG^9OKKSG1N6q/K#Uo'u$A7cRCPa7"
    "S9<)tlc['mAdFEUBI4D!fbkqcqs>-rOrdUgU'XB`/Io9QN5[dVkZld4T87unZTEUqVUb?(iQ]7/f&XbVrY5C*+lq2:TC5(<A"
    "+jm0DE5CWS5q2:AdR1TY-qF_C`0.'L8Y_2kmaA`XI+NVF\"BZMO](s&:N.'-t/a<BP\"ET+oiSBAi;+%2<M/olfJQon@H#kAnd"
    "B>f(sM(G:84biIX/I!8.7UY:3$Sp?jr;Et#98!'?9`RiX^\\@"
  ),
}


def main():
    gen_pngs()
    gen_jpegs()
    return 0


if __name__ == "__main__":
    sys.exit(main())
