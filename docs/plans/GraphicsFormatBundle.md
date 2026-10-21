# 그래픽스 형식 묶음 — HDR 장면 색 · 툰 · 변형 탄젠트 · Shaded Wireframe · 2D 조명 · VRM 런타임

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-1, D2-6, D2-7, D2-10 · 진행: 3/9

후보 `ForwardHdrSceneColor` · `ToonShadingCompletion` · `ViewModeCompletion`(Shaded Wireframe) · `TwoDLightingExtras` · `VrmRuntimeConsumption` 을 옮겨 왔다.
셰이더 소스를 바꾸는 단위는 커밋 훅(`CheckShaderStamps`)이 다시 굽기를 요구해 워크트리에서 커밋하지 못했다. 그 단위는 **패치 하나**
`D:\w\gfxcand-patches\gfxcand-shader-units-01-05-on-ab36bf4c7.patch`(바뀌는 파일 목록은 같은 폴더의 `gfxcand-shader-units-files.txt`)로 넘기고, App 빌드가 있는 통합 때
적용해 `App.exe --cook-shaders` 와 함께 커밋한다. 단위별 옛 패치는 `history` 폴더에 있다(앞 패치 위에 만든 것이라 차례로만 맞는다).

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| G1 | 포워드 파이프라인 넷의 `SceneColor`(단계 파이프라인은 `BloomColor` · `OutlineColor` 도)를 `R16G16B16A16_FLOAT` 로, `postchain.hlsl` 블룸 뒤 `saturate` 제거 | XML 커밋됨, 셰이더는 패치 |
| G2 | 툰 포워드 전용 지킴(`SW_PASS_GBUFFER` 는 `#error`) · MToon UV 애니메이션(스크롤 · 회전 · 마스크, PassCB `g_SwMaterialTime`) · VRM 머티리얼 임포터 매핑(0.x · 1.0) · 툰 머티리얼 열아홉에 프로퍼티 넷 | 패치 |
| G3 | 모프 풀 정점에 탄젠트 칸(`GPUMorphVertex::_tangent`, `SW_MORPH_FLOAT4_PER_VERTEX` 3) — 스키닝 · 모프 컴퓨트가 탄젠트를 돌리고 툰이 `swLoadMorphedTangent` 로 읽는다 | 패치 |
| G4 | RHI 깊이 바이어스(`RHIPipelineStateDesc::_depthBias` · `_slopeScaledDepthBias`, 네 백엔드) | 커밋됨 |
| G5 | 보기 모드 `ShadedWireframe`(11) — 표의 `_bOverlay`, 지오메트리 패스가 Lit 과 선(`SW_VIEWMODE_WIREOVERLAY`, 와이어프레임 · 바이어스 · 깊이 쓰기 끔)으로 두 번 그린다 | 패치 |
| G6 | 2D 조명: 자유 모양 빛(`FreeformLight2DComponent`, 빛 원소 뒤의 변 원소) · 부드러운 그림자(그림자 원천 반경, 표본 다섯) · 섞는 방식(`Light2DBlendStyle` 더하기 · 곱하기 · 빼기). `GPULight` 에 덧칸 `_extra`(80 바이트) | 패치 |
| G7 | 2D 빛 텍스처(쿠키) | 설계만 — 아래 |
| G8 | VRM 런타임: 임포트가 표정 → 얼굴 리그(`.facial.json`), 스프링 본 → 후처리 리그(`.rig.json`, `SpringChain`)를 쓴다(`VrmRuntimeAssets`) | 커밋됨 |
| G9 | 조용한 폴백 정리: 보기 모드 범위 밖 값(`isRenderViewModeIndex`, `-gv_viewMode` 는 초기화 실패), Vulkan 와이어프레임 미지원(`RHICapabilities::_bWireframeFill`, PSO 거절) | 커밋됨 |

## 남은 것

