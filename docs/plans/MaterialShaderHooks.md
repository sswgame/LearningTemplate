# 머티리얼 셰이더 훅 구조

> 상태: 끝 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 표

엔진이 헤더 뼈대를 가지고 셰이더 파일은 훅만 정의하는 구조로 머티리얼 셰이더를 바꾼다. 지금은 엔진 셰이더 7개(foliage · forwardlit · sprite2d · sprite2dlit · terrain · toon · water)가 같은 일(정점 변환 · 스킨 · VAT · 조명 · 림 · 그림자 패스 분기)을 각자 쓴다. 그 결과 같은 계산이 어긋난 결함이 이미 있다(2절).
이 문서는 2026-10-23 사용자와 정한 결정(1절)과 사실(2절)을 구분해 적는다. 추정과 확인하지 못한 것은 5절에 모았다.

## 1. 결정 (2026-10-23)

- **뼈대는 엔진이 가진다.** 헤더 두 단 — `materialshader.hlsli`(훅 선언 · 기본 선택) → `materialshadermain.hlsli`(정점 · 픽셀 진입점 뼈대). 셰이더 파일은 훅만 정의하고 뼈대를 include 한다(두 단 include).
- **훅은 다섯이다**: 표면 · 조명 모델 · 월드 위치 오프셋 · 사용자 보간 필드 · 외곽선. "조명 뒤 색" 훅과 "불투명 마스크" 훅은 쓰는 곳이 없어 뺀다.
- **"있으면 쓰고 없으면 기본" 폴백은 받지 않는다.** 훅마다 둘 중 하나를 명시한다 — 훅을 정의하거나, `SW_MATERIAL_DEFAULT_*` 로 기본을 쓴다고 명시해 고른다. 둘 다 없으면 컴파일 오류, 둘 다 있으면 재정의 오류다(뼈대의 `#error`).
- **엔진 셰이더 7개가 공개 계약의 첫 사용자다.** 게임 셰이더(G1)는 같은 계약을 쓴다. 엔진 셰이더가 쓰지 못하는 계약은 만들지 않는다.
- **훅 집합을 파이프라인 XML 로 옮기는 안은 채택하지 않는다.** 이유 셋: ① HLSL 뼈대의 `#if` 와 원본이 둘이 된다. ② 파이프라인 5개에 같은 내용을 반복해야 한다. ③ 쿠커의 2단계 PSO 가 define 을 잃는다. 패스 종류 → define 대응은 패스 종류 테이블 한 곳에 두고, 중복은 뼈대의 `#error` 로 막는다.
- **비트까지 같은 깊이**(그림자 · 프리패스 · 본 패스의 깊이가 같은 비트)는 위치를 만드는 코드가 뼈대 한 곳뿐이라는 형식으로 지킨다(훅이 위치를 직접 만들 수 없다).

## 2. 사실 — 지금 있는 결함 (코드로 확인한 줄 번호)

| 결함 | 근거 |
|---|---|
| 툰은 VAT(정점 애니메이션 텍스처)를 읽지 않는데 shadowdepth 는 읽는다 | `toon:262`, `toon:294` 대 `shadowdepth:14` |
| 포워드 림은 pow2 × 0.15, 디퍼드 림은 pow3 × 0.12 로 다르다 | `forwardlit:102` 대 `deferredlighting:60` |
| forwardlit 에 그림자 패스 분기가 없다 | forwardlit 전체 |
| 시선 방향 계산이 세 벌이다 | `lighting.hlsli:465` · `toon:151` · `water:72` |
| 머티리얼 XML 과 셰이더 구조체가 어긋나도 조용히 넘어간다 — 셰이더에만 있는 필드는 0, XML 에만 있는 필드는 버려진다. 리플렉션이 실패하면 폴백한다 | `Material.cpp` 의 `matchPropertiesToSchema` · `packPropertiesIntoBytes` · `ensureShaderLayout`. `…Map` 필드가 0 이면 Vulkan 에서 첫 실제 텍스처를 읽는다 |

## 3. 위험

