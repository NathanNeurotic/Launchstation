# OSDSYS-ify the dashboard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** OSDSYS-style procedural button icons, two new animated backgrounds, UI animation polish, an audsrv-based SFX subsystem, and CWD-relative theme folders for the wLaunchELF dashboard.

**Architecture:** Four workstreams over the existing dashboard shell (640x448 virtual canvas, gsKit single-buffered GS_ONESHOT, `dashCfg.anim`/`dashCfg.effects` gating). Icons stay procedural (SDF → PSMT8 alpha atlas, drawn as tinted sprites); pad buttons become a two-sprite composite (dark badge tile + coloured glyph tile). Sounds are Sony ADP samples embedded as blobs and played through ps2sdk `audsrv` ADPCM channels, with graceful no-op degradation. Theme folders gain a second, CWD-first root without moving settings storage.

**Tech Stack:** C (EE), gsKit/dmaKit, ps2sdk `audsrv` (EE lib + IRX), ps2build (`ps2.yaml`) primary build, legacy `Makefile`/`embed.make` secondary.

**Spec:** `docs/superpowers/specs/2026-09-26-osdsys-dash-design.md` — read it first; this plan argues from it.

## Global Constraints

- Every task must end with a clean `ps2build build` of `ps2.yaml` (MCP `build_project`, or `ps2build build` in the repo) with **zero new warnings**.
- All new/changed motion must respect `dashCfg.anim` (`DASH_ANIM_OFF` = instant/static, `REDUCED` = shorter/simpler) and `dashCfg.effects` for background scenes, using the existing `dashAnimSpeed()`/`fx` idioms.
- Theme/persistence formats stay backward compatible: theme.cfg and DASHBOARD.CNF store backgrounds as **string ids**, never enum numbers — new enum values must be inserted before `DASH_BG_IMAGE` / appended after existing presets only.
- Icons remain 100% procedural — no PNG/image blobs in the icon path.
- The virtual canvas is `DASH_VW` x `DASH_VH` (640x448) — never hardcode 640x448 or NTSC constants in new draw code.
- gsKit GS_ONESHOT: no per-frame allocations in draw paths; scenes are a few hundred flat/gouraud prims max (PS2 Towers ~500 is the ceiling).
- Embedded-symbol names must be identical in both build systems: `<name>` / `size_<name>`.
- SFX must be pure no-ops when the audio driver is unavailable or `dashCfg.sound == 0` — never crash, never block.
- Reference assets live in `agent_delivery_assist/` (untracked); payloads copied into the repo go under `res/sfx/` only (`bgm.ogg` and the rating PNGs are out of scope).

## Review Focus

1. **Animations OFF** (`Dash_Animations = 0`): a user who disabled animation must see zero motion — no focus pops, no tab overshoot, instant modals. (Pinned in Task 5.)
2. **PAL video mode**: new scenes and icon draws must use `DASH_VW`/`DASH_VH` so 640x512 keeps proportions. (Pinned in Task 3.)
3. **Sound on a console where audsrv fails** (full IOP, driver conflict): every `dashSoundPlay` call site must stay silent-and-safe. (Pinned in Task 7.)
4. **Old DASHBOARD.CNF / theme.cfg files**: loading a config saved by an older build must still resolve every background/preset id. (Pinned in Tasks 3–4.)
5. **Theme folder on a device that is not up yet**: CWD roots must be skipped when the device stack is not loaded, never force a driver load. (Pinned in Task 8.)

---

### Task 1: Pad-icon atlas — badge tile + six new glyphs

**Files:**
- Modify: `include/dash_internal.h:374-418` (icon enum)
- Modify: `src/dash_icons.c` (atlas size, SDF cases, raster gradient, `dashIcon`)

**Interfaces:**
- Consumes: existing SDF helpers `sdBox/sdSeg/sdTriP/sdBoxOutline`, `U()`, sprite batch API (`dashSpriteBatchBegin/Add/End`), `dashIconTint`.
- Produces: new enum range `DI_PAD_BADGE` .. `DI_BTN_SELECT` (contiguous, in that order); `dashIcon()` renders any id in that range (except `DI_PAD_BADGE` itself) as badge+glyph. Task 2 and all hint rows consume this unchanged via `dashIcon()`.

- [ ] **Step 1: Extend the icon enum.** In `include/dash_internal.h`, replace the tail of `enum DashIcon`:

```c
	DI_FILE,
	DI_BACK,
	DI_PAD_BADGE,   /* shared dark rounded-square tile drawn behind pad glyphs */
	DI_BTN_CROSS,
	DI_BTN_CIRCLE,
	DI_BTN_SQUARE,
	DI_BTN_TRIANGLE,
	DI_BTN_UP,
	DI_BTN_DOWN,
	DI_BTN_LEFT,
	DI_BTN_RIGHT,
	DI_BTN_START,
	DI_BTN_SELECT,
	DI_KEY,
	DI_GAMEPAD,
	DI_EXEC,    /* default application icon */
	DI_PALETTE, /* theme settings */
	DI_RECENT,  /* recently used */
	DI_IMAGE,   /* wallpaper / picture */
	DI_COUNT    /* the atlas holds 49 icons (7 x 7) */
};
```

- [ ] **Step 2: Grow the atlas to 7 rows.** In `src/dash_icons.c`:

```c
#define ICON_TEX_H 476
```

(was `408`; `49 icons * 68 px slot / 7 cols = 7 rows`.) Update the file-header comment to `512x476`.

- [ ] **Step 3: Add the badge SDF and the six new glyph SDFs.** In `iconSdf()` (`src/dash_icons.c:145`), add before `case DI_BTN_CROSS:`:

```c
		case DI_PAD_BADGE:
			d = sdBox(p, 0.0f, 0.0f, 0.92f, 0.92f, 0.20f);
			break;
```

and after `case DI_BTN_TRIANGLE:` (`:357`):

