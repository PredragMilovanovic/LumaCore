# Physics model

LumaCore computes **direct illuminance** on a measurement grid from isotropic point
sources, with hard shadows from opaque obstacles. This page states the model, its units,
its assumptions and its limits, and how each part is verified.

## Quantities and units

| Symbol | Quantity | Unit |
|---|---|---|
| I | luminous intensity of the source (toward the point) | candela, cd = lm/sr |
| r | distance from source to measurement point | metre, m |
| θ | angle between the surface normal and the direction to the source | rad |
| E | illuminance at the point | lux, lx = lm/m² |
| Φ | luminous flux | lumen, lm |
| Ω | solid angle | steradian, sr |

All geometry is in metres in a right-handed coordinate system with +Z up.

## The model

For one source and one point *p* on a surface with unit normal **n**:

```
E = I · cos θ / r²        with  cos θ = n · (s − p) / r,   r = |s − p|
```

which the code evaluates as `I · (n · (s − p)) / r³` (one square root, no explicit cosine).
For several sources the illuminances add (superposition), each source counting only if
the straight segment from *p* to the source is free of obstacles.

Derivation in one line: an isotropic source emits I lumens per steradian; a small
surface patch dA at distance r, tilted by θ, subtends dΩ = dA cos θ / r², so it receives
dΦ = I dΩ and E = dΦ/dA = I cos θ / r².

## Assumptions

1. **Point sources.** Each luminaire is a point: either isotropic (I the same in every direction)
   or with a measured intensity distribution I(C, γ) from an EULUMDAT file (see below).
2. **Direct light only.** No inter-reflections from walls, ceiling or furniture, so there is
   no indirect component. Results are lower than in a real room with light-coloured surfaces.
3. **Opaque, binary occlusion.** A source is either fully visible from the point or fully
   blocked (hard shadows, no penumbra, no transmission).
4. **No attenuation** by the air between source and surface.
5. **Light from behind is ignored.** If cos θ ≤ 0 the source lies behind or level with the
   surface and contributes 0.

## Limitations and how the engine handles them

| Limitation | Handling |
|---|---|
| The inverse-square law holds only far from a luminaire: roughly at distances ≥ 5× its largest dimension (the photometric-distance rule). | Treat results close to a luminaire with care. Points nearer than `kMinLightDistance` (1 mm) get 0 from that source, because E → ∞ as r → 0. |
| Real luminaires are not isotropic: their intensity depends on direction, I(C, γ). | Supported through EULUMDAT (.ldt) files (next section). The luminaire is still treated as a point, which is accurate beyond about 5× its largest dimension. |
| A measurement point inside a solid obstacle (e.g. a work plane cutting through a 2 m cabinet) is not a real surface. | Such points are reported as **NaN (not measured)** and excluded from E_min, E_avg and U0. Points exactly on a surface (e.g. a desk top) are still measured. |
| Gaps between furniture and walls can be in full shadow, so E_min = 0 in the direct-light model. | The CLI excludes a **0.5 m border zone** along the walls by default, as is usual for working-area calculations (EN 12464-1). Change it with `--border`. |
| A shadow ray that starts exactly on a surface may hit that same surface ("shadow acne"). | Shadow rays start `kShadowBias` (1 µm) off the surface along the normal and stop 1 µm short of the source. Tested in `Shadows.SurfaceDoesNotShadowItself`. |
| No maintenance factor, daylight or colour. | Out of scope. |

## Luminaire photometry (EULUMDAT)

Manufacturers publish each luminaire's light distribution as an EULUMDAT (`.ldt`) file, the
European standard format. LumaCore reads it (`native/src/lighting/photometry.cpp`) and uses the
intensity toward each measurement point instead of a constant I.

- **Angles (C-γ system).** γ = 0° points straight down, 90° is horizontal, 180° straight up.
  C = 0° is the luminaire's reference axis (its length, for a linear luminaire); C increases
  counter-clockwise seen from above. In the scene, a luminaire hangs pointing down and can be
  rotated about the vertical axis (`rotation_deg` = scene angle of its C0 axis from +X).
- **Units.** Files store cd per 1000 lm of lamp flux; LumaCore multiplies by lamp flux / 1000
  (lamp set 1) and the file's conversion factor, so the engine works in candela.
