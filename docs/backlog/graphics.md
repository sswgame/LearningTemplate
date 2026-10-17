# 백로그: 그래픽스, RHI, 셰이더

렌더러, RHI 백엔드, 셰이더, 머티리얼, 환경 렌더링의 남은 일입니다. 상태 태그의 뜻, 일을 고르는 순서, 새 항목을 쓰는 법은 [백로그 안내](README.md)에 있습니다.

- `D2-1` [기능] **2D 의 남은 것** — 2D 빛 텍스처 · 자유 모양 빛 · 부드러운 그림자 · 빛 블렌드 스타일.
  넷 모두 빛 컴포넌트(`Source/Engine/Object/Component/2D/Light2DComponent`)의 데이터부터 바꿔야 해 렌더러만 맡은 작업자는 시작하지 못했다(2026-10-17).

- `D2-2` [기능] **방향광 그림자의 남은 것** — 그림자 아틀라스(캐스케이드 · 뷰별 볼륨 · 점광 · 스폿 · 경계 구 맞춤)는 들어갔다(Renderer README "다중 뷰").
  (1) 그림자 컬링 상자가 하나(방향광 볼륨 + 로컬 경계 구)라 캐스케이드마다 따로 거르지 않는다 — 뷰마다 그림자 컬링 칸이 하나(`_shadowCullSlot`)라 타일마다 칸을 늘리는
  GPU 컬링 구조 변경이고, 이득은 그림자 패스 드로우 수를 재 본 뒤에 판단한다(`GPU.Shadow`, 측정 전).
  (3) 바이어스가 텍셀 단위로 줄어 여섯 게임의 여드름을 아직 눈으로 안 봤다 — 경계 구 맞춤 · envshowcase CSM 은 새 빌드로 게임별 스크린샷 전후를 다시 찍는다(게임마다 `SW_ACTIVE_GAME` 빌드가 따로 필요해 통합 담당의 게임별 빌드 때).

- `D2-5` [기능] **환경(지형 · 식생 · 물, `Engine/Environment`)의 남은 것** — 들어간 것과 계약(지형 충돌 · 부력 포함)은 `Source/Engine/Environment` 각 헤더 주석.
  (2) 강 경로가 점 목록이다 — 스플라인 컴포넌트가 Engine 에 들어오면 그것을 경로로 받는다. (3) 하늘 · 시간 · 높이 안개 + 물속 안개 패스(값은
  `WaterBodyComponent::findUnderwaterFog` 가 이미 준다) — 다중 뷰 병합 뒤. (4) 물의 굴절 · 화면 공간 두께는 반투명 패스가 장면 색 사본 · 장면 깊이를 입력으로 받는
  계약이 있어야 한다(지금은 지형 깊이를 정점에 굽는다).
  (6) 지형 LOD 교체 프레임의 최악 시간이 기준(GT 2 ms)을 넘는다(측정값은 [검증과 측정](../08_Verification.md) 2절 "지형 LOD 교체"). 메시 집합이 바뀌어 RT 는 정점 풀을
  통째로 다시 만든다(`RT.GPUScene.vertexPool`, 벤치에는 없다 — App 표로). LOD 메시를 미리 만들어 두거나 청크 정점을 풀에서 부분 갱신한다. 지오모프(LOD 튐) · 레이어 다섯 이상(두 번째 스플랫 — 머티리얼 텍스처 칸이 넷이다)이 없다.
  (7) 지형 쓰기(결정은 [결정 기록](../09_Decisions.md) 5-5 — GPU 스플랫 구간 업로드 · 청크별 충돌 바디 · 메시 재생성 나눔 · 식생 · 내비 연결 · 에디터 `TerrainBrushTool` 과 시나리오
  `editor.terrainbrush` 까지 들어갔다)의 남은 것 — ⓐ 측정이 Debug 뿐이다: `TerrainBenchTest.ColliderRebuildAfterBrushWrite` · `WriteHeightsRectSizes` 를 Release 로 다시 재고
  `TerrainComponent` 의 청크 재생성 예산(쓰기 · 틱마다 넷)을 GT 2 ms 에 맞춘다. ⓒ 식생은 드래그가 끝나면 숲 전체를 다시
  뿌린다(쇼케이스 Debug 약 0.86 s) — 바뀐 셀만 다시 계산한다. 내비는 런타임 내비메시 타일만 다시 굽는다(에디터가 구운 내비 에셋은 다시 굽기 명령으로). ⓓ 에디터 편집 모드에서도 틱이 남은 청크를
  만드는지 확인하지 않았다(브러시 한 번은 쓰기에서 끝난다). ⓔ 브러시 선택은 레이 전진이 걸음마다 `findTerrainAt` 으로 지형을 훑는다(측정 안 함).

