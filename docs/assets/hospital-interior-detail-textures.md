# Pinatty Regional Hospital interior detail textures

Generated on 2026-09-25 by
[`tools/make_hospital_interior_detail_textures.py`](../../tools/make_hospital_interior_detail_textures.py).
These are original procedural game textures made with Pillow 11.3.0 and the
Python standard library. No reference images, external art, logos, or protected
red-cross emblem were used. The generator fixes a separate random seed for each
asset so edits to one texture do not change the others.

## Assets

All files are opaque RGB PNGs. The seven material swatches are 512 x 512 and
periodic in both axes; wrapped chips, scratches, and lines continue across the
tile edges. Their tonal variation and small-scale detail are restrained for
stable rendering at room scale. Signs are single-face 4:1 images with readable
Pinatty Regional Hospital copy. The screen is a single-face 4:3 fictional 1991
monochrome ultrasound console display.

| File | Dimensions | Intended use | SHA-256 |
| --- | ---: | --- | --- |
| `terrazzo.png` | 512 x 512 | Warm terrazzo floor aggregate | `80f37c799e2cd733944ad671668bf044ae490a621afd1a8085414087ad66267f` |
| `upholstery.png` | 512 x 512 | Subdued teal woven upholstery | `e7a3bb7f77ad920dcf093ba310c02cfcf668a0d4e7469d52e5ff759139dff9ad` |
| `laminate.png` | 512 x 512 | Pale oak laminate grain | `8d7020dfcc5407b9499da54b854e70ed7f1c3d864345900a8e6e183ce8838713` |
| `curtain.png` | 512 x 512 | Pale teal-grey woven curtain | `5f5afc3bab544613e5037464580cb68d40dcc1c0d9f4b6d307dcfb696593df4c` |
| `wallpaint.png` | 512 x 512 | Warm off-white painted wall | `b8409b55d2e8a4544647c086b695086ff6ec0edd7d20a9e69ccbccef9d2605d3` |
| `ceiling.png` | 512 x 512 | Acoustic ceiling panels | `440d5b3da10f10cb73b566a73a27f2b328c74cc0b621b3f4a1594ca45d8dbea3` |
| `steel.png` | 512 x 512 | Brushed satin steel | `88e41f0a9deb7783bd0d6cedf5c047f3f7d85afe08c954993f76c47df4d64cfd` |
| `reception-sign.png` | 1024 x 256 | “RECEPTION / ADMISSIONS • INFORMATION” | `9303e66c95f9bba490358ff7c7c8ccac952a6648850ab9565c0eaf8a14790982` |
| `pharmacy-sign.png` | 1024 x 256 | “PHARMACY / PRESCRIPTIONS • PICKUP” | `21d0e1b2cc58c6aca21edaf0505d928fb7b699f971131bc3e4b4bdd90339aa04` |
| `diagnostics-sign.png` | 1024 x 256 | “DIAGNOSTIC IMAGING / X-RAY • CT • ULTRASOUND” | `1269b786c6f8296f94b3b1f3befe04cce692726e8a3157914487f1c36a503959` |
| `emergency-sign.png` | 1024 x 256 | “EMERGENCY / 24 HOUR • AMBULANCE ENTRANCE” | `6e56c37cbb901795a5ec6d78223c4c19da0e971f850f89fbd0022ee8d7d35141` |
| `ward-sign.png` | 1024 x 256 | “PATIENT WARD / INPATIENT ROOMS” | `3cb9484bd5cd084d0767d112f398442d4d8b0ace7db2a2b4054ef7b5fdeb3bc9` |
| `directory-sign.png` | 1024 x 256 | “HOSPITAL DIRECTORY / LEVEL 1 • CLINICS • WARDS” | `2ced8df40a34ebbc4a6d76ac8ffa1ba76381e9f239ea24beaa2b7ac440be18a5` |
| `screen.png` | 640 x 480 | Fictional 1991 imaging-console CRT face | `bae5dd0e1a3ed3ee4d351bc0e45825f0ad1f01bb99fc4fee6c9c3224ecb8e3a0` |

The sign type uses the installed Arial family (Arial Bold for headings and
Arial Regular for secondary copy). All sign faces share a warm enamel base,
teal lettering, a narrow ochre rule, and distinct line icons. The screen uses
blank patient data and a stylized synthetic scan; it contains no real medical
record or person image.

## Inspection and repeatability

The 14 outputs were inspected together on a contact sheet and individually at
full resolution for textile, wood, signs, and CRT detail. The contact sheet was
kept outside the project at `/tmp/hospital-interior-detail-contact-sheet.png`.
The generator was run a second time into a temporary folder; all 14 PNGs were
byte-identical to the project outputs.