- **Symmetry.** Files may store only part of the distribution: one plane (rotationally
  symmetric), half (mirror-symmetric to C0–C180 or C90–C270), or a quadrant (symmetric to both).
  The reader expands every case to the full 0–360° range.
- **Between measured angles** the intensity is interpolated linearly in γ and in C (wrapping
  around 360°). Directions beyond the last measured γ get 0 (e.g. above a downlight measured
  to 90°).
- **Check: luminous flux.** Integrating I over the sphere (Φ = ∫∫ I sin γ dγ dC) gives the
  luminaire's flux, which must equal lamp flux × light output ratio. For the three sample files
  it agrees within 0.1 %.

The sample files in `samples/ldt/` are synthetic (defined by formulas in
`tools/make_sample_ldt.py`, not measurements of real products): a wide Lambertian downlight, a
narrow 30° spot, and a linear office luminaire with a "batwing" distribution. Real manufacturer
files work the same way.

## Work plane and task areas

The work plane is a horizontal calculation surface, by default at 0.75 m (desk height). Two
consequences of the direct-light, point-source model matter when furniture is present:

- **A point inside a closed object can never see a lamp.** Such points are excluded when the object
  is a solid (boxes, spheres), and the web furniture is built so that nothing hollow straddles the
  plane (details in [web.md](web.md)).
- **Real shadow pockets exist.** Right behind a chair backrest, every lamp can be hidden: the
  direct illuminance there really is 0 lx in this model, while a real room fills it with reflected
  light. That is one reason EN 12464-1 judges illuminance and uniformity on the **task area**. The web
  demo reports the desk working zones and tables separately from the whole plane.

## Metrics

Over the measured (non-NaN) points of the grid:

- **E_min, E_max, E_avg** (lx). E_avg is the arithmetic mean over the cell centres.
- **Uniformity U0 = E_min / E_avg** (0 if E_avg = 0). EN 12464-1 sets minimum U0 values per
  task, e.g. 0.6 for many office tasks.

## Numerical method

- The calculation surface is divided into nx × ny equal cells; E is evaluated at each
  **cell centre**. Summing E · (cell area) is the midpoint-rule integral of the flux.
- All arithmetic is in double precision.
- Every executor calls the same per-point kernel and adds the light contributions **in scene
  order**, so the results do not depend on how the work is scheduled (required for the
  bit-identical multithreaded results in Milestone 4).

## Verification

| Check | Expected | Test |
|---|---|---|
| Source at height h directly above the point | E = I / h² (1000 cd at 2 m → 250 lx) | `PointIlluminance.DirectlyBelowIsInverseSquare` |
| Horizontal offset d | E = I · h / (h² + d²)^{3/2}, 1000 random cases, relative error ≤ 1e-12 | `PointIlluminance.HorizontalOffsetMatchesClosedForm_Randomized` |
| Surface tilted by α toward an overhead source | E = (I / h²) · cos α | `PointIlluminance.TiltedSurfaceScalesWithCosine` |
| Several sources | E = Σ E_k | `Superposition.TotalIsSumOfIndividualLights_Randomized` |
| Flux through a square of half-side a, at distance h | Φ = I · Ω with Ω = 4 · asin(a² / (a² + h²)); midpoint sum within 1e-4 | `Simulation.FluxThroughSquareMatchesSolidAngle` |
| Light centred over a square grid | field symmetric in x, y and the diagonal | `Simulation.CenteredLightGivesSymmetricField` |
| Obstacle between source and point | that source contributes 0; others unaffected | `Shadows.*` |
| EULUMDAT symmetry expansion (all 5 types) | mirrored planes return the stored values | `Photometry.Symmetry*`, `Photometry.NoSymmetryWrapsAround360` |
| Luminaire flux of the sample files | ∫∫ I sin γ dγ dC = lamp flux × LOR, within 0.1 % | `Photometry.SampleFilesConserveFlux` |
| Lambertian downlight, offset d | E = I0 h² / (h² + d²)² | `PhotometricLight.IlluminanceUnderALambertianDownlight` |
| Isotropic `.ldt` file | same E as an isotropic light, within 1e-12 | `PhotometricLight.IsotropicFileMatchesIsotropicLight` |

The CLI's `single` scenario also compares every grid point with the closed form and prints
the largest relative error. It is about 6e-16, i.e. double-precision rounding.
