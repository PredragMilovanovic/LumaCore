#!/usr/bin/env python3
"""Generate the synthetic EULUMDAT sample files in samples/ldt/.

These are idealized luminaires defined by formulas, not measurements of real products. Each
distribution is normalized so that the luminaire flux (integral of I over the sphere) equals
LOR x lamp flux, which lets the C++ flux integration be checked against a known value.

Usage:
    python tools/make_sample_ldt.py
"""

from pathlib import Path

import numpy as np

OUT = Path(__file__).resolve().parent.parent / "samples" / "ldt"


def sphere_integral(c_deg, g_deg, table):
    """Integral of I(C, γ) sin γ dγ dC over the full sphere for the table *as lighting software
    interprets it*: bilinear interpolation between the tabulated angles (C covers 0..360
    inclusive here). Evaluated with the midpoint rule on a fine grid, so sharp beams are
    normalized correctly (sampling the table directly is off by ~0.7 % for a cos^20 spot)."""
    gf = (np.arange(3600) + 0.5) * 180.0 / 3600            # 0.05° steps
    cf = (np.arange(720) + 0.5) * 360.0 / 720               # 0.5° steps
    per_plane = np.array([np.interp(gf, g_deg, row) for row in table])
    fine = np.array([np.interp(cf, c_deg, per_plane[:, j]) for j in range(gf.size)])
    weights = np.sin(np.radians(gf))[:, None]
    return float((fine * weights).sum() * np.radians(180.0 / 3600) * np.radians(360.0 / 720))


def write_ldt(path, *, name, number, isym, ityp, c_all, gammas, stored_planes, lamp_flux,
              lor, dims_mm, lamp_type="LED", watts=20.0):
    """stored_planes: rows of cd/klm for the C-planes the symmetry requires, in file order."""
    dff = 100.0  # all samples emit downward only
    lines = [
        "LumaCore samples (synthetic)",
        str(ityp), str(isym), str(len(c_all)),
        f"{(c_all[1] - c_all[0]) if len(c_all) > 1 else 0:g}",
        str(len(gammas)), f"{gammas[1] - gammas[0]:g}",
        "synthetic", name, number, path.name, "LumaCore make_sample_ldt.py",
        *[f"{v:g}" for v in dims_mm],          # L, B, H, La, Ba, H0, H90, H180, H270
        f"{dff:g}", f"{lor:g}", "1", "0",      # DFF, LORL, conversion factor, tilt
        "1",                                   # lamp sets
        "1", lamp_type, f"{lamp_flux:g}", "4000K", "80", f"{watts:g}",
        *["0.9"] * 10,                         # direct ratios (not used by LumaCore)
        *[f"{c:g}" for c in c_all],
        *[f"{g:g}" for g in gammas],
        *[f"{v:.3f}" for row in stored_planes for v in row],
    ]
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\r\n")
    print(f"wrote {path.relative_to(OUT.parent.parent)}")


def rotational(path, *, name, number, profile, lamp_flux, lor, dims_mm, step=2.5):
    """Rotationally symmetric (Isym 1) distribution I(γ) = k * profile(γ)."""
    gammas = np.arange(0.0, 180.0 + 1e-9, step)
    shape = profile(np.radians(gammas))
    full = np.tile(shape, (2, 1))  # C = 0 and 360 for the integral
    k = lor / 100.0 * 1000.0 / sphere_integral(np.array([0.0, 360.0]), gammas, full)
    write_ldt(path, name=name, number=number, isym=1, ityp=1, c_all=[0.0], gammas=gammas,
              stored_planes=[k * shape], lamp_flux=lamp_flux, lor=lor, dims_mm=dims_mm)


def down_only(f):
    return lambda g: np.where(g <= np.pi / 2, f(np.minimum(g, np.pi / 2)), 0.0)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # 1. Wide downlight: Lambertian, I = I0 cos γ.
    rotational(OUT / "downlight_wide.ldt", name="Downlight wide (Lambertian)", number="LC-DL-W",
               profile=down_only(np.cos), lamp_flux=2000, lor=90,
               dims_mm=[200, 0, 80, 180, 0, 0, 0, 0, 0])

    # 2. Narrow spot: I = I0 cos^20 γ, half-intensity at about 15° (30° beam).
    rotational(OUT / "spot_narrow.ldt", name="Spotlight narrow 30 deg", number="LC-SP-N",
               profile=down_only(lambda g: np.cos(g) ** 20), lamp_flux=1500, lor=85,
               dims_mm=[90, 0, 120, 60, 0, 0, 0, 0, 0])

    # 3. Linear office luminaire (Isym 4): narrow along its length (C0), batwing across (C90).
    c_all = np.arange(0.0, 360.0, 15.0)                 # Mc = 24
    gammas = np.arange(0.0, 180.0 + 1e-9, 5.0)          # Ng = 37
    g = np.radians(gammas)
    along = down_only(lambda x: np.cos(x) ** 1.5)(g)
    across = down_only(lambda x: np.cos(x) * (1.0 + 2.2 * np.sin(x) ** 2))(g)

    def plane(c_deg):
        w = np.sin(np.radians(c_deg)) ** 2              # 0 along C0, 1 along C90
        return (1.0 - w) * along + w * across

    c_full = np.arange(0.0, 360.0 + 1e-9, 15.0)
    full = np.array([plane(c) for c in c_full])
    lor = 80.0
    k = lor / 100.0 * 1000.0 / sphere_integral(c_full, gammas, full)
    stored = [k * plane(c) for c in c_all[: len(c_all) // 4 + 1]]  # C0 .. C90
    write_ldt(OUT / "linear_batwing.ldt", name="Linear office luminaire, batwing",
              number="LC-LN-B", isym=4, ityp=2, c_all=c_all, gammas=gammas,
              stored_planes=stored, lamp_flux=4000, lor=lor,
              dims_mm=[1200, 150, 60, 1150, 120, 0, 0, 0, 0], watts=32.0)


if __name__ == "__main__":
    main()
