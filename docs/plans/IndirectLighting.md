# 간접 조명 구현 단위 (`D2-12`)

> 상태: 측정대기 · 선행: ClusteredLighting · 영역: graphics · 옛번호: D2-12 · 진행: 8/8

**남은 확인(통합에서 `gpu-verifier` 로 한 번에):** 매니페스트 id `indirect-lighting-visual-checks`(P4 · P5 · D2-6.4 의 셰이더 패치 적용 · 쿠킹 · 화면 확인 — 2절 각 단위 칸의 "확인 전" · "확인 필요" 표시). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

결정은 [결정 기록](../09_Decisions.md) 5-12 의 `D2-12` 행이 원본입니다. 단위가 끝나면 절을 지우고 남는 교훈은 코드 헤더 주석으로 옮깁니다.

## 1. 정한 것

- 라이트 프로브는 균일 격자의 2차 구면 조화 함수(SH9, RGB)입니다(유니티 APV 의 방향).
- 반사 프로브는 옥타헤드럴 2D 아틀라스로 시작합니다(큐브 텍스처 바인딩은 네 백엔드가 모두 지원하지 않아 필요해지면 바꾼다).
- 베이크 결과 `.probes` 는 커밋합니다(CI 에 GPU 가 없으므로 내비메시처럼 결과를 저장소에 둔다).
- 금속도와 거칠기 PBR 을 도입하고 툰은 별도 경로를 유지합니다. 골든 이미지를 다시 찍습니다.
- DX11 과 GL 도 같은 기능 수준입니다(엔진 텍스처 슬롯 네 개, bindless 없음은 슬롯 재배치로 푼다).

## 2. 단위 (모두 들어감 — 실행 · 화면은 "확인 필요")

