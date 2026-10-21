# 간접 조명 구현 단위 (`D2-12`)

> 상태: 구현중 · 선행: ClusteredLighting · 영역: graphics · 옛번호: D2-12 · 진행: 7/8

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-12` 행이 원본입니다. 이 문서는 그 결정을 구현 단위로 나눈 계획과 단위마다의 계약을 적습니다.
단위가 끝나면 그 단위의 절을 지우고, 남는 교훈은 코드의 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 라이트 프로브는 균일 격자의 2차 구면 조화 함수(SH9, RGB)입니다. 유니티 APV 의 방향과 같습니다.
- 반사 프로브는 옥타헤드럴 2D 아틀라스로 시작합니다. 큐브 텍스처 바인딩은 네 백엔드가 모두 지원하지 않아 필요해지면 바꿉니다.
- 베이크 결과 `.probes` 는 커밋합니다. CI 에 GPU 가 없으므로 내비메시처럼 결과를 저장소에 둡니다.
- 금속도와 거칠기 PBR 을 도입합니다. 툰은 별도 경로를 유지합니다. 골든 이미지를 다시 찍습니다.
- DX11 과 GL 도 같은 기능 수준입니다. 백엔드 제약(엔진 텍스처 슬롯 네 개, bindless 없음)은 슬롯 재배치로 풉니다.

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| P0 | SH9 수학(`Core/Math/SphericalHarmonics.h` `SH9Color` — 투영, 평가, 회전 없이 합산, GPU 원소 float4 일곱 개)과 `SphericalHarmonicsTest` — **들어감**. 테스트 실행은 **확인 필요**(빌드 미확인) | 없음 |
| P1 | `LightProbeVolumeComponent`(축 정렬 상자 `_extent`, 목표 간격 `_spacing`, 베이크 경로 `_probeDataPath`)와 `.probes` 형식(`LightProbeGrid` — 매직 `SWLP`, 버전, 격자 모양, 프로브마다 SH9 RGB 27 실수) — **들어감**. 모양이 다른 베이크는 쓰지 않는다. `LightProbeGridTest` 실행은 **확인 필요** | P0 |
| P2 | CPU 베이크(`LightProbeBaker`): 프로브마다 균일 구 레이를 쏘아 닿은 면이 튕긴 직접광(반사율은 볼륨의 `_bounceAlbedo`)과 빈 방향의 하늘을 SH 로 투영한다. 직접광 자체는 담지 않는다(조명 식이 이미 계산해 두 번 비춘다). 가림은 물리 레이캐스트 — **들어감**. 하늘 항은 이제 있는 스카이 컴포넌트(`SkyComponent` 의 `SkyGradient::evaluateRadiance`, D3-7.4)를 `LightProbeBaker::collectInput` 에 잇는 일이 **남음**(지금은 0). 물리 씬이 있는 굽기 테스트는 **남음** | P1, `D3-7.4`(하늘) |
| P3 | 셰이더 소비: 볼륨은 라이트 목록의 `SW_LIGHT_TYPE_PROBE_VOLUME` 원소(클러스터 그리드 항목), SH9 는 전용 슬롯 t13 `g_SwLightProbes`(`swLoadProbe` 하나만 위치를 안다). `swShadeLights` 가 덮는 볼륨들의 SH(삼선형, 가중 평균)로 앰비언트를 바꾼다. 상자 밖은 자르지 않는다 — **들어감**. 스카이 전역 항목(볼륨 밖)은 D3-7.4 뒤로 남음, 그때까지 볼륨 밖은 지금의 앰비언트. 툰(`toon.hlsl`)은 자기 앰비언트를 그대로 쓴다. `RenderPassGPUTest.LightProbeVolumeReplacesAmbient` · `SceneLightTest.CollectsBakedProbeVolumesAfterLights` 실행은 **확인 필요** | P1, `D2-13` S1 |
| P4 | 반사 프로브: GPU 베이크(씬을 여섯 면으로 그려 옥타헤드럴로 접고 거칠기 밉을 만듭니다)와 아틀라스 슬롯 | `D2-6.3` 슬롯 지도, P5 |
| P5 | 금속도와 거칠기 PBR — **들어감**(빌드 · 셰이더 쿠킹 · 화면 확인 전). 조명 식 `swShadeLights( …, roughness, metallic )`(lighting.hlsli — 언리얼 기본 셰이딩 모델과 같은 GGX · 스미스 결합 가시성 근사 · 슐릭, 확산은 `기본 색 × (1 − 금속도)`, 환경 반사는 `EnvBRDFApprox` × 앰비언트 — 반사 프로브가 들어오면 그 자리를 바꾼다), 머티리얼 필드 `metallic`(forwardlit 의 `SwMaterialData`, forwardlit 머티리얼 43 개에 `0.0` 을 적었다 — 빠진 필드를 0 으로 채우는 길에 기대지 않는다), G버퍼 셋째 타깃 `GBufferMaterial`(아래 D2-6.4). 테스트 `RenderPassGPUTest.MetallicChangesForwardAndDeferredPictures`. 골든 이미지는 다시 찍어야 한다(유전체에도 하이라이트가 생겼다) | `D2-6.4` G버퍼 확장 |
| D2-6.4 | G버퍼 셋째 타깃 — **들어감**(빌드 전). 첨부 `GBufferMaterial`(R8G8B8A8_UNORM, r 금속도 · g 거칠기, b · a 안 씀 — 쓰는 쪽 `swEncodeGBufferMaterial`, 읽는 쪽 `swLoadGBufferMaterial` 둘 다 binding.hlsli), 역할 `RenderPassInputRole::GBufferMaterial`(PassCB `g_GBufferMaterialIndex`), GBuffer 패스 MRT 셋(`RenderPassTypeInfo` 포맷 표가 수를 정한다), Lighting 계약의 필수 입력. 에뮬 백엔드(DX11 · GL)는 조명 패스가 엔진 텍스처 다섯을 읽어 t0..t3 이 모자라 **t15** 에 따로 둔다(`SW_SLOT_ENGINE_GBUFFER_MATERIAL` — 띠 꼭대기, 띠가 닿으면 `ShaderBindingSlots.h` 가 막는다. DX11 은 s15 가 정적 세트라 짝 샘플러 없이 정적 엔진 텍스처 샘플러로 읽는다). 셰이딩 모델은 알베도 알파에 그대로 둔다 | 없음 |
| P6 | 에디터 베이크: 커맨드 `lighting.bakeProbes`(Build > Bake Light Probes — 선택한 볼륨, 없으면 모든 볼륨), 결과는 씬 옆 `<씬>.<오브젝트>.probes` 와 볼륨의 `_probeDataPath`, 탐침 `Editor.LightProbeBakeCount` · `Editor.BakedLightProbeVolumeCount`, 시나리오 `engine/automation/editor/lightprobebake.scenario.xml` — **들어감**. 굽기는 동기라 진행 표시는 끝난 뒤 알림 하나다(긴 굽기를 비동기 작업으로 옮기는 일은 **남음**). 볼륨을 실제로 굽는 시나리오(파일을 쓰는)와 시나리오 실행은 **확인 필요** | P2 |

## 3. 충돌 주의

P5 · D2-6.4 의 셰이더 소스(binding.hlsli · bindingslots.hlsli · lighting.hlsli · forwardlit · gbuffer · deferredlighting · terrain · foliage · water)는 커밋 훅(`CheckShaderStamps`)이 쿠킹 바이너리와 함께 커밋하라고 막아 **패치로 따로 넘겼다**(작업자 보고의 경로). C++ 는 `SW_SLOT_ENGINE_GBUFFER_MATERIAL` 을 쓰므로 그 패치를 붙이고 `App --cook-shaders` 한 뒤에 빌드된다.
binding.hlsli 의 PassCB 와 bindingslots.hlsli 는 다른 작업(조명 · 셰이더)과 자주 겹친다 — 슬롯은 t15(에뮬 전용)만 새로 썼다.