- **이른 반환이 훅 안에서 뼈대로 인라인되면 재발한다.** Vfx V5 의 SPIR-V 최적화 사례([Vfx](Vfx.md) V4 남은 확인 "V5 원인")가 이른 반환 있는 `evaluateCurve` 에서 났다. 뼈대는 출구가 하나여야 한다. 훅 안의 중간 `return` 은 형식으로 막지 못하므로 최후 수단으로 `CheckShaderConventions` 에 검사를 둔다(막지 못하는 틈이라 게이트를 둔다).
- **쿠킹 바이너리 수는 백엔드마다 2811 이다.** 외부 define 을 만들지 않으면 그대로다. 예외는 `water.material` 의 `MATERIAL_VERTEX_DEFORM` 하나다(H4).
- H7 은 이미지가 바뀐다(포워드 · 디퍼드 림을 한 함수로 합침). 골든을 다시 뜬다.

## 4. 단위

순서는 D2(가장 먼저, 훅과 독립) → H1 → H2 ~ H6 → H7 → D1 → G1 이다. H2 ~ H6 은 H1 뒤에 서로 독립이다.

| 단계 | 내용 | 읽을 문서 | 고칠 문서 | 검증 | 상태 |
|---|---|---|---|---|---|
| D2 | 머티리얼 XML 과 셰이더 구조체의 불일치를 오류로 만든다 — 셰이더에만 있는 필드 · XML 에만 있는 필드 · 리플렉션 실패 폴백 셋 모두. `Material.cpp` 의 `matchPropertiesToSchema` · `packPropertiesIntoBytes` · `ensureShaderLayout` 이 대상이다. 훅과 독립이라 **가장 먼저** | 이 문서 2절, `Source/Engine/Resource/Material/` README | 이 문서 D2 행, 머티리얼 모듈 README | Debug nogpu 테스트(불일치 XML 이 오류), 네 백엔드 기존 머티리얼 전부 로드 | 끝남(2026-10-23, main `71889fb71` — 저장소의 모든 `.material` 이 불일치 없음, 오류로 막는 단위 테스트 추가) |
| H1 | 뼈대(`materialshader.hlsli` · `materialshadermain.hlsli`) + forwardlit. 림은 현재 식 그대로 둔다. **먼저 확인**: 사용자 보간 필드 구조체가 FXC(DX12 경로) · GL(SPIR-V 경유) 에서 되는지 | `Source/Engine/RHI` 셰이더 README, 이 문서 1절 | 이 문서 H1 행 | 셰이더 쿠킹 네 백엔드, forwardlit 화면이 바뀌기 전과 같다 | 끝남(2026-10-23, main `b9969fc07` — 쿠킹 · 테스트 통과, 화면 픽셀 동일 비교는 미확인) |
| H2 | toon — VAT 통일, 시선 계산 통일 | 이 문서 2절 | 이 문서 | toon 화면(VAT 가 있는 메시), 그림자 | 끝남(2026-10-23, main `b9969fc07` — 쿠킹 · 테스트 통과, 화면 픽셀 동일 비교는 미확인) |
| H3 | terrain · foliage | 이 문서 | 이 문서 | 화면 | 끝남(2026-10-23, main `4f54ba646` — terrain 은 표면 훅만, foliage 는 줄이기 · 바람 · 휘기를 월드 위치 오프셋 훅 하나로, 머티리얼은 정점에서 읽고 색 · 텍스처 인덱스는 사용자 보간 칸. DX12 화면 바이트 동일) |
| H4 | water(+ `MATERIAL_VERTEX_DEFORM`) | 이 문서 3절 | 이 문서 | 화면, 쿠킹 바이너리 수 변화가 `water.material` 한 개뿐 | 끝남(2026-10-23, main `4f54ba646` — 파도를 오프셋 훅으로, 화면 자리를 읽어 시선 방향을 만드는 사용자 보간 칸 훅은 뼈대가 준 최종 클립 위치(`VertexContext.clipPosition`)를 읽는다. DX12 화면 바이트 동일. **바이너리 수 변화는 물 한 개만이 아니다** — 백엔드마다 물 +1(그림자 퍼뮤테이션)이고 H5 의 스프라이트 머티리얼도 같은 이유로 sprite2d · sprite2dlit 각 +1, shadowdepth −2 라서 합계 +1(2816 → 2817 파일) ) |
| H5 | sprite2d · sprite2dlit | 이 문서 | 이 문서 | 2D 화면 | 끝남(2026-10-23, main `4f54ba646` — 픽셀 스냅은 오프셋 훅, 아틀라스 프레임 UV · 인스턴스 색 · 스프라이트 축은 사용자 보간 칸. 세 스프라이트 머티리얼에 `MATERIAL_VERTEX_DEFORM` · `MATERIAL_BLEND_TRANSLUCENT` 를 더했다. 2D 화면 3장 바이트 동일. 쓰는 곳이 없던 `ALPHA_TEST` 분기는 없앴다. 보기 모드(Normals · Depth 등)를 이제 스프라이트도 따른다) |
| H6 | gbuffer · shadowdepth 를 머티리얼 없는 뼈대 사용자로 | 이 문서 | 이 문서 | 깊이 비트 일치(훅이 위치를 만들 수 없음), 그림자 화면 | 끝남(2026-10-23, main `4f54ba646` — `SW_MATERIAL_NONE` 스위치. 위치 · 정점 로드는 뼈대 한 곳이라 `swLoadAnimatedPosition` · `swLoadMorphPosition` 을 없앴고, 깊이 프리패스의 `MATERIAL_VERTEX_DEFORM` · 그림자의 `MATERIAL_SHADOW_CAST_OFF` 배치 빼기는 뼈대가 한다. 프리패스 파이프라인 화면 = 포워드 화면 바이트 동일(깊이가 어긋나면 구멍이 난다), 그림자 장면 바이트 동일. 머티리얼 없는 G버퍼 배치가 이제 보기 모드 Normals · LightingOnly 등을 따른다) |
| H7 | 포워드 · 디퍼드 림을 한 함수로 — **이미지가 바뀐다**(2절의 pow2 × 0.15 대 pow3 × 0.12) | 이 문서 2절 | 이 문서, QA 골든 | 골든을 다시 뜬다, 네 백엔드 | 끝남(2026-10-23, main `3da802885` — `lighting.hlsli` 의 `Lighting::computeRimLight` 한 곳에 상수. 정본은 디퍼드(pow3 × 0.12): 림이 약해 가장자리가 덜 번진다. 벤치 골든 8장을 다시 떴고 Empty · Shooter3D 게임 골든은 네 백엔드 그대로 통과) |
| D1 | 도메인 발견 — `ShaderCooker::kArrIncludeRootDomain` 하드코딩을 `Config/Engine/EngineConfig.json` 의 `_listResourcePriority` 기반 발견으로 바꾼다. 엔진은 도메인 종류만 알고 game 아래 폴더를 훑는다. 게임 셰이더는 자기 도메인 + engine + common 만 include 하고, 게임 폴더의 같은 이름 헤더가 엔진 헤더를 가리지 못하게 한다. 테스트 도메인의 최소 셰이더로 쿠킹 스탬프 · include · 머티리얼 경로 · 리플렉션을 증명한다 | `Config/Engine/EngineConfig.json`, 쿠커 README | 이 문서 | 테스트 도메인 셰이더가 쿠킹 → 머티리얼 → 리플렉션까지 통과, 같은 이름 헤더 가림 시도가 오류 | 끝남(2026-10-23, main `25f8c863d`) |
| G1 | 게임 셰이더 제작(첫 요구가 생길 때 착수) — 계약은 1절. 후보 문서에는 중복해 두지 않는다 | 이 문서 | 이 문서, 해당 게임 문서 | 게임 화면 | 끝남(2026-10-23, main `871e58646` — Shooter3D CCTV 모니터를 `game/shooter3d/shaders/cctvmonitor.hlsl` 로: 표면 훅만 정의하고 `SW_MATERIAL_UNLIT` · `SW_MATERIAL_DEFAULT_VARYINGS` 를 명시, 주사선 줄 수 · 세기는 머티리얼 XML. 엔진 쪽 변경 없이 계약대로 쿠킹 · 로드됐다. `MaterialTest.GameDomainShaderMaterialMatchesItsStruct`, 시나리오 `shooter3d.cctvmonitor` 로 DX12 · DX11 · GL 화면 확인. Vulkan 은 이 변경 전 머티리얼로도 같은 `VK_ERROR_OUT_OF_DEVICE_MEMORY` 로 죽어 미확인) |

