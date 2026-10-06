<!-- 생성 문서 — 손으로 고치지 않는다. 정본은 코드다. 다시 만들기: py -3 Scripts/generate/GenerateConfigReference.py -->

# 사용자 설정 (플레이어 옵션)

[설정 색인](README.md) · 스키마 형식: [`Source/Engine/UserSettings/README.md`](../../Source/Engine/UserSettings/README.md)

플레이어 값은 `usersettings.json`(사용자 폴더)에 **기본값과 다른 것만** 쓰인다. `target` 이 `gv:` 면 그 전역 변수에 값을 넣는다 — 기동 때 명령줄 `-gv_*` 로 준 변수는 덮지 않는다(명령줄이 이긴다, `docs/07_Configuration.md` 우선순위).

## `Resource/engine/settings/engine.settings.xml`

| id | 타입 | 기본값 | 범위 | 적용 | 대상 |
|---|---|---|---|---|---|
| `display.windowMode` | enum | `windowed` |  | confirm | `applier:display.windowMode` |
| `display.resolution` | enum | `1280x720` |  | confirm | `applier:display.resolution` |
| `display.vsync` | bool | `false` |  | confirm | `applier:display.vsync` |
| `graphics.quality` | enum | `high` |  | confirm | `게임 코드` |
| `graphics.upscaler` | enum | `off` |  | confirm | `gv:gv_upscaler` |
| `graphics.renderScale` | float | `1` | 0.5 ~ 1 (눈금 0.05) | confirm | `gv:gv_renderScale` |
| `graphics.shadowQuality` | enum | `high` |  | confirm | `gv:gv_shadowQuality` |
| `graphics.viewDistance` | float | `1` | 0.25 ~ 2 (눈금 0.25) | confirm | `gv:gv_viewDistanceScale` |
| `graphics.foliageDensity` | float | `1` | 0 ~ 1.5 (눈금 0.25) | confirm | `gv:gv_foliageDensity` |
| `graphics.postQuality` | enum | `high` |  | confirm | `gv:gv_postQuality` |
| `graphics.textureQuality` | enum | `high` |  | restart | `gv:gv_textureQuality` |
| `graphics.effectsQuality` | enum | `high` |  | confirm | `gv:gv_effectsQuality` |
| `graphics.motionBlur` | bool | `true` |  | confirm | `gv:gv_motionBlur` |
| `audio.masterVolume` | float | `1` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.musicVolume` | float | `0.8` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.sfxVolume` | float | `1` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.voiceVolume` | float | `1` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.ambientVolume` | float | `1` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.uiVolume` | float | `1` | 0 ~ 1 (눈금 0.05) | immediate | `applier:audio.busVolume` |
| `audio.mute` | bool | `false` |  | immediate | `applier:audio.mute` |
| `controls.mouseSensitivity` | float | `1` | 0.1 ~ 5 (눈금 0.05) | confirm | `applier:input.mouseSensitivity` |
| `controls.gamepadSensitivity` | float | `1` | 0.1 ~ 5 (눈금 0.05) | confirm | `applier:input.gamepadSensitivity` |
| `controls.invertY` | bool | `false` |  | confirm | `applier:input.invertY` |
| `controls.stickDeadzone` | float | `0.15` | 0 ~ 0.5 (눈금 0.01) | confirm | `applier:input.stickDeadzone` |
| `controls.aimMode` | enum | `hold` |  | confirm | `applier:input.toggleMode` |
| `controls.crouchMode` | enum | `hold` |  | confirm | `applier:input.toggleMode` |
| `controls.sprintMode` | enum | `hold` |  | confirm | `applier:input.toggleMode` |
| `gameplay.fieldOfView` | float | `70` | 60 ~ 110 (눈금 1) | confirm | `gv:gv_cameraFieldOfView` |
| `gameplay.cameraShake` | float | `1` | 0 ~ 1 (눈금 0.1) | confirm | `gv:gv_cameraShakeScale` |
| `gameplay.headBob` | bool | `true` |  | confirm | `gv:gv_cameraHeadBob` |
| `accessibility.colorVision` | enum | `off` |  | confirm | `gv:gv_colorVisionMode` |
| `accessibility.uiScale` | float | `1` | 0.75 ~ 1.5 (눈금 0.05) | confirm | `gv:gv_uiScale` |
| `accessibility.textSize` | float | `1` | 0.75 ~ 2 (눈금 0.05) | confirm | `gv:gv_uiTextScale` |
| `accessibility.reduceFlashing` | bool | `false` |  | confirm | `gv:gv_reduceFlashing` |
| `accessibility.reduceMotion` | bool | `false` |  | confirm | `gv:gv_uiReduceMotion` |
| `accessibility.subtitles` | bool | `true` |  | confirm | `gv:gv_subtitles` |
| `accessibility.subtitleSize` | enum | `medium` |  | confirm | `gv:gv_subtitleSize` |
| `accessibility.subtitleBackground` | float | `0.5` | 0 ~ 1 (눈금 0.1) | confirm | `gv:gv_subtitleBackgroundOpacity` |
| `language.text` | enum | `` |  | immediate | `applier:localization.language` |
| `telemetry.enabled` | bool | `false` |  | confirm | `게임 코드` |
| `telemetry.crashReports` | enum | `local` |  | confirm | `게임 코드` |

## `Resource/game/shooter3d/data/shooter3d.settings.xml`

| id | 타입 | 기본값 | 범위 | 적용 | 대상 |
|---|---|---|---|---|---|
| `gameplay.difficulty` | enum | `normal` |  | confirm | `게임 코드` |
