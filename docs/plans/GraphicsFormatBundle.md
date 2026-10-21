# 그래픽스 형식 묶음 — HDR 장면 색 · 툰 · 변형 탄젠트 · Shaded Wireframe

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-6, D2-10 · 진행: 1/5

후보 `ForwardHdrSceneColor` · `ToonShadingCompletion` · `ViewModeCompletion`(Shaded Wireframe)을 옮겨 왔다. 셰이더 소스를 바꾸는 단위는 커밋 훅(`CheckShaderStamps`)이 다시 굽기를 요구해, App 빌드가 있는 통합 때 패치를 적용하고 `App.exe --cook-shaders` 와 함께 커밋한다. 패치는 번호 순서대로 적용한다(뒤 패치가 앞 패치 위에 만들어졌다).

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| G1 | 포워드 파이프라인 넷의 `SceneColor`(단계 파이프라인은 `BloomColor` · `OutlineColor` 도)를 `R16G16B16A16_FLOAT` 로, `postchain.hlsl` 블룸 뒤 `saturate` 제거 | XML 커밋됨, 셰이더는 패치 `gfxcand-01-postchain-hdr` |
| G2 | 툰 포워드 전용 지킴(`SW_PASS_GBUFFER` 는 `#error`) · MToon UV 애니메이션(스크롤 · 회전 · 마스크, PassCB `g_SwMaterialTime`) · VRM 임포터 매핑(0.x · 1.0) · 툰 머티리얼 열아홉에 프로퍼티 넷 | 패치 `gfxcand-02-toon-forwardonly-uvanimation` |
| G3 | 모프 풀 정점에 탄젠트 칸(`GPUMorphVertex::_tangent`, `SW_MORPH_FLOAT4_PER_VERTEX` 3) — 스키닝 · 모프 컴퓨트가 탄젠트를 돌리고 툰이 `swLoadMorphedTangent` 로 읽는다 | 패치 `gfxcand-03-morph-skin-tangent` |
| G4 | RHI 깊이 바이어스(`RHIPipelineStateDesc::_depthBias` · `_slopeScaledDepthBias`, 네 백엔드) | 커밋됨 |
| G5 | 보기 모드 `ShadedWireframe`(11) — 표의 `_bOverlay`, 지오메트리 패스가 Lit 과 선(`SW_VIEWMODE_WIREOVERLAY`, 와이어프레임 · 바이어스 · 깊이 쓰기 끔)으로 두 번 그린다 | 패치 `gfxcand-04-shaded-wireframe` |

## 남은 것

- 통합: 패치 01 → 04 적용, Debug 전체 빌드, `App.exe --cook-shaders` 후 `ShaderCookRequestTest` 가 알리는 낡은 바이너리 정리(툰 G버퍼 변형은 이제 쿠킹되지 않는다), `ShaderBindingValidatorTest` · `VrmMaterialImporterTest` · `RenderViewModeTest` · `RenderPassGPUTest.FusedPostChainMatchesStaged` · `MorphPoolIdentityMatchesRest`.
- G1: 1.0 을 넘던 픽셀만 밝아지는 의도한 룩 변경이다. 골든 넷(Empty · NileCity · Shooter3D · StarSkirmish)을 `--record --runs 3` 로 다시 뜨고, 대역폭 차이를 Release 로 잰다. 2D 파이프라인은 톤맵을 안 해 그대로다.
- G2: 0.x 변환(v 스크롤 · 회전 방향 뒤집기, 회전 × 2π)과 1.0 의 회전 방향은 UniVRM · three-vrm 과 눈으로 대조하지 않았다 — UV 애니메이션이 있는 VRM 으로 확인한다. 외곽선 퍼뮤테이션(정점이 머티리얼을 읽는다)과 맷캡에는 적용하지 않는다. 시계는 군중 시계(`AnimationCrowd::getClock`) float32 라 오래 켜 두면 정밀도가 준다.
- G3: 모프 풀이 정점당 32 → 48 바이트다. `RT.GPUScene` 업로드 · 스키닝 비용을 Release 로 잰다. 모프 차이에는 탄젠트 차이가 없어 변형된 노멀에 다시 직교화한다.
- G5: 네 백엔드 스크린샷(포워드 · 디퍼드 · 깊이 프리패스)으로 선이 면 위에 보이고 z-fighting 이 없는지 본다. 바이어스 값(상수 -4 · 기울기 -1, `FrameRendererPSO.cpp` applyViewModeToDesc)은 그때 맞춘다. Vulkan 은 `fillModeNonSolid` 가 없으면 솔리드로 그린다(기존 동작).
