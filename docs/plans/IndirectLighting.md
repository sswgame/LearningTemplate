# 간접 조명 구현 단위 (`D2-12`)

> 상태: 구현중 · 선행: ClusteredLighting · 영역: graphics · 옛번호: D2-12 · 진행: 5/7

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
| P2 | CPU 베이크(`LightProbeBaker`): 프로브마다 균일 구 레이를 쏘아 닿은 면이 튕긴 직접광(반사율은 볼륨의 `_bounceAlbedo`)과 빈 방향의 하늘을 SH 로 투영한다. 직접광 자체는 담지 않는다(조명 식이 이미 계산해 두 번 비춘다). 가림은 물리 레이캐스트 — **들어감**. 하늘 항은 `collectInput` 이 `SkyComponent::findSky` 로 그러데이션을 받아(`LightProbeBakeInput::_sky`, 옛 SH 포인터 `_pSky` 를 뺐다) 빈 방향마다 `SkyGradient::evaluateRadiance` 로 센다 — **들어감**(빌드 미확인). 스카이가 없으면 세기 0 이라 0 이다. 물리 씬이 있는 굽기 테스트(하늘 항 포함)는 **남음** | P1, `D3-7.4`(하늘) |
| P3 | 셰이더 소비: 볼륨은 라이트 목록의 `SW_LIGHT_TYPE_PROBE_VOLUME` 원소(클러스터 그리드 항목), SH9 는 전용 슬롯 t13 `g_SwLightProbes`(`swLoadProbe` 하나만 위치를 안다). `swShadeLights` 가 덮는 볼륨들의 SH(삼선형, 가중 평균)로 앰비언트를 바꾼다. 상자 밖은 자르지 않는다 — **들어감**. 스카이 전역 항목(볼륨 밖)은 D3-7.4 뒤로 남음, 그때까지 볼륨 밖은 지금의 앰비언트. 툰(`toon.hlsl`)은 자기 앰비언트를 그대로 쓴다. `RenderPassGPUTest.LightProbeVolumeReplacesAmbient` · `SceneLightTest.CollectsBakedProbeVolumesAfterLights` 실행은 **확인 필요** | P1, `D2-13` S1 |
| P4 | 반사 프로브: GPU 베이크(씬을 여섯 면으로 그려 옥타헤드럴로 접고 거칠기 밉을 만듭니다)와 아틀라스 슬롯 | `D2-6.3` 슬롯 지도, P5 |
| P5 | 금속도와 거칠기 PBR(GGX, 슐릭 프레넬), 머티리얼 필드와 G버퍼 칸 | `D2-6.4` G버퍼 확장 |
| P6 | 에디터 베이크: 커맨드 `lighting.bakeProbes`(Build > Bake Light Probes — 선택한 볼륨, 없으면 모든 볼륨), 결과는 씬 옆 `<씬>.<오브젝트>.probes` 와 볼륨의 `_probeDataPath`, 탐침 `Editor.LightProbeBakeCount` · `Editor.BakedLightProbeVolumeCount`, 시나리오 `engine/automation/editor/lightprobebake.scenario.xml` — **들어감**. 굽기는 동기라 진행 표시는 끝난 뒤 알림 하나다(긴 굽기를 비동기 작업으로 옮기는 일은 **남음**). 볼륨을 실제로 굽는 시나리오(파일을 쓰는)와 시나리오 실행은 **확인 필요** | P2 |

## 3. 충돌 주의

P5 는 머티리얼 상수와 G버퍼 레이아웃을 바꾸므로 그래픽스 형식 묶음(feat7: 탄젠트, 머티리얼 슬롯 16, G버퍼)이 들어간 뒤에 시작합니다.