## 5. 확인하지 못한 것

- FXC · GL 이 보간 필드의 중첩 구조체를 받는지(H1 이 먼저 확인한다).
- ~~`swLoadAnimatedPosition` 과 `InstanceData::loadAnimatedVertex` 가 비트 단위로 같은 위치를 내는지~~ — H6 이 짧은 형태를 없애 질문이 사라졌다(모든 패스가 `InstanceData::loadAnimatedVertex` 하나로 읽는다).
- 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지.

## 6. 구현에서 더한 것 (H3 ~ H6, 명세 밖 판단)

- **`SW_MATERIAL_NONE`**(머티리얼 없는 사용자): 머티리얼 구조체 · 읽기 · 오프셋 훅이 없다. 알파 컷오프는 `#error`. 깊이 패스에서 이 셰이더가 같은 자리를 못 만드는 배치를 뼈대가 한 점으로 모아 뺀다.
- **깊이만 쓰는 변형(`SW_MATERIAL_DEPTH_ONLY` — 깊이 패스이고 알파 컷오프가 아님)의 정점 → 픽셀 칸은 위치와 등장 디더뿐이다.** 그림자 맵은 모든 메시를 패스마다 그리므로 쓰이지 않는 칸을 내보내지 않는다. H1 · H2 의 forwardlit · toon 깊이 변형도 같이 바뀌었다.
- **사용자 보간 칸 훅의 문맥에 `clipPosition`(오프셋 · 외곽선이 반영된 최종 클립 위치)을 읽기 전용으로 둔다.** 물의 시선 방향이 필요하다. 그 훅에서만 채워지고 다른 훅에서는 0 이다.
- **스프라이트의 픽셀 스냅은 월드 위치 오프셋이다.** 오프셋 훅은 `MATERIAL_VERTEX_DEFORM` 퍼뮤테이션에서만 불리므로 세 스프라이트 머티리얼이 그 define 을 가진다. 대가로 스프라이트도 그림자 퍼뮤테이션을 하나씩 쿠킹하고 깊이 프리패스에서 빠진다(스프라이트는 투명이라 영향이 없다).
- **게임 셰이더 한 개의 쿠킹 산출물은 백엔드마다 180 파일(네 백엔드 720, 스탬프 · 매니페스트 포함)이다**(G1, cctvmonitor). 머티리얼 셰이더는 패스 종류 × 머티리얼 퍼뮤테이션(포그 셋)을 모두 굽는다. 조명 없는 셰이더도 G버퍼 변형이 구워진다 — 디퍼드 파이프라인에서 어떻게 보이는지는 확인하지 않았다(Shooter3D 는 포워드).
- **물은 그림자를 드리우지 않는다(2026-10-23 사용자 결정 — 상용 엔진 기준).** 반투명 물은 Unreal Water · Unity 물 모두 기본이 그림자 끔이고, 알파 블렌드 면이 그림자 맵에 불투명하게 찍히면 물 밑이 어두워진다. `water.material` 에 `MATERIAL_SHADOW_CAST_OFF` 를 더했다(풀과 같은 방식). 쿠킹 바이너리와 물이 든 골든은 다시 쿠킹 · 다시 뜨기가 남았다.
- 동작이 바뀌는 곳(모두 Lit 화면은 같다): 머티리얼 없는 G버퍼 배치와 스프라이트가 보기 모드를 따른다. 스프라이트의 Depth · Light Complexity 보기는 스프라이트 평면의 z 를 쓴다(예전 2D 조명 셰이더는 z = 0 으로 가정했다).