- `D2-6` [기능] **툰 머티리얼(`toon.hlsl`, MToon 1.0 체계)의 남은 것** — 노멀 맵(정점에 탄젠트가 없다) · UV 스크롤 애니메이션 · 셰이딩 시프트 / 림 곱 / 외곽선 두께 텍스처(머티리얼 텍스처 칸이 넷이라 기본 · 그림자 · 발광 · 맷캡만 받는다) · 디퍼드의 계단 셰이딩(G버퍼는 표면만 적어 램버트로 칠해진다).
  UV 스크롤 · 텍스처 칸 늘리기는 VRM 머티리얼 임포터(`Source/Editor`)의 매핑과 같이 바꿔야 해 그래픽 작업자 혼자 끝낼 수 없다.

- `D2-7` [기능] **VRM 임포트의 남은 것** — humanoid 본 표 · 표정 · 스프링 본 · 1인칭은 `<이름>.avatar.json` 으로 나오지만 데이터뿐이다. 런타임이 읽는 타입이 없다: 표정 → 모프 가중치, 스프링 본 시뮬레이션,
  1인칭 메시 숨김(스키마 테스트는 JSON 객체와 `_version` · `_mapHumanBone` 만 본다). 런타임 타입은 `Engine/Character` · `Engine/Animation` 에, BC7 재임포트는 Editor 임포터에 있어 그래픽 작업자의 파일 밖이다. 본 메시(`<이름>.mesh`)는 구간들을 다시 합친 것이라 디스크에 두 벌이다(VRoid 34k 삼각형 7 MB × 2) — 엔진 메시에 머티리얼 구간이 생기면 하나로 줄인다.
  VRoid 텍스처는 BC3 이다(Debug DirectXTex 의 BC7 은 512×256 한 장도 10 분이 넘는다 — Release 로 BC7 임포트를 다시 할 것). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 는 Debug 에서 혼자 31 초라 EditorTest 한도를 30 → 120 초로 올려 두었다 — 임포트를 줄이면 되돌린다.

- `D2-9` [기능] **반해상도 블룸의 Release 측정** — `deferredpipeline.xml` 블룸의 밝은 부분을 반해상도 패스(`BloomDownsample`)로 나눴다. p50 · p99 를 `GPU.PostBloom`(전) 대
  `GPU.BloomDownsample` + `GPU.PostBloom`(후)로 잰다. 지면 되돌린다.

- `D2-10` [기능] **뷰포트 보기 모드의 남은 것**(2026-10-14, `RenderViewMode` — 언리얼 View Mode · 유니티 Draw Mode 와 견준 것은 기억에 의존한 비교다). `LightingOnly` 는 들어갔다 —
  `toon` · `sprite2dlit` 포워드 경로까지 회색으로 바꾸고 4 백엔드 그림(`RenderPassGPUTest.ViewModesProduceDistinctPictures`, 포워드 · 디퍼드)은 2026-10-17 Debug 로 통과했다.
  Shipping 베이크(쿠킹한 LightingOnly 변형으로 Shipping hostgpu)는 Shipping 전체 빌드가 필요해 통합 담당의 Shipping 확인 때 돌린다.
  안 한 것: Roughness / Metallic 버퍼 보기(G버퍼에 그 칸이 없다 — 칸을 늘리거나 패스를 새로 만들어야 한다), Shaded Wireframe(패스가 둘 필요), Light Complexity · Mipmaps(그리는 경로가 새로 필요).