- 통합: 패치 적용, Debug 전체 빌드, `App.exe --cook-shaders` 후 `ShaderCookRequestTest` 가 알리는 낡은 바이너리 정리(툰 G버퍼 변형은 이제 쿠킹되지 않는다), `ShaderBindingValidatorTest` · `VrmMaterialImporterTest` · `RenderViewModeTest` · `Light2DTest` · `RenderPassGPUTest.FusedPostChainMatchesStaged` · `MorphPoolIdentityMatchesRest` · `Light2DFalloffAndShadowOnEveryBackend`.
- G1: 1.0 을 넘던 픽셀만 밝아지는 의도한 룩 변경이다. 골든 넷(Empty · NileCity · Shooter3D · StarSkirmish)을 `--record --runs 3` 로 다시 뜨고, 대역폭 차이를 Release 로 잰다. 2D 파이프라인은 톤맵을 안 해 그대로다.
- G2: 0.x 변환(v 스크롤 · 회전 방향 뒤집기, 회전 × 2π)과 1.0 의 회전 방향은 UniVRM · three-vrm 과 눈으로 대조하지 않았다 — UV 애니메이션이 있는 VRM 으로 확인한다. 외곽선 퍼뮤테이션과 맷캡에는 적용하지 않는다. 시계는 군중 시계(`AnimationCrowd::getClock`) float32 라 오래 켜 두면 정밀도가 준다.
- G3: 모프 풀이 정점당 32 → 48 바이트다. `RT.GPUScene` 업로드 · 스키닝 비용을 Release 로 잰다. 모프 차이에는 탄젠트 차이가 없어 변형된 노멀에 다시 직교화한다.
- G5: 네 백엔드 스크린샷(포워드 · 디퍼드 · 깊이 프리패스)으로 선이 면 위에 보이고 z-fighting 이 없는지 본다. 바이어스 값(상수 -4 · 기울기 -1, `FrameRendererPSO.cpp` applyViewModeToDesc)은 그때 맞춘다.
- G6: 2D 쇼케이스 씬에 자유 모양 · 곱하기 · 부드러운 그림자 빛을 두고 네 백엔드로 찍는다. 부드러운 그림자는 그림자를 켠 빛마다 가림막 검사가 다섯 배다 — 토막 수백 개 씬에서 GPU 시간을 잰다.
- G7 설계: 빛마다 텍스처를 읽으려면 에뮬 백엔드(DX11 · GL)에 패스 단위 텍스처 칸이 있어야 한다. 엔진 슬롯 t0..t3 은 역할(그림자 · G버퍼 · 깊이)이 차지하고 머티리얼 슬롯은 드로우마다 머티리얼 것이다. 권고: 2D 패스 역할 `Light2DCookieAtlas` 하나(장면의 쿠키를 렌더러가 아틀라스 렌더 타깃에 모은다)를 `commitBindlessTextureBindings` 의 역할 표에 더하고, 빛 원소는 아틀라스 UV 사각형을 `_extra.w` 번째 덧원소로 싣는다. 쿠키 에셋 수명은 `TextureCache::acquire` · `release` 짝을 컴포넌트가 쥔다.
- G8: 스프링 물성은 같은 식이 아니라 옮김이다(강성 × 스텝, 끌림 = 감쇠, 중력 ÷ 스텝) — 실제 VRM(AvatarSample_D)을 다시 임포트해(`kVersion` 2) 흔들림을 눈으로 맞춘다. 캐릭터 프리팹이 두 파일을 참조해야 런타임이 쓴다(얼굴 리그 · `PoseModifierComponent`). 1인칭 메시 숨김과 본 메시 두 벌(`<이름>.mesh`)은 엔진 메시에 머티리얼 구간이 생긴 뒤다. VRoid 텍스처 BC7 재임포트는 Release 로 한다([검증과 측정](../08_Verification.md) 2절, 512² 한 장 8 초). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 의 EditorTest 한도 30 → 120 초는 임포트를 줄이면 되돌린다.