| 단위 | 내용 | 선행 |
|---|---|---|
| P0 | SH9 수학(`Core/Math/SphericalHarmonics.h` `SH9Color` — 투영, 평가, 합산, GPU 원소 float4 일곱 개)과 `SphericalHarmonicsTest`. 테스트 실행은 **확인 필요** | 없음 |
| P1 | `LightProbeVolumeComponent`(축 정렬 상자 `_extent`, 목표 간격 `_spacing`, 베이크 경로 `_probeDataPath`)와 `.probes` 형식(`LightProbeGrid` — 매직 `SWLP`, 버전, 격자 모양, 프로브마다 SH9 RGB 27 실수). 모양이 다른 베이크는 쓰지 않는다. `LightProbeGridTest` 실행 **확인 필요** | P0 |
| P2 | CPU 베이크(`LightProbeBaker`): 프로브마다 균일 구 레이를 쏘아 닿은 면이 튕긴 직접광(반사율 `_bounceAlbedo`)과 빈 방향의 하늘(`SkyGradient::evaluateRadiance`, 스카이가 없으면 0)을 SH 로 투영한다. 직접광 자체는 담지 않는다(조명 식이 이미 계산해 두 번 비춘다). 가림은 물리 레이캐스트. `LightProbeBakerTest.BakesSkyAboveAndFloorBounceBelow` · `SteppedBakeMatchesOneShotBake`(실행 미확인), 편집 중 씬의 정적 콜라이더로 세운 가림 씬으로 굽는 `StaticCollidersOccludeTheEnclosedProbeBeforePlay`(Debug 통과) | P1, `D3-7.4`(하늘) |
| P3 | 셰이더 소비: 볼륨은 라이트 목록의 `SW_LIGHT_TYPE_PROBE_VOLUME` 원소(클러스터 그리드 항목), SH9 는 전용 슬롯 t13 `g_SwLightProbes`(`LightProbe::load` 하나만 위치를 안다). `Lighting::shadeLights` 가 덮는 볼륨들의 SH(삼선형, 가중 평균)로 앰비언트를 바꾼다. 상자 밖은 자르지 않는다. 스카이 전역 항목(볼륨 밖)은 D3-7.4 뒤로 남음, 그때까지 볼륨 밖은 지금의 앰비언트. 툰은 자기 앰비언트를 쓴다. `RenderPassGPUTest.LightProbeVolumeReplacesAmbient` · `SceneLightTest.CollectsBakedProbeVolumesAfterLights` 실행 **확인 필요** | P1, `D2-13` S1 |
| P4 | 반사 프로브(빌드 · 셰이더 쿠킹 · 화면 확인 전). `ReflectionProbeComponent`(상자 `_extent`, 섞는 폭 `_blendDistance`, `_probeDataPath`)와 `.reflprobe`(`ReflectionProbeData` — 매직 `SWRP`, 찍은 위치, 옥타헤드럴 한 변 · 거칠기 단계 수, 텍셀 float32 RGB; 찍은 위치가 지금 위치와 다르면 쓰지 않는다). GPU 베이크 `ReflectionProbeBaker`(자기 `FrameRenderer` 로 디퍼드를 90° 면 여섯으로 그려 `LitColor`(RGBA16F)를 읽는다 — 포워드의 SceneColor 는 RGBA8 이라 1 을 넘는 빛이 잘린다) → CPU 접기 · GGX 거르기 `ReflectionProbeFilter` → 에디터 `lighting.bakeReflectionProbes`(Build > Bake Reflection Probes, `<씬>.<오브젝트>.reflprobe`, `EditorReflectionProbeBakeSession` 이 프로브 단위로 에디터 프레임에 나눠 굽는다). **아틀라스 슬롯을 새로 두지 않았다** — 옥타헤드럴 텍셀을 라이트 프로브 버퍼(t13)에 SH9 원소와 함께 이어 붙이고 셰이더가 수로 쌍선형한다(`LightData::sampleReflectionProbe`; 에뮬 백엔드도 같은 길). 라이트 원소 `SW_LIGHT_TYPE_REFLECTION_PROBE`(13)는 경계 구로 그리드에 든다. 조명 식은 반사 앰비언트(EnvBRDFApprox × 앰비언트)를 덮는 프로브의 빛으로 바꾼다. `ReflectionProbeDataTest` · `SceneLightTest.CollectsBakedReflectionProbesAfterVolumes`. **남음**: 시차 보정(상자 투영), 프로브 버퍼를 프레임마다 다시 올리는 비용(프로브 하나가 약 6000 원소 — 바뀐 프레임에만 올리기), GL · DX11 에서 에디터가 렌더 스레드의 컨텍스트를 잡는지, 굽는 시나리오 · 화면 확인, 프로브 하나 안을 면 단위로 나누기 | `D2-6.3` 슬롯 지도, P5 |
| P5 | 금속도와 거칠기 PBR(빌드 · 셰이더 쿠킹 · 화면 확인 전). `Lighting::shadeLights( …, roughness, metallic )`(언리얼 기본 셰이딩 모델과 같은 GGX · 스미스 결합 가시성 근사 · 슐릭, 확산은 `기본 색 × (1 − 금속도)`, 환경 반사는 `EnvBRDFApprox` × 앰비언트), 머티리얼 필드 `metallic`(forwardlit 머티리얼 43 개에 `0.0` 을 적었다 — 빠진 필드를 0 으로 채우는 길에 기대지 않는다), G버퍼 셋째 타깃. `RenderPassGPUTest.MetallicChangesForwardAndDeferredPictures`. 골든 이미지는 다시 찍어야 한다(유전체에도 하이라이트가 생겼다) | `D2-6.4` |
| D2-6.4 | G버퍼 셋째 타깃(빌드 전). 첨부 `GBufferMaterial`(R8G8B8A8_UNORM, r 금속도 · g 거칠기, b · a 안 씀 — `SurfaceOutput::encodeGbufferMaterial` / `PassTexture::loadGbufferMaterial`), 역할 `RenderPassInputRole::GBufferMaterial`(PassCB `g_GBufferMaterialIndex`), GBuffer 패스 MRT 셋, Lighting 계약의 필수 입력. 에뮬 백엔드(DX11 · GL)는 조명 패스가 엔진 텍스처 다섯을 읽어 t0..t3 이 모자라 **t15** 에 따로 둔다(`SW_SLOT_ENGINE_GBUFFER_MATERIAL` — 띠 꼭대기, `ShaderBindingSlots.h` 가 띠가 닿으면 막는다. DX11 은 s15 가 정적 세트라 정적 엔진 텍스처 샘플러로 읽음). 셰이딩 모델은 알베도 알파에 둔다 | 없음 |
| P6 | 에디터 베이크: `lighting.bakeProbes`(Build > Bake Light Probes — 선택한 볼륨, 없으면 모든 볼륨), 결과 `<씬>.<오브젝트>.probes` 와 볼륨의 `_probeDataPath`, 탐침 `Editor.LightProbeBakeCount` · `Editor.BakedLightProbeVolumeCount`, 시나리오 `lightprobebake.scenario.xml`. 나눠 굽기 · 취소(`EditorLightProbeBakeSession`). 가림은 플레이 물리 씬이 아니라 굽기마다 씬의 정적 콜라이더로 세운 씬이다(`ScenePhysics::createStaticScene3D` ← `PhysicsComponent::appendStaticBodies3D` — 언리얼 Lightmass · 유니티 Contribute GI 와 같은 자리). 편집 모드에서도 굽고 움직이는 바디는 들지 않으며 물리 서비스가 없으면 오류로 멈춘다. **남음**: 볼륨을 실제로 굽는 시나리오(파일을 쓰는)와 실행 확인, 굽기를 작업 스레드로 옮기기(가림 씬이 굽기의 사본이라 가능) | P2 |

## 3. 충돌 주의

P5 · D2-6.4 의 셰이더 소스(binding.hlsli · bindingslots.hlsli · lighting.hlsli · forwardlit · gbuffer · deferredlighting · terrain · foliage · water)는 커밋 훅(`CheckShaderStamps`)이 쿠킹 바이너리와 함께 커밋하라고 막아 **패치로 따로 넘겼다**. C++ 는 `SW_SLOT_ENGINE_GBUFFER_MATERIAL` 을 쓰므로 그 패치를 붙이고 `App --cook-shaders` 한 뒤에 빌드된다. binding.hlsli 의 PassCB 와 bindingslots.hlsli 는 다른 작업과 자주 겹친다 — 슬롯은 t15(에뮬 전용)만 새로 썼다.
