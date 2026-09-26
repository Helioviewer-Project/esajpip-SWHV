# Transcode fixtures

`input/` holds seven JP2 files; `kakadu/` holds, under the same names, what
Kakadu's `kdu_transcode` made of each with
`Corder=RPCL ORGgen_plt=yes Cprecincts={128,128}`, wrapped as JP2 with the
XML box reduced to its root element. `hv_transcode -x` must give the same
files, except for the codestream's COM segments: Kakadu writes its own.

| Input | Content | Progression | Input PLT |
| --- | --- | --- | --- |
| `2015_12_21__00_10_34_34__SDO_AIA_AIA_171.jp2` | SDO/AIA 171, 4096×4096 | RPCL | No |
| `solo_fsi174_510x514_LRCP.jp2` | EUI FSI 174 crop `[1200:1714, 1200:1710]` | LRCP | No |
| `solo_fsi174_511x513_LRCP_PLT.jp2` | EUI FSI 174 crop `[1200:1713, 1200:1711]` | LRCP | Yes |
| `solo_fsi174_509x513_PCRL.jp2` | EUI FSI 174 crop `[1200:1713, 1200:1709]` | PCRL | No |
| `solo_fsi174_127x129_RLCP_PLT.jp2` | EUI FSI 174 crop `[1200:1329, 1200:1327]` | RLCP | Yes |
| `synthetic_rgb_129x129_origin129_CPRL.jp2` | Synthetic, 3 components, grid origin (129,129) | CPRL | No |
| `synthetic_rgb_129x129_CPRL_SOP_EPH.jp2` | Synthetic, 3 components, SOP and EPH markers | CPRL | Yes |

Crops use NumPy's `[row_start:row_end, column_start:column_end]` indexing on
the full EUI FSI 174 image
`solo_L2_eui-fsi174-image_20260919T000055144_V00`. The EUI inputs were
written with OpenJPEG 2.5.4: 6 resolutions, 256×256 precincts, 64×64
code-blocks, layers at compression ratios 16, 8, 4 and 1 (the last
lossless), reversible transform, and the FITS header as XML.

The synthetic inputs have at pixel (x,y) the channels `(x + 3*y) % 256`,
`(5*x + y) % 256` and `x ^ y`, with three resolutions, 256×256 precincts,
64×64 code-blocks and three layers. The origin-129 file's layers are at
compression ratios 4, 2 and 1, with a reversible transform. With 128×128
precincts it has nine packets whose precincts hold no code-blocks; Kakadu
7.10.3 and 8.4.1 both encode them as `80`. The SOP/EPH file uses grid
origin (0,0).

The references were made with Kakadu 7.10.3; for the SOP/EPH file with
`Cuse_sop=no Cuse_eph=no` as well (Kakadu keeps SOP and EPH by default,
`hv_transcode` never writes them). The AIA pair is older: its reference was
made with Kakadu 7.7.

The files, and the script that made them from the FITS source, come from
the hvJP2K repository (`hvJP2K/jp2/test/transcode`). Regenerating them
needs that script and Kakadu; the tests only read them.
