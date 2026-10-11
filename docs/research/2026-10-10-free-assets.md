# Free assets for Runline: animation, character, buildings (research, 2026-10-10)

Constraint from the user: no money spent. Project rule: no Mirror's Edge assets (EA/DICE copyright;
retargeting them is still a derivative).

## Animation

| Source | What it is | Fit for Runline | License |
|---|---|---|---|
| **Epic Game Animation Sample (GASP)** | 500+ AAA motion-capture animations on the UE5 Manny/Quinn skeleton, plus ~400 more in the 5.7 update (adds slides). Locomotion (walk/run/sprint, jumps, falls) and a traversal set (vault, mantle, hurdle). | **Best base.** Same skeleton as our arms/body (no retargeting); traversal clips map to Vault/Mantle/PullUp; sprint, jump, land and slide map to Ground/Air/Slide. Missing: wallrun, wallclimb, ledge hang/shimmy, 180 turn, ME-style first-person arm poses. | Free; Unreal Engine EULA ("UE-only" content). |
| **Mixamo** (Adobe) | Free library: climbing, hanging, braced-hang shimmy, rolls, running slides, jumps, wall moves of varying quality. | **Gap filler** for hang/shimmy/roll/climb. Needs retargeting to Manny (UE5 IK Retargeter, or the free "Mixamo Animation Retargeting 2" plugin). Quality is uneven; check each clip. | Free, royalty-free in games and films; no redistribution of the raw files. |
| **Hand-keyed (your own)** | First-person arm poses keyed in Unreal (Control Rig / Sequencer) or Blender, against Mirror's Edge footage, timed to faith-runner's move durations. | **The signature moments:** hand on the wall in a wallrun, the reach and catch on a ledge grab, the hand plant on a vault. The part no library has, and the strongest portfolio piece. | Yours. |
| Fab biweekly freebies | Two-week free drops; claim and you keep them forever. Occasionally animation packs. | Opportunistic. | Fab Standard License. |

How it plugs in: the core decides movement and duration (for example vault 0.5 s, pull-up fixed);
animations are visual only, chosen from the core state and played at the rate that fits its timer
(no root motion). The M2b body+arms setup (arms copy the hidden body's pose) carries over unchanged.

## Character

- **Manny/Quinn** (already in the project): fine for now; in first person only the arms show.
- **MetaHuman** (free with UE; Creator built into UE 5.6+): a distinctive runner (outfit, sleeves,
  gloves read in first person). Heavy (LODs, grooms), but fine for one character.
- **Mixamo characters**: free, lighter, more generic.

## Buildings and environment

| Source | What it is | Fit | License |
|---|---|---|---|
| **City Sample Buildings** (Epic, from The Matrix Awakens) | 24 modular building kits, 44 sample buildings, 2,000+ meshes, 2K-8K textures. | Rooftop architecture and props (AC units, vents, pipes, ledges) at AAA quality. **Heavy:** migrate only what's used. Photoreal, which may fight a clean Mirror's Edge look. | Free; UE-only content. |
| **Kenney** (Modular Buildings, Building Kit, City Kits) | Low-poly modular buildings, walls, floors, doors, windows. | Light, clean, stylized; easy to bring to a white-and-red Mirror's Edge palette. | **CC0.** |
| **Quaternius** (Modular Sci-Fi MegaKit, 270+ pieces) | Stylized modular sci-fi kit. | If the look goes futuristic. | **CC0.** |
| **Poly Haven / ambientCG** | HDRI skies, PBR textures, some models. | Skies and surface materials for any style. | **CC0.** |
| **Fab biweekly freebies** | E.g. past drops: Downtown Alley (50+ urban modular pieces), Flying Fantasy Town. | Watch for urban packs; claim to keep. | Fab Standard License. |

How it plugs in: ARunlineBox stays the collider; an art pass swaps or decorates its cube with kit
meshes (non-colliding visuals), so the core's box collision, and its parity with faith-runner, is
unchanged.

## Cautions

- **UE-only content (GASP, City Sample) and Mixamo files can't be published as raw files.** Keep them
  out of any public repository (a git-ignored folder, or a private repo/LFS) and ship them only inside
  built games. CC0 content (Kenney, Quaternius, Poly Haven) has no such limit.
- **Repo size:** City Sample is tens of GB; migrate individual meshes, not the project.
- **Look:** the spec's art direction is "greybox-plus with a strong palette". Photoreal city kits can
  hurt readability (which surfaces are runnable) and performance; Mirror's Edge itself is clean and
  bright with red runner vision, which the greybox already approximates.

## Sources
- [Game Animation Sample (CG Channel / gamefromscratch)](https://gamefromscratch.com/?p=28949), [UE 5.7 update notes](https://lilys.ai/pt/notes/unreal-engine-5-20260221/game-animation-project-update-ue57), [Fab: GASP-based vault/mantle listing](https://www.fab.com/listings/259f8545-f820-47b3-8fc1-e8ec5458214d)
- [Mixamo licensing FAQ (Adobe community)](https://community.adobe.com/questions-696/mixamo-faq-licensing-royalties-ownership-eula-and-tos-589400), [Mixamo-to-UE5 retargeting](https://unamedia.com/ue5-mixamo/docs/retarget-mixamo-to-ue5/)
- [MetaHuman license](https://www.metahuman.com/license), [MetaHumans usable in other engines (CG Channel)](https://www.cgchannel.com/2025/06/you-can-now-sell-metahumans-or-use-them-in-unity-or-godot/)
- [City Sample assets (CG Channel)](https://www.cgchannel.com/2022/04/download-epic-games-free-city-sample-assets-for-ue5), [City Sample Buildings](https://docs.unrealengine.com/marketplace/en-US/product/city-sample-buildings)
- [Kenney Modular Buildings](https://kenney-assets.itch.io/modular-buildings), [Kenney Building Kit](https://kenney-assets.itch.io/building-kit), [Quaternius Modular Sci-Fi MegaKit](https://quaternius.itch.io/modular-sci-fi-megakit)
- [Poly Haven license FAQ](https://docs.polyhaven.com/en/faq)
- [Fab biweekly free drops (CG Channel)](https://www.cgchannel.com/2024/11/epic-games-to-share-new-free-assets-on-fab-every-two-weeks), [giveaway changes (gamefromscratch)](https://gamefromscratch.com/massive-changes-to-the-unreal-engine-fab-marketplace-giveaways/), [Downtown Alley freebie (Digital Production)](https://digitalproduction.com/2025/05/22/free-fab-epics-giving-away-a-back-alley/)
- [Unreal Engine EULA change log (content distribution)](https://www.unrealengine.com/en-US/eula-change-log/unreal)
