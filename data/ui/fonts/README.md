# Voxellancer Flight

Generated for Voxellancer with PixelLab `create_font`, with user approval.
The brief requested a readable geometric sci-fi UI font, without voxel cubes,
extrusion, outlines, shadows, or glow.

- Job: `37f0f40e-9fd4-46dd-a9c0-19adbc4c98df`
- Asset: `4ff19968-c3ee-5b61-ae00-989fc43d617b`
- Weight: Regular; native cell size: 32 pixels.
- Cost: 15 generations.
- PixelLab's asset-usage guidance permits personal and commercial use of
  generated assets. No third-party font was supplied as a reference.

The PNG and TTF are the original generated outputs. The runtime DDS preserves
the PNG's alpha channel with white RGB for tinting, uses DXT5 compression, and
contains one mip level. No font library is required at runtime.
The DDS header declares the full 81,920-byte compressed level size (rather
than a row pitch), as required by the engine's existing DDS loader.

**Verified output layout:** 256x320 pixels, **8 columns and 10 rows**, row-major.
This differs from the generator's advertised 10x8 layout. Glyph order:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.,!?:;'"-+()0123456789/%*=_$
```

Space advances without a quad; unsupported characters use `?`. Glyphs preserve
case. The UI renderer batches each string as textured triangles rather than
loading or rendering voxel letters. Legacy `FontSize` values remain as HUD
layout sizes, not references to the retired voxel font assets.
