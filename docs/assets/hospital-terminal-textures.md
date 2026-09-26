# Hospital terminal textures

These three 640×480 RGB textures are original, deterministic Pillow drawings for static hospital props. They use the existing Pinatty Regional Hospital setting and the same fitted, early-1990s screen style as the hospital interior details.

- `records-screen.png` shows a fictional registration menu. It contains no patient names, identifiers, or records.
- `vitals-screen.png` has a simple green ECG-style trace and fixed demo heart-rate value. It is a decorative, static prop display, not medical guidance or live patient information.
- `scale-screen.png` shows a pharmacy bench scale LCD at `0.00 g`, with ready and tare legends. It is a decorative static prop face.

The artwork is drawn in `tools/make_hospital_terminal_textures.py` with fixed per-image random seeds. Text uses available system Arial, Helvetica, or DejaVu fonts; no external image assets or patient data are used. Regenerate with:

```sh
python3 tools/make_hospital_terminal_textures.py
```

The script can also take `--output-dir PATH` and `--contact-sheet PATH` for previews or alternate output locations.