```c
		case DI_BTN_UP:
			d = U(sdSeg(p, -0.5f, -0.22f, 0.0f, 0.34f, 0.12f),
			    sdSeg(p, 0.0f, 0.34f, 0.5f, -0.22f, 0.12f));
			break;
		case DI_BTN_DOWN:
			d = U(sdSeg(p, -0.5f, 0.22f, 0.0f, -0.34f, 0.12f),
			    sdSeg(p, 0.0f, -0.34f, 0.5f, 0.22f, 0.12f));
			break;
		case DI_BTN_LEFT:
			d = U(sdSeg(p, 0.22f, -0.5f, -0.34f, 0.0f, 0.12f),
			    sdSeg(p, -0.34f, 0.0f, 0.22f, 0.5f, 0.12f));
			break;
		case DI_BTN_RIGHT:
			d = U(sdSeg(p, -0.22f, -0.5f, 0.34f, 0.0f, 0.12f),
			    sdSeg(p, 0.34f, 0.0f, -0.22f, 0.5f, 0.12f));
			break;
		case DI_BTN_START:
			d = sdTriP(p, -0.36f, -0.44f, -0.36f, 0.44f, 0.44f, 0.0f);
			break;
		case DI_BTN_SELECT:
			d = sdBoxOutline(p, 0.0f, 0.0f, 0.58f, 0.38f, 0.17f, 0.1f);
			break;
```

(The existing four pad glyph cases stay unchanged — they are the glyphs.)

- [ ] **Step 4: Give the badge tile a soft top light.** In the raster loop in `dashIconsInit()` (`src/dash_icons.c:438`), after the `a = dashClampf(...)` line add:

```c
				if (id == DI_PAD_BADGE)
					a *= 0.86f + 0.14f * (1.0f - p.y) * 0.5f; /* soft top light */
```

- [ ] **Step 5: Two-sprite pad rendering in `dashIcon()`.** Replace the body of `dashIcon()` (`src/dash_icons.c:475-489`):

```c
void dashIcon(int id, float x, float y, float size, DashColor c)
{
	float u0, v0, pad;

	if (!icons_ready || id < 0 || id >= DI_COUNT || size <= 0.0f || c.a == 0)
		return;
	if (id < DI_PAD_BADGE || id > DI_BTN_SELECT)
		c = dashIconTint(c);
	u0 = (float)((id % ICON_COLS) * ICON_SLOT);
	v0 = (float)((id / ICON_COLS) * ICON_SLOT);
	pad = size * 2.0f / ICON_SIZE;
	if (id > DI_PAD_BADGE && id <= DI_BTN_SELECT) {
		/* OSDSYS pad tile: fixed dark badge, then the coloured glyph at 56% */
		float bu0 = (float)((DI_PAD_BADGE % ICON_COLS) * ICON_SLOT);
		float bv0 = (float)((DI_PAD_BADGE / ICON_COLS) * ICON_SLOT);
		float g = size * 0.56f, go = (size - g) * 0.5f;

		dashSpriteBatchBegin(&icon_tex, DC(16, 18, 30, 232));
		dashSpriteBatchAdd(x - pad, y - pad, bu0, bv0, x + size + pad, y + size + pad, bu0 + ICON_SLOT, bv0 + ICON_SLOT);
		dashSpriteBatchEnd();
		dashSpriteBatchBegin(&icon_tex, c);
		dashSpriteBatchAdd(x + go, y + go, u0, v0, x + go + g, y + go + g, u0 + ICON_SLOT, v0 + ICON_SLOT);
		dashSpriteBatchEnd();
		return;
	}
	dashSpriteBatchBegin(&icon_tex, c);
	dashSpriteBatchAdd(x - pad, y - pad, u0, v0, x + size + pad, y + size + pad, u0 + ICON_SLOT, v0 + ICON_SLOT);
	dashSpriteBatchEnd();
}
```