- `D2-11` [기능] **파티클 · VFX 시스템이 없다.** 언리얼 Niagara · 유니티 VFX Graph · Godot GPUParticles 에 해당한다. 결정은 결정 기록 5-12 의 `D2-11` 두 행(CPU · GPU 둘 다, Shipping 은 GPU 고정, 고정 모듈 + `FloatCurve`, 방출기 단위 투명 정렬, 서버는 시뮬레이션 안 함)이다.
  단위와 계약은 [파티클 · VFX 구현 단위](../plans/Vfx.md) V0(데이터 형식) ~ V6(메시 입자 · 리본 · 충돌)에 있다. 코드는 아직 없다. V4(GPU 시뮬)는 `D2-6.3` 의 슬롯 지도 재작성 뒤다.

- `D2-12` [기능] **간접 조명이 없다 — 반사 프로브 · 라이트 프로브.** 결정은 결정 기록 5-12 의 `D2-12` 행(SH9 균일 격자, 옥타헤드럴 아틀라스, `.probes` 커밋, 금속도 · 거칠기 PBR)이다.
  단위와 계약은 [간접 조명 구현 단위](../plans/IndirectLighting.md) P0(SH 수학) ~ P6(에디터 베이크 UI)에 있다. 코드는 아직 없다. P5(PBR)는 그래픽스 형식 묶음(feat7: 머티리얼 슬롯 16 · G버퍼) 뒤다. 라이트맵 · 실시간 GI 는 `D2-14`.

- `D2-13` [기능] **클러스터드 라이팅의 남은 단위.** 설계는 결정 기록 5-12, 단위와 계약은 [클러스터드 라이팅 구현 단위](../plans/ClusteredLighting.md)에 있다. `D2-12` 와 `D2-11` 의 선행이다.
  S0(키 라이트 폴백 제거 + 커서 API)과 S1(CPU `LightGrid`, `gv_lightCulling`)은 `wt/feat15` 에 빌드 없이 들어갔다. 통합 때 빌드, `LightGridTest` · `ShadowSetupTest`, 셰이더 다시 베이크,
  골든 바이트 비교(네 백엔드 × Empty · NileCity · Shooter3D · StarSkirmish, `gv_lightCulling` 0 과 1), `RenderPassGPUTest` 의 라이트 없는 케이스 확인이 남았다.
  남은 단위: S2 GPU 생산자(`D2-6.3` 의 슬롯 지도 재작성 뒤), S3 측정, S4 Light Complexity, S5 프로브 · 파티클 라이트.

- `D2-14` [기능] **실시간 전역 조명과 볼륨 표현이 없다.** `D2-12` 의 프로브 뒤 단계: 라이트맵 또는 실시간 GI(Lumen · SDFGI · APV 갱신 방식 비교), 볼륨 안개 · 구름 · 대기 산란(`D2-5.2` 와 합침). 설계 문서가 먼저다. `D2-13` 선행.

- `E-3` [관찰] **Vulkan(FIFO) + VSync 에서 가끔 한 프레임이 여러 주기를 놓친다**(Release 큐브 100, 165 Hz — 조사 결과는 [검증과 측정](../08_Verification.md) 2절 "Vulkan VSync 프레임 놓침",
  스왑체인 이미지 4 개는 결정 기록 3절에서 기각). 다시 보이면 `-gv_tracy=1` 로 acquire · 펜스 · present 를 시간축으로 보고, 전체 화면(독점) · NVIDIA "Vulkan/OpenGL 프레젠트 방식" 설정을 바꿔 가른다.

- `E-13` [관찰] **실제 TDR 뒤 행은 이 PC 에서 재현하지 못했다**(옛 A-2). `rhi.removeDevice` 로 DX12(장치 제거) · Vulkan(손실 상태로 둠) 모두 프레임을 계속 돌고 종료 코드 0 으로 끝난다
  (2026-10-15 Vulkan 런타임 3/3 · 에디터 2/2, DX12 1/1). 남은 것은 드라이버 안의 대기(첫 `VK_ERROR_DEVICE_LOST` 를 돌려주기 전)뿐이다. 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 매달린 스레드의 스택을 남긴다.

- `G-12` [조건부] **임포터가 1 채널(R8) DDS 를 내는 규칙**(조건: 마스크 · 1 채널 텍스처 에셋이 생기면) — RHI · 로더는 `R8_UNORM`(DXGI 61, DX10 머리)을 읽는다. `App --import-textures` 의
  규칙 표(`TextureImportConfig.json`)에 R8 출력이 없다.

---

[◀ 백로그 안내](README.md)
