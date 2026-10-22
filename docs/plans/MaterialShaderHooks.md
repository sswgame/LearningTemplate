# 머티리얼 셰이더 훅 구조

> 상태: 설계 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 표

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
| D2 | 머티리얼 XML 과 셰이더 구조체의 불일치를 오류로 만든다 — 셰이더에만 있는 필드 · XML 에만 있는 필드 · 리플렉션 실패 폴백 셋 모두. `Material.cpp` 의 `matchPropertiesToSchema` · `packPropertiesIntoBytes` · `ensureShaderLayout` 이 대상이다. 훅과 독립이라 **가장 먼저** | 이 문서 2절, `Source/Engine/Resource/Material/` README | 이 문서 D2 행, 머티리얼 모듈 README | Debug nogpu 테스트(불일치 XML 이 오류), 네 백엔드 기존 머티리얼 전부 로드 | 남음 |
| H1 | 뼈대(`materialshader.hlsli` · `materialshadermain.hlsli`) + forwardlit. 림은 현재 식 그대로 둔다. **먼저 확인**: 사용자 보간 필드 구조체가 FXC(DX12 경로) · GL(SPIR-V 경유) 에서 되는지 | `Source/Engine/RHI` 셰이더 README, 이 문서 1절 | 이 문서 H1 행 | 셰이더 쿠킹 네 백엔드, forwardlit 화면이 바뀌기 전과 같다 | 남음 |
| H2 | toon — VAT 통일, 시선 계산 통일 | 이 문서 2절 | 이 문서 | toon 화면(VAT 가 있는 메시), 그림자 | 남음 |
| H3 | terrain · foliage | 이 문서 | 이 문서 | 화면 | 남음 |
| H4 | water(+ `MATERIAL_VERTEX_DEFORM`) | 이 문서 3절 | 이 문서 | 화면, 쿠킹 바이너리 수 변화가 `water.material` 한 개뿐 | 남음 |
| H5 | sprite2d · sprite2dlit | 이 문서 | 이 문서 | 2D 화면 | 남음 |
| H6 | gbuffer · shadowdepth 를 머티리얼 없는 뼈대 사용자로 | 이 문서 | 이 문서 | 깊이 비트 일치(훅이 위치를 만들 수 없음), 그림자 화면 | 남음 |
| H7 | 포워드 · 디퍼드 림을 한 함수로 — **이미지가 바뀐다**(2절의 pow2 × 0.15 대 pow3 × 0.12) | 이 문서 2절 | 이 문서, QA 골든 | 골든을 다시 뜬다, 네 백엔드 | 남음 |
| D1 | 도메인 발견 — `ShaderCooker::kArrIncludeRootDomain` 하드코딩을 `Config/Engine/EngineConfig.json` 의 `_listResourcePriority` 기반 발견으로 바꾼다. 엔진은 도메인 종류만 알고 game 아래 폴더를 훑는다. 게임 셰이더는 자기 도메인 + engine + common 만 include 하고, 게임 폴더의 같은 이름 헤더가 엔진 헤더를 가리지 못하게 한다. 테스트 도메인의 최소 셰이더로 쿠킹 스탬프 · include · 머티리얼 경로 · 리플렉션을 증명한다 | `Config/Engine/EngineConfig.json`, 쿠커 README | 이 문서 | 테스트 도메인 셰이더가 쿠킹 → 머티리얼 → 리플렉션까지 통과, 같은 이름 헤더 가림 시도가 오류 | 완료(2026-10-23, wt/domains) |
| G1 | 게임 셰이더 제작(첫 요구가 생길 때 착수) — 계약은 1절. 후보 문서에는 중복해 두지 않는다 | 이 문서 | 이 문서, 해당 게임 문서 | 게임 화면 | 남음 |

## 5. 확인하지 못한 것

- FXC · GL 이 보간 필드의 중첩 구조체를 받는지(H1 이 먼저 확인한다).
- `swLoadAnimatedPosition` 과 `swLoadAnimatedVertex` 가 비트 단위로 같은 위치를 내는지(H6 의 깊이 일치 전제).
- 툰이 프로브(간접 조명)를 받지 않는 것이 의도인지.