(`DI_PAD_BADGE` drawn directly, without the glyph pass, falls through to the normal single-sprite path with the caller's colour — harmless, it is only used as the badge layer.)

- [ ] **Step 6: Build.**

Run the ps2build build (MCP `build_project` on `C:\Users\natha\Github\wLaunchELF_R3Z-master\ps2.yaml`, or `ps2build build`).
Expected: PASS, no new warnings.

- [ ] **Step 7: Commit.**

```bash
git add include/dash_internal.h src/dash_icons.c
git commit -m "dash: OSDSYS badge-style pad icon tiles with dpad/start/select glyphs"
```

---

### Task 2: Button badge restyle in hints and dialogs

**Files:**
- Modify: `src/dash_ui.c:125-150` (`drawButton`)

**Interfaces:**
- Consumes: Task 1's two-sprite `dashIcon()` (pad ids render their own badge).
- Produces: `drawButton()` returns the width consumed (unchanged contract for `dashDrawHints()`/`dashDrawHeader()`); symbol-button hints no longer draw a separate panel badge.

- [ ] **Step 1: Replace `drawButton()`.** The symbol branch drops its panel badge (the atlas tile now *is* the badge); the text branch becomes a squircle badge instead of a pill:

```c
static float drawButton(float x, float y, int icon, const char *key)
{
	const float d = 22.0f;

	if (icon >= 0) {
		dashIcon(icon, x, y, d, buttonColor(icon));
		return d;
	}
	{
		float tw = dashTextWidth(key, DASH_FS_CAPTION);
		float w = tw + 14.0f;
		int keep;
		if (w < d)
			w = d;
		/* OSDSYS squircle badge for L1/R1/START/SELECT-style labels */
		keep = dashTheme.corner;
		dashTheme.corner = DASH_CORNER_SOFT;
		dashPanelGradV(x, y, w, d, 6.0f, DC(30, 36, 54, 235), DC(13, 16, 26, 235));
		dashPanelBorder(x, y, w, d, 6.0f, DC(255, 255, 255, 60));
		dashTheme.corner = keep;
		dashTextS(x + (w - tw) * 0.5f, y + 5.0f, key, DASH_FS_CAPTION, DASH_C_TEXT);
		return w;
	}
}
```

- [ ] **Step 2: Build.** Expected: PASS, no new warnings.

- [ ] **Step 3: Commit.**

```bash
git add src/dash_ui.c
git commit -m "dash: restyle hint buttons as OSDSYS badge tiles"
```

---

### Task 3: New background scenes — streaks and clouds

**Files:**
- Modify: `include/dash_internal.h:96-115` (`enum DashBgKind`), `:811-824` (scene prototypes)
- Modify: `src/dash_themes.c:112-118` (`bg_ids`/`bg_names`)
- Modify: `src/dash_bg_ps2.c` (two new scene functions + local helper)
- Modify: `src/dash_bg.c:435-500` (`drawScene` dispatch)

**Interfaces:**
- Consumes: `dashRectGradV/dashQuadG/dashGlow/dashGfxBlend/dashBgVignette/dashBgHash/dashSinf/dashCosf/dashColorLighten/dashColorLerp/colA` (all existing); `dashTheme.bg1/bg2`.
- Produces: `dashBgSceneStreaks(float t, int fx)` and `dashBgSceneClouds(float t, int fx)` following the existing `dashBgSceneX` signature; new string ids `streaks` / `clouds` accepted in theme.cfg and DASHBOARD.CNF. Task 4 and the theme editor consume these automatically via `DASH_BG_COUNT`.

- [ ] **Step 1: Add the enum values before `DASH_BG_IMAGE`.** In `include/dash_internal.h`:

```c
	DASH_BG_WAVE,      /* PSX-style ribbons */
	DASH_BG_CRYSTAL,   /* the previous dashboard atmosphere */
	DASH_BG_STREAKS,   /* light pillars sweeping across the screen */
	DASH_BG_CLOUDS,    /* layered drifting fog */
	DASH_BG_IMAGE,     /* wallpaper PNG from the theme */
	DASH_BG_COUNT
```

(String-id serialization makes this insertion safe for old configs — `image` still resolves.)

- [ ] **Step 2: Register the ids and names.** In `src/dash_themes.c:112-118`:

```c
static const char *const bg_ids[DASH_BG_COUNT] = {
    "solid", "gradient", "tunnel", "mist", "towers", "disc", "orbit", "clockring", "grid",
    "lines", "cubes", "stream", "tiles", "brushed", "wave", "crystal", "streaks", "clouds", "image"};
static const char *const bg_names[DASH_BG_COUNT] = {
    "Solid colour", "Gradient", "System tunnel", "Blue mist", "PS2 towers", "Disc error red",
    "Orbit lights", "Clock ring", "Memory card grid", "Boot lines", "Wireframe cubes",
    "Data stream", "Memory card tiles", "Brushed console", "Ribbons", "Crystal",
    "Light streaks", "Drift clouds", "Wallpaper image"};
```

- [ ] **Step 3: Declare the prototypes.** In `include/dash_internal.h` after `dashBgSceneWave`:

```c
void dashBgSceneWave(float t, int fx);
void dashBgSceneStreaks(float t, int fx);
void dashBgSceneClouds(float t, int fx);
```

- [ ] **Step 4: Implement the scenes.** Append to `src/dash_bg_ps2.c`:

```c
//--------------------------------------------------------------
// Light streaks (the opening's light pillars)
//--------------------------------------------------------------
static void streak(float x, float yc, float len, float w, float tilt, DashColor c)
{
	float dx = dashSinf(tilt) * len * 0.5f, dy = dashCosf(tilt) * len * 0.5f;
	float px = dashCosf(tilt) * w * 0.5f, py = -dashSinf(tilt) * w * 0.5f;

	dashQuadG(x - dx - px, yc - dy - py, x - dx + px, yc - dy + py,
	          x + dx - px, yc + dy - py, x + dx + px, yc + dy + py, c, c, c, c);
}

void dashBgSceneStreaks(float t, int fx)
{
	DashColor base = dashTheme.bg2, glow = dashTheme.bg1;
	int i, n = (fx == DASH_FX_SUBTLE) ? 7 : 12;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(base, 0.06f), base);
	dashGfxBlend(DASH_BLEND_ADD);
	for (i = 0; i < n; i++) {
		float sp = 26.0f + (i % 4) * 14.0f;              /* px/s horizontal drift */
		float x = fmodf(t * sp + i * 173.0f, (float)(DASH_VW + 240)) - 120.0f;
		float w = 3.0f + (i % 3) * 2.5f;
		float len = 190.0f + 60.0f * dashBgHash(i, 7);
		float yc = DASH_VH * 0.5f + (dashBgHash(i, 3) - 0.5f) * DASH_VH * 0.3f;
		float tilt = 0.22f;
		DashColor c = colA(glow, 24 + (i % 3) * 10);

		streak(x, yc, len, w, tilt, c);
		streak(x, yc, len, w * 0.45f, tilt, colA(dashColorLighten(glow, 0.5f), c.a + 14));
	}
	dashGfxBlend(DASH_BLEND_NORMAL);
	dashBgVignette(0.75f);
}

//--------------------------------------------------------------
// Drift clouds (the illegal-disc cloud field, theme-tinted)
//--------------------------------------------------------------
void dashBgSceneClouds(float t, int fx)
{
	int i, n = (fx == DASH_FX_SUBTLE) ? 5 : 9;

	dashRectGradV(0, 0, DASH_VW, DASH_VH, dashColorLighten(dashTheme.bg2, 0.03f), dashTheme.bg2);
	for (i = 0; i < n; i++) {
		float depth = (i % 3) / 2.0f;                    /* 0 near .. 1 far */
		float sp = 6.0f + 26.0f * depth;                 /* px/s */
		float dir = (i & 1) ? 1.0f : -1.0f;
		float span = (float)(DASH_VW + 360);
		float x = fmodf(dir * t * sp + i * 211.0f, span);

		if (dir < 0.0f)
			x = span - x;
		x -= 180.0f;
		{
			float y = DASH_VH * 0.2f + depth * DASH_VH * 0.6f + dashSinf(t * 0.05f + i * 1.7f) * 14.0f;
			float rx = 150.0f + 90.0f * depth + 30.0f * dashBgHash(i, 11);
			float ry = 44.0f + 26.0f * depth;
			DashColor c = dashColorLerp(dashTheme.bg1, DC(255, 255, 255, 255), 0.25f + 0.2f * depth);

			dashGfxBlend(DASH_BLEND_ADD);
			dashGlow(x, y, rx, ry, colA(c, (int)(20 + 26 * (1.0f - depth))));
			dashGfxBlend(DASH_BLEND_NORMAL);
		}
	}
	dashBgVignette(0.8f);
}
```

- [ ] **Step 5: Wire the dispatch.** In `drawScene()` (`src/dash_bg.c:485` area):

```c
		case DASH_BG_CRYSTAL:
			sceneCrystal(fx);
			break;
		case DASH_BG_STREAKS:
			dashBgSceneStreaks(t, fx);
			break;
		case DASH_BG_CLOUDS:
			dashBgSceneClouds(t, fx);
			break;
		case DASH_BG_IMAGE:
```

- [ ] **Step 6: Build.** Expected: PASS, no new warnings.

- [ ] **Step 7: Commit.**

```bash
git add include/dash_internal.h src/dash_themes.c src/dash_bg.c src/dash_bg_ps2.c
git commit -m "dash: add light-streaks and drift-clouds backgrounds"
```

---

### Task 4: New preset — Light Show

**Files:**
- Modify: `src/dash_themes.c:143-164` (preset enum), `:166-185` (presets table), `dashThemeLoadPreset` switch

**Interfaces:**
- Consumes: `DASH_BG_STREAKS` (Task 3).
- Produces: preset id `lightshow` (index `P_LIGHTSHOW`, appended last so existing preset indices are unchanged).

- [ ] **Step 1: Append the preset enum value.** In `src/dash_themes.c`:

```c
	P_PSX,
	P_CRYSTAL,
	P_LIGHTSHOW,
	P_COUNT
```

- [ ] **Step 2: Append the table entry.** In `presets[]`:

```c
    {"psx", "PSX", "Charcoal and silver ribbons."},
    {"crystal", "Crystal", "The previous dashboard look: towers, stars and glow."},
    {"lightshow", "Light Show", "Light pillars sweeping across an indigo night."}};
```

- [ ] **Step 3: Add the preset body.** In `dashThemeLoadPreset()`'s switch, after `case P_CRYSTAL:` block add:

```c
		case P_LIGHTSHOW:
			t->bg = DASH_BG_STREAKS;
			t->bg1 = DC(96, 140, 255, 255);  /* streak colour */
			t->bg2 = DC(4, 4, 14, 255);      /* night base */
			t->select = DC(40, 80, 170, 160);
			t->sel_text = t->text;
			t->highlight = DASH_HL_BAR;
			break;
```

- [ ] **Step 4: Build.** Expected: PASS. (`dashThemePresetCount()` auto-counts; the Settings theme picker shows it.)

- [ ] **Step 5: Commit.**

```bash
git add src/dash_themes.c
git commit -m "dash: add Light Show preset using the streaks background"
```

---

### Task 5: UI animation polish

**Files:**
- Modify: `src/dash_ui.c` (focus pop helpers, modal easing, tab spring, hints slide)
- Modify: `src/dash_art.c:595` (`dashDrawAppArt` focus scale)
- Modify: `src/dash_main.c` (`dashSectionAnim()` accessor)

**Interfaces:**
- Consumes: `dashEaseOutCubic`, `dashApproach`, `dashAnimSpeed`, `dashTime`.
- Produces: `void dashUiFocusPoke(void)` and `float dashUiFocusPop(void)` (declared in `dash_internal.h`; Task 7's screens may call `dashUiFocusPoke()` on selection change); `float dashSectionAnim(void)` in `dash_main.c` (declared in `dash_internal.h`).

- [ ] **Step 1: Declare the new helpers.** In `include/dash_internal.h`, near the other UI declarations (`:695-715`):

```c
void dashUiInit(void);
void dashUiUpdate(float dt);
void dashUiFocusPoke(void); /* call when the selection moves */
float dashUiFocusPop(void); /* 0.9 -> 1.0 scale spring, 1.0 when animations are off */
```

and in the shell section (`:759-765`):

```c
void dashRenderScene(void);
float dashTime(void);
float dashAnimSpeed(void);
float dashSectionAnim(void); /* 0..1 section-transition clock */
```

- [ ] **Step 2: Implement the pop + modal easing + tab spring + hints slide in `src/dash_ui.c`.**

Add the state and helpers near `dashUiInit()`:

```c
static float focus_pop = 1.0f;

void dashUiFocusPoke(void)
{
	if (dashCfg.anim != DASH_ANIM_OFF)
		focus_pop = 0.0f;
}

float dashUiFocusPop(void)
{
	float k = dashEaseOutCubic(dashClampf(focus_pop, 0.0f, 1.0f));
	return 0.9f + 0.1f * k;
}
```

Advance the clock in `dashUiUpdate()` (add at the top):

```c
	focus_pop += dt * 6.0f;
	if (focus_pop > 1.0f)
		focus_pop = 1.0f;
```

In `dashModalMenu()` (`:637`) and `modalButtons()` (`:731`), replace the open-easing lines with:

```c
		open = dashApproach(open, 1.0f, 16.0f * dashAnimSpeed(), 1.0f / 60.0f);
		{
			float oe = dashEaseOutCubic(open);
```

and use `oe` where `open` fed alpha/offset/backdrop/modalPanel:
`modalBackdrop(oe * (preview != NULL ? 0.25f : 1.0f)); dashGfxPushAlpha(oe); dashGfxPushOffset(0, 18.0f * (1.0f - oe)); modalPanel(x, y, w, h, oe);` (same in `modalButtons` minus the preview term).

In `dashDrawHeader()` (`:281`), replace the chase with a sprung chase:

```c
	if (tab_anim_x < 0.0f)
		tab_anim_x = target_x;
	{
		float dx = target_x - tab_anim_x;
		tab_anim_x = dashApproach(tab_anim_x, target_x, 14.0f * dashAnimSpeed(), 1.0f / 60.0f);
		tab_anim_x += dx * 0.04f * dashClampf(fabsf(dx) / tab_w, 0.0f, 1.0f) * dashSinf(dashTime() * 16.0f);
	}
```

In `dashDrawHints()` (`:164`), add the slide before the button loop (after `x` is clamped):

```c
	x += 14.0f * (1.0f - dashEaseOutCubic(dashSectionAnim()));
```

- [ ] **Step 3: Apply the pop to artwork cards.** In `dashDrawAppArt()` (`src/dash_art.c:595`), after `float r = w * 0.10f;` add:

```c
	if (focus > 0.5f) {
		float k = dashUiFocusPop();
		x -= w * (k - 1.0f) * 0.5f;
		y -= h * (k - 1.0f) * 0.5f;
		w *= k;
		h *= k;
	}
```

- [ ] **Step 4: Add the `dashSectionAnim()` accessor.** In `src/dash_main.c` after `dashAnimSpeed()`:

```c
float dashSectionAnim(void)
{
	return section_anim;
}
```

- [ ] **Step 5: Build.** Expected: PASS, no new warnings.

- [ ] **Step 6: Commit.**

```bash
git add include/dash_internal.h src/dash_ui.c src/dash_art.c src/dash_main.c
git commit -m "dash: animation polish - focus pop, sprung tabs, eased modals, hint slide"
```

---

### Task 6: SFX subsystem — module, payload, build wiring, settings

**Files:**
- Create: `res/sfx/cursor.adp`, `confirm.adp`, `cancel.adp`, `transition.adp`, `message.adp`, `boot.adp`, `bd_connect.adp`, `bd_disconnect.adp` (copied from `agent_delivery_assist/`)
- Create: `include/dash_sound.h`, `src/dash_sound.c`
- Modify: `src/embed_blobs.S` (payload lines), `ps2.yaml` (source, lib, embed_irx), `Makefile` (objs/incs/libs), `embed.make` (bin2s rules)
- Modify: `include/dash_internal.h:469-483` (`DashSettings`), `src/dash_store.c` (defaults, parse, save), `src/dash_settings.c` (two rows)

**Interfaces:**
- Consumes: `audsrv` EE library + IRX (ps2sdk package, already installed); `SifExecModuleBuffer` (via `launchelf.h`, same as `init.c`); embedded symbols `sfx_<n>_adp`/`size_sfx_<n>_adp` and `audsrv_irx`/`size_audsrv_irx` from both build systems.
- Produces: `dash_sound.h` API —

```c
enum DashSound {
	DASH_SND_CURSOR = 0, DASH_SND_CONFIRM, DASH_SND_CANCEL, DASH_SND_TRANSITION,
	DASH_SND_MESSAGE, DASH_SND_BOOT, DASH_SND_CONNECT, DASH_SND_DISCONNECT, DASH_SND_COUNT
};
int dashSoundInit(void);   /* idempotent; loads irx + samples; 1 = ready */
void dashSoundShutdown(void);
void dashSoundPlay(int id); /* no-op unless ready and enabled */
```

Task 7 consumes only `dashSoundPlay`/`dashSoundInit`. `DashSettings` gains `int sound; int sound_vol;` (sound=1, sound_vol=80 defaults) with keys `Dash_Sound` / `Dash_SoundVolume`.

- [ ] **Step 1: Copy the payload files.**

```bash
mkdir -p res/sfx
cp agent_delivery_assist/cursor.adp agent_delivery_assist/confirm.adp agent_delivery_assist/cancel.adp \
   agent_delivery_assist/transition.adp agent_delivery_assist/message.adp agent_delivery_assist/boot.adp \
   agent_delivery_assist/bd_connect.adp agent_delivery_assist/bd_disconnect.adp res/sfx/
```

(`bgm.ogg` stays out — BGM is a separate task.)

- [ ] **Step 2: Embed the payloads for ps2build.** Append to `src/embed_blobs.S`:

```asm
/* Dashboard UI sounds (Sony ADP, played via audsrv). */
EMBED_FILE(sfx_cursor_adp, "res/sfx/cursor.adp")
EMBED_FILE(sfx_confirm_adp, "res/sfx/confirm.adp")
EMBED_FILE(sfx_cancel_adp, "res/sfx/cancel.adp")
EMBED_FILE(sfx_transition_adp, "res/sfx/transition.adp")
EMBED_FILE(sfx_message_adp, "res/sfx/message.adp")
EMBED_FILE(sfx_boot_adp, "res/sfx/boot.adp")
EMBED_FILE(sfx_connect_adp, "res/sfx/bd_connect.adp")
EMBED_FILE(sfx_disconnect_adp, "res/sfx/bd_disconnect.adp")
```

- [ ] **Step 3: Embed for the legacy Makefile.** In `embed.make` (near the other bin2s rules, e.g. after the `poweroff_irx` rule at `:596`):

```make
$(EE_ASM_DIR)sfx_cursor_adp.s: res/sfx/cursor.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_cursor_adp

$(EE_ASM_DIR)sfx_confirm_adp.s: res/sfx/confirm.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_confirm_adp

$(EE_ASM_DIR)sfx_cancel_adp.s: res/sfx/cancel.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_cancel_adp

$(EE_ASM_DIR)sfx_transition_adp.s: res/sfx/transition.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_transition_adp

$(EE_ASM_DIR)sfx_message_adp.s: res/sfx/message.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_message_adp

$(EE_ASM_DIR)sfx_boot_adp.s: res/sfx/boot.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_boot_adp

$(EE_ASM_DIR)sfx_connect_adp.s: res/sfx/bd_connect.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_connect_adp

$(EE_ASM_DIR)sfx_disconnect_adp.s: res/sfx/bd_disconnect.adp | $(EE_ASM_DIR)
	$(BIN2S) $< $@ sfx_disconnect_adp

AUDSRV_SOURCE := $(PS2DEV)/packages/world/audsrv/bin/audsrv.irx
ifeq ($(wildcard $(AUDSRV_SOURCE)),)
  $(error Missing audsrv.irx. Update the PS2BUILD SDK packages)
endif

$(EE_ASM_DIR)audsrv_irx.s: $(AUDSRV_SOURCE) | $(EE_ASM_DIR)
	$(BIN2S) $< $@ audsrv_irx
```

- [ ] **Step 4: Wire the legacy Makefile.** In `Makefile`:
- `EE_OBJS` (line 31-43): add `dash_sound.o audsrv_irx.o sfx_cursor_adp.o sfx_confirm_adp.o sfx_cancel_adp.o sfx_transition_adp.o sfx_message_adp.o sfx_boot_adp.o sfx_connect_adp.o sfx_disconnect_adp.o` (add `dash_sound.o` next to the other `dash_*.o` at line 38; the irx/sfx objects next to `poweroff_irx.o` at line 33).
- After `EE_INCS` (line 45) add:

```make
EE_INCS += -I$(PS2DEV)/packages/world/audsrv/include
```

- After `EE_LDFLAGS` (line 47) add:

```make
EE_LDFLAGS += -L$(PS2DEV)/packages/world/audsrv/lib
```

- In `EE_LIBS` (line 54-55) append `-laudsrv` inside the `--start-group`.

- [ ] **Step 5: Wire ps2.yaml.** In the `wLaunchELF` target:
- `sources:` add `- src/dash_sound.c` (after `src/dash_themes.c`).
- `libs:` add `audsrv` (after `pad`).
- `embed_irx:` add `- audsrv` (after `usbmass_bd`).

- [ ] **Step 6: Write `include/dash_sound.h`.**

```c
//--------------------------------------------------------------
//File name:   dash_sound.h
//Dashboard UI sounds (audsrv ADPCM one-shots). Every call is a
//safe no-op when sound is off or the driver failed to start.
//--------------------------------------------------------------
#ifndef DASH_SOUND_H
#define DASH_SOUND_H

enum DashSound {
	DASH_SND_CURSOR = 0,
	DASH_SND_CONFIRM,
	DASH_SND_CANCEL,
	DASH_SND_TRANSITION,
	DASH_SND_MESSAGE,
	DASH_SND_BOOT,
	DASH_SND_CONNECT,
	DASH_SND_DISCONNECT,
	DASH_SND_COUNT
};

int dashSoundInit(void);
void dashSoundShutdown(void);
void dashSoundPlay(int id);

#endif
```

- [ ] **Step 7: Write `src/dash_sound.c`.**

```c
//--------------------------------------------------------------
//File name:   dash_sound.c
//Dashboard SFX: loads audsrv, uploads the embedded ADP samples and
//plays them on ADPCM channels. Degrades to silent no-ops on any
//failure (driver missing, sample rejected, sound disabled).
//--------------------------------------------------------------
#include "launchelf.h"
#include "dash_internal.h"
#include "dash_sound.h"
#include <audsrv.h>

extern u8 audsrv_irx[];
extern int size_audsrv_irx;
extern u8 sfx_cursor_adp[];
extern int size_sfx_cursor_adp;
extern u8 sfx_confirm_adp[];
extern int size_sfx_confirm_adp;
extern u8 sfx_cancel_adp[];
extern int size_sfx_cancel_adp;
extern u8 sfx_transition_adp[];
extern int size_sfx_transition_adp;
extern u8 sfx_message_adp[];
extern int size_sfx_message_adp;
extern u8 sfx_boot_adp[];
extern int size_sfx_boot_adp;
extern u8 sfx_connect_adp[];
extern int size_sfx_connect_adp;
extern u8 sfx_disconnect_adp[];
extern int size_sfx_disconnect_adp;

static audsrv_adpcm_t samples[DASH_SND_COUNT];
static int sound_ready;

static void *blob_data(int id)
{
	switch (id) {
		case DASH_SND_CURSOR: return sfx_cursor_adp;
		case DASH_SND_CONFIRM: return sfx_confirm_adp;
		case DASH_SND_CANCEL: return sfx_cancel_adp;
		case DASH_SND_TRANSITION: return sfx_transition_adp;
		case DASH_SND_MESSAGE: return sfx_message_adp;
		case DASH_SND_BOOT: return sfx_boot_adp;
		case DASH_SND_CONNECT: return sfx_connect_adp;
		default: return sfx_disconnect_adp;
	}
}

static int blob_size(int id)
{
	switch (id) {
		case DASH_SND_CURSOR: return size_sfx_cursor_adp;
		case DASH_SND_CONFIRM: return size_sfx_confirm_adp;
		case DASH_SND_CANCEL: return size_sfx_cancel_adp;
		case DASH_SND_TRANSITION: return size_sfx_transition_adp;
		case DASH_SND_MESSAGE: return size_sfx_message_adp;
		case DASH_SND_BOOT: return size_sfx_boot_adp;
		case DASH_SND_CONNECT: return size_sfx_connect_adp;
		default: return size_sfx_disconnect_adp;
	}
}

int dashSoundInit(void)
{
	int i, r;

	if (sound_ready)
		return 1;
	if (!dashCfg.sound)
		return 0;
	if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &r) < 0)
		return 0;
	if (audsrv_init() != 0)
		return 0;
	if (audsrv_adpcm_init() != 0) {
		audsrv_quit();
		return 0;
	}
	for (i = 0; i < DASH_SND_COUNT; i++) {
		samples[i].buffer = NULL;
		if (audsrv_load_adpcm(&samples[i], blob_data(i), blob_size(i)) != 0)
			samples[i].buffer = NULL; /* sample stays silent */
	}
	sound_ready = 1;
	return 1;
}

void dashSoundShutdown(void)
{
	if (!sound_ready)
		return;
	audsrv_quit();
	sound_ready = 0;
}

void dashSoundPlay(int id)
{
	static int rr;
	int ch;

	if (!sound_ready || !dashCfg.sound || id < 0 || id >= DASH_SND_COUNT)
		return;
	if (samples[id].buffer == NULL)
		return;
	ch = (id == DASH_SND_CURSOR) ? (rr++ & 1) : 2;
	audsrv_adpcm_set_volume_and_pan(ch, dashCfg.sound_vol * 0x3FFF / 100, 0);
	audsrv_ch_play_adpcm(ch, &samples[id]);
}
```

- [ ] **Step 8: Add the settings.** In `include/dash_internal.h` `DashSettings` add after `int intro_done;`:

```c
	int sound;
	int sound_vol;
```

In `src/dash_store.c`:
- `dashStoreDefaults()` (`:229`): `dashCfg.sound = 1; dashCfg.sound_vol = 80;`
- `settingsKv()` (`:269`, after `Dash_IntroDone`): `else if (!stricmp(key, "Dash_Sound")) dashCfg.sound = clampInt(value, 0, 1, 1); else if (!stricmp(key, "Dash_SoundVolume")) dashCfg.sound_vol = clampInt(value, 0, 100, 80);`
- `dashStoreSaveSettings()` (`:367`): add `"Dash_Sound = %d\r\nDash_SoundVolume = %d\r\n"` to the format and `dashCfg.sound, dashCfg.sound_vol` to the args.

In `src/dash_settings.c` (add `#include "dash_sound.h"` next to the other includes at the top of the file):
- enum `SetId`: add `S_SOUND, S_SVOL` after `S_ANIM`.
- `dash_items[]` (after the `S_ANIM` row): `{ST_CHOICE, S_SOUND, "Interface sounds", "Cursor, confirm and other dashboard sound effects."},` and `{ST_CHOICE, S_SVOL, "Sound volume", NULL},`.
- `choiceInfo()`: `case S_SOUND: *cur = dashCfg.sound; return 2;` and `case S_SVOL: *names = pct_names, *cur = dashCfg.sound_vol / 10; return 11;`.
- `setChoice()`: `case S_SOUND: dashCfg.sound = v; if (v) dashSoundInit(); break;` and `case S_SVOL: dashCfg.sound_vol = v * 10; break;`.

- [ ] **Step 9: Build.** Expected: PASS, no new warnings (audsrv header + lib resolve; embedded symbols link).

- [ ] **Step 10: Commit.**

```bash
git add res/sfx include/dash_sound.h src/dash_sound.c src/embed_blobs.S ps2.yaml Makefile embed.make include/dash_internal.h src/dash_store.c src/dash_settings.c
git commit -m "dash: audsrv-based SFX subsystem with embedded ADP UI sounds"
```

---

### Task 7: SFX hookup — cursor, confirm, cancel, transition, message, boot, devices

**Files:**
- Modify: `src/dash_main.c` (init/re-init, boot, cursor, section), `src/dash_ui.c` (modal + toast sounds), `src/dash_devices.c` (connect/disconnect)

**Interfaces:**
- Consumes: Task 6's `dashSoundInit/Play`, `DASH_SND_*` ids; Task 5's `dashUiFocusPoke` is *not* required here.
- Produces: no new API; behaviour only.

- [ ] **Step 1: Shell hooks in `src/dash_main.c`.**
- Add `#include "dash_sound.h"` with the other includes.
- In `leaveLegacy()` (`:145`) add after `dashInputFlush();`: `dashSoundInit(); /* re-start after IOP resets ran legacy paths */`
- In `DashRun()` after `mainActionLaunchHook = dashLaunchHook;` add: `dashSoundInit(); if (from_boot) dashSoundPlay(DASH_SND_BOOT);`
- In the main loop after `dashInputPoll(&in);` add:

```c
		if (in.pressed & (DASH_IN_UP | DASH_IN_DOWN | DASH_IN_LEFT | DASH_IN_RIGHT))
			dashSoundPlay(DASH_SND_CURSOR);
```

- In `dashSwitchSection()` (`:90`) after `section_anim = 0.0f;`: `dashSoundPlay(DASH_SND_TRANSITION);`

- [ ] **Step 2: Dialog + toast hooks in `src/dash_ui.c`.**
- Add `#include "dash_sound.h"`.
- `dashModalMenu()`: on the OK return path (`:616-619`) add `dashSoundPlay(DASH_SND_CONFIRM);` before `return sel;`; on the BACK/MENU path add `dashSoundPlay(DASH_SND_CANCEL);` before `return -1;`. Add `dashSoundPlay(DASH_SND_CURSOR);` next to the UP/DOWN/L2/R2 handlers.
- `modalButtons()`: before `return (cancel_label == NULL) ? 1 : (sel == 0);` add `dashSoundPlay(DASH_SND_CONFIRM);`; before the BACK return add `dashSoundPlay(DASH_SND_CANCEL);`; add cursor sound on the LEFT/RIGHT toggle.
- `dashToast()` (`:469`): after `ntoasts++;` add `dashSoundPlay(DASH_SND_MESSAGE);`

- [ ] **Step 3: Device attach/detach in `src/dash_devices.c`.**
- Add `#include "dash_sound.h"` and file-scope state:

```c
static u32 dev_snapshot = 0xFFFFFFFFu; /* set on first poll: baseline, no sounds */
```

- Replace `dashDevicesPoll()`:

```c
void dashDevicesPoll(void)
{
	u32 now = 0;
	int i;

	pollRootMemoryCardDevices();
	for (i = 0; i < 2; i++)
		if (filerRootMcPresent(i, NULL, NULL) > 0)
			now |= 1u << i;
	if (filerUsbDriversLoaded()) {
		int m = filerUsbDriveMask();
		for (i = 0; i < 10; i++)
			if (m & (1 << i))
				now |= 1u << (2 + i);
	}
	if (dev_snapshot == 0xFFFFFFFFu)
		dev_snapshot = now;
	else if (now != dev_snapshot) {
		if (now & ~dev_snapshot)
			dashSoundPlay(DASH_SND_CONNECT);
		if (dev_snapshot & ~now)
			dashSoundPlay(DASH_SND_DISCONNECT);
		dev_snapshot = now;
	}
}
```

(`filerUsbDriversLoaded` is already declared in `dash_internal.h:791`.)

- [ ] **Step 4: Build.** Expected: PASS, no new warnings.

- [ ] **Step 5: Commit.**

```bash
git add src/dash_main.c src/dash_ui.c src/dash_devices.c
git commit -m "dash: hook UI sounds to input, dialogs, sections and device events"
```

---

### Task 8: CWD-first theme folders

**Files:**
- Modify: `src/dash_themes.c:930-1070` (`themesRoot`, `dashThemeFolderList`, `dashThemeFolderLoad`, `dashThemeFolderExport`)

**Interfaces:**
- Consumes: `LaunchElfDir` (`include/launchelf.h:336`), `dashStoreDir()`, `isPathDeviceStackLoaded()` (already used at `:939`), `getDir`, `dashLoadTextFile`.
- Produces: unchanged function signatures; new behaviour — theme folders are discovered in `LaunchElfDir/THEMES/` first, then `<store_dir>/THEMES/`; export writes to the first writable root.

- [ ] **Step 1: Replace the single-root helper with a root list.** In `src/dash_themes.c`, first add the small writable-device check next to where `themesRoot()` was (it mirrors `dash_store.c`'s file-local `isWritableDevice`):

```c
static int isWritablePath(const char *path)
{
	return (!strncmp(path, "mc", 2) || !strncmp(path, "mass", 4) || !strncmp(path, "usb", 3) ||
	        !strncmp(path, "hdd", 3) || !strncmp(path, "mmce", 4) || !strncmp(path, "mx4sio", 6) ||
	        !strncmp(path, "ata", 3));
}
```

Then replace `themesRoot()` with:

```c
/* Ordered candidate roots for theme folders: the ELF's own folder first
 * (so themes ride along with the ELF on any device), then the dashboard
 * folder. A root is only offered when its device stack is already loaded;
 * looking at themes never starts a driver. */
static int themeRoots(char roots[2][MAX_PATH])
{
	int n = 0;
	const char *store = dashStoreDir();

	if (LaunchElfDir[0] && isWritablePath(LaunchElfDir) && isPathDeviceStackLoaded(LaunchElfDir))
		dashStrCopy(roots[n++], LaunchElfDir, MAX_PATH);
	if (store != NULL && store[0]) {
		char root[MAX_PATH];

		if (snprintf(root, sizeof(root), "%sTHEMES/", store) < (int)sizeof(root) &&
		    (n == 0 || strcmp(roots[0], root) != 0))
			dashStrCopy(roots[n++], root, MAX_PATH);
	}
	return n;
}
```

(`isWritablePath` is defined above `themeRoots` so it is in scope; both are `static` file-local.)

- [ ] **Step 2: List across all roots.** Replace `dashThemeFolderList()`'s root acquisition: build the listing loop over each root from `themeRoots()`, skipping names already collected (case-insensitive). Skeleton:

```c
int dashThemeFolderList(char names[][DASH_THEME_NAME_MAX], int max)
{
	char roots[2][MAX_PATH];
	FILEINFO *files;
	int nr, ri, n, i, count = 0;

	nr = themeRoots(roots);
	if (nr <= 0)
		return 0;
	files = (FILEINFO *)malloc(sizeof(FILEINFO) * MAX_ENTRY);
	if (files == NULL)
		return 0;
	for (ri = 0; ri < nr; ri++) {
		filerRootMcPollIdle();
		n = getDir(roots[ri], files);
		for (i = 0; i < n && i < MAX_ENTRY - 2 && count < max; i++) {
			FILEINFO *f = &files[i];
			int j, dup = 0;

			if (f->name[0] == '\0' || f->name[0] == '.')
				continue;
			if (!(f->stats.AttrFile & sceMcFileAttrSubdir))
				continue;
			if (strlen(f->name) >= DASH_THEME_NAME_MAX)
				continue;
			for (j = 0; j < count; j++)
				if (!stricmp(names[j], f->name))
					dup = 1;
			if (!dup)
				dashStrCopy(names[count++], f->name, DASH_THEME_NAME_MAX);
		}
	}
	free(files);
	return count;
}
```

- [ ] **Step 3: Load from the first root that has the folder.** Replace `dashThemeFolderLoad()` with:

```c
int dashThemeFolderLoad(const char *folder)
{
	char roots[2][MAX_PATH], dir[MAX_PATH], path[MAX_PATH];
	char *text;
	int nr, ri, len;
	DashTheme t;
	FolderCtx ctx;

	nr = themeRoots(roots);
	for (ri = 0; ri < nr; ri++) {
		if (snprintf(dir, sizeof(dir), "%s%s/", roots[ri], folder) >= DASH_PATH_MAX - 32)
			continue;
		if (snprintf(path, sizeof(path), "%stheme.cfg", dir) >= (int)sizeof(path))
			continue;
		filerRootMcPollIdle();
		text = dashLoadTextFile(path, 16 * 1024, &len);
		if (text == NULL)
			continue; /* not in this root: try the next */
		/* start from the default so a sparse theme.cfg still makes sense */
		dashThemeLoadPreset(P_OSD, &t);
		t.name[0] = '\0';
		ctx.t = &t;
		ctx.folder_path = dir;
		dashKvParse(text, folderKv, &ctx);
		free(text);
		if (t.name[0] == '\0')
			dashStrCopy(t.name, folder, sizeof(t.name));
		resolveInFolder(t.image, sizeof(t.image), dir);
		resolveInFolder(t.app_icon, sizeof(t.app_icon), dir);
		/* an image background without an image falls back to the gradient */
		if (t.bg == DASH_BG_IMAGE && t.image[0] == '\0')
			t.bg = DASH_BG_GRADIENT;
		dashTheme = t;
		dashThemeCommit();
		return 0;
	}
	return -1;
}
```

- [ ] **Step 4: Export to the first writable root.** In `dashThemeFolderExport()`, first adjust the top-of-body declarations: change `char root[MAX_PATH], dir[MAX_PATH], fixed[MAX_PATH], path[MAX_PATH];` to `char roots[2][MAX_PATH], dir[MAX_PATH], fixed[MAX_PATH], path[MAX_PATH];` and add `int nr;` to the `int len, ret;` line. Then replace the `themesRoot()` acquisition + fallback block with:

```c
	char roots[2][MAX_PATH];
	int nr;

	nr = themeRoots(roots);
	if (nr <= 0)
		return -1;
	filerRootMcPollIdle();
	/* create THEMES/ and THEMES/<folder>/ under the first root (errors = already there) */
	dashStrCopy(dir, roots[0], sizeof(dir));
	dir[strlen(dir) - 1] = '\0';
	if (genFixPath(dir, fixed) >= 0)
		genMkdir(fixed, fileMode);
	if (snprintf(dir, sizeof(dir), "%s%s", roots[0], folder) >= DASH_PATH_MAX - 32)
		return -1;
```

keeping the rest of the existing body (`genMkdir` for the theme dir, `theme.cfg` write) unchanged.

- [ ] **Step 5: Build.** Expected: PASS, no new warnings.

- [ ] **Step 6: Commit.**

```bash
git add src/dash_themes.c
git commit -m "dash: theme folders resolve from the ELF folder first, dashboard folder second"
```

---

### Task 9: Documentation sweep

**Files:**
- Modify: `docs/themes.md`, `docs/dashboard.md`

**Interfaces:**
- Consumes: everything above (facts only).
- Produces: docs matching the shipped behaviour.

- [ ] **Step 1: `docs/themes.md`.**
- Backgrounds table: add `| Light Show | Light streaks | Light pillars sweeping across an indigo night |` after the Crystal row.
- `background` key list (line ~202): insert `streaks`, `clouds` before `image`.
- Preset ids list (line ~233): add `lightshow`.
- "Theme folders" section: rewrite the `<dashboard folder>` paragraph to: themes are looked up in `THEMES/` inside the ELF's own folder first, then inside the dashboard folder (where DASHBOARD.CNF lives); export writes to the first of those that is writable. Image paths stay relative to the theme folder they were loaded from.

- [ ] **Step 2: `docs/dashboard.md`.** Read it first; add short sections matching its style: interface sounds (Settings > Dashboard > Interface sounds / Sound volume; the `Dash_Sound`/`Dash_SoundVolume` DASHBOARD.CNF keys; graceful silence when the audio driver cannot start) and the two new backgrounds/preset. Update any settings key tables.

- [ ] **Step 3: Commit.**

```bash
git add docs/themes.md docs/dashboard.md
git commit -m "docs: sounds, new backgrounds, lightshow preset, CWD theme lookup"
```

---

## Final verification

- [ ] Full ps2build build of `ps2.yaml` passes clean.
- [ ] `git log --oneline` shows one commit per task on `nathan/feature/theme-engine-commits-09252026`.
- [ ] User checklist on hardware/PCSX2 (cannot be done in this environment): icons in hints/dialogs, both new backgrounds in all three motion modes, SFX on each hooked event, theme load from `THEMES/` colocated with the ELF, and a boot with `Dash_Animations = 0` confirming zero motion.
