# 그래픽스 형식 묶음 — HDR 장면 색 · 툰 · 변형 탄젠트 · Shaded Wireframe · 2D 조명 · VRM 런타임 · 메시 구간

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: D2-1, D2-6, D2-7, D2-10 · 진행: 12/12

후보 `ForwardHdrSceneColor` · `ToonShadingCompletion` · `ViewModeCompletion`(Shaded Wireframe) · `TwoDLightingExtras` · `VrmRuntimeConsumption` 을 옮겨 왔다.
셰이더 소스를 바꾸는 단위는 워크트리에서 커밋하지 못해 패치로 넘겼고, 통합(2026-10-22, `f33a7f91f`)에서 적용해 쿠킹했다. 다른 패치와 겹친 자리:
2D 자유 모양 빛의 변 타입은 12(반사 프로브가 13), 드로우 루프의 보기 모드 두 번 그리기는 `isSameDrawGroup` · `drawBatchGroup` 이 받는다, 2D 쿠키는 밉 0 으로 읽는다(`PassTexture::sampleLight2dCookieAtlas` — DX11 X3511).

## 1. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| G1 | 포워드 파이프라인 넷의 `SceneColor`(단계 파이프라인은 `BloomColor` · `OutlineColor` 도)를 `R16G16B16A16_FLOAT` 로, `postchain.hlsl` 블룸 뒤 `saturate` 제거 | 들어감(XML 커밋, 셰이더는 통합) |
| G2 | 툰 포워드 전용 지킴(`SW_PASS_GBUFFER` 는 `#error`) · MToon UV 애니메이션(VRMC_materials_mtoon-1.0 "Transform Order" 의 식 — 회전 뒤 스크롤, PassCB `g_SwMaterialTime`) · VRM 머티리얼 임포터 매핑(0.x 는 UniVRM 이관과 같이 v 스크롤 부호 반전 · 회전 × 2π) · 툰 머티리얼 열아홉에 프로퍼티 넷 | 들어감(통합 — 빌드 · nogpu 테스트, 화면 확인 전) |
| G3 | 모프 풀 정점에 탄젠트 칸(`GPUMorphVertex::_tangent`, `SW_MORPH_FLOAT4_PER_VERTEX` 3) — 스키닝 · 모프 컴퓨트가 탄젠트를 돌리고 툰이 `MorphPool::loadTangent` 로 읽는다 | 들어감(통합 — 빌드 · nogpu 테스트, 화면 확인 전) |
| G4 | RHI 깊이 바이어스(`RHIPipelineStateDesc::_depthBias` · `_slopeScaledDepthBias`, 네 백엔드) | 커밋됨 |
| G5 | 보기 모드 `ShadedWireframe`(11) — 표의 `_bOverlay`, 지오메트리 패스가 Lit 과 선(`SW_VIEWMODE_WIREOVERLAY`, 와이어프레임 · 바이어스 · 깊이 쓰기 끔)으로 두 번 그린다 | 들어감(통합 — 빌드 · nogpu 테스트, 화면 확인 전) |
| G6 | 2D 조명: 자유 모양 빛(`FreeformLight2DComponent`) · 부드러운 그림자 · 섞는 방식(`Light2DBlendStyle`). `GPULight` 에 덧칸 `_extra`(80 바이트) | 들어감(통합 — 빌드 · nogpu 테스트, 화면 확인 전) |
| G7 | 2D 빛 쿠키: `PointLight2DComponent::_cookieTexturePath`, 렌더러의 쿠키 아틀라스(`Light2DCookieAtlas` — 1024² 에 칸 16, 칸마다 fullscreenblit 로 옮겨 그림), 패스 역할 `Light2DCookieAtlas`(PassCB `g_Light2DCookieAtlasIndex`, 에뮬 백엔드는 t1 을 알베도 · AO 와 나눔), 빛 원소 `_extra.w` 가 칸 번호 | 들어감 · 화면 확인(2026-10-22, 시나리오 `game/empty/automation/light2dcookie`, 네 백엔드) |
| G8 | VRM 런타임: 임포트가 표정 → 얼굴 리그(`.facial.xml`), 스프링 본 → 후처리 리그(`.rig.xml`, `SpringChain`)를 쓴다(`VrmRuntimeAssets`) | 커밋됨 |
| G9 | 조용한 폴백 정리: 보기 모드 범위 밖 값(`isRenderViewModeIndex`), Vulkan 와이어프레임 미지원(`RHICapabilities::_bWireframeFill`) | 커밋됨 |
| G10 | 메시 구간(`MeshSection`, `.mesh` 의 `SECT`) · `MeshComponent::_sectionName` · `MeshSectionComponent` · 1인칭 가시성(`MeshPersonVisibility`, `setFirstPersonViewOnObject`) · VRM 을 메시 한 벌 + 캐릭터 프리팹(`<y>.prefab.xml` — 얼굴 리그 · 후처리 리그 컴포넌트가 참조)으로 임포트 | 들어감(VAT 구간 오프셋 셰이더는 통합) |
| G11 | 같은 모양 결함: 머티리얼 PSO 변형을 못 만들면 패스 PSO 로 그리던 폴백을 오류 + 그리지 않음으로, 라이트 · 배치 원소 구조체를 공용 헤더 하나로(`lightdata.hlsli` · `batchdata.hlsli`, stride 검사 `ShaderBindingValidatorTest.LightElementLayoutMatchesCPUStruct`) | 들어감(통합 — 빌드 · nogpu 테스트, 화면 확인 전) |
| G12 | VRM 스프링 본 식 맞추기: 리그 사슬에 `axis_pull`(three-vrm `boneAxis × stiffness × delta`) · 관절마다 물성(`joints`) | 커밋됨 |

## 남은 것

- 통합: 패치 적용, Debug 전체 빌드, `App.exe --cook-shaders` 후 `ShaderCookRequestTest` 가 알리는 낡은 바이너리 정리(툰 G버퍼 변형은 이제 쿠킹되지 않는다), `ShaderBindingValidatorTest` · `VrmMaterialImporterTest` · `VrmRuntimeAssetsTest` · `RenderViewModeTest` · `Light2DTest` · `MeshAssetTest` · `ModelImporterTest` · `RenderPassGPUTest.FusedPostChainMatchesStaged` · `MorphPoolIdentityMatchesRest` · `Light2DFalloffAndShadowOnEveryBackend`.
- G1: 1.0 을 넘던 픽셀만 밝아지는 의도한 룩 변경이다. 골든 넷(Empty · NileCity · Shooter3D · StarSkirmish)을 `--record --runs 3` 로 다시 뜨고, 대역폭 차이를 Release 로 잰다.
- G2: VRM 1.0 스펙의 의사 코드(mat2 열 우선)와 수식(행렬)이 회전 부호에서 어긋난다 — 수식과 three-vrm(`mtoon.frag`)을 따랐다. 스펙 샘플 "VRMC_materials_mtoon UV Animation Test" 로 방향을 눈으로 확인한다. 외곽선 퍼뮤테이션과 맷캡에는 적용하지 않는다(맷캡은 스펙도 제외).
- G3: 모프 풀이 정점당 32 → 48 바이트다. `RT.GPUScene` 업로드 · 스키닝 비용을 Release 로 잰다.
- G5: 네 백엔드 스크린샷(포워드 · 디퍼드 · 깊이 프리패스)으로 선이 면 위에 보이고 z-fighting 이 없는지 본다. 바이어스 값(상수 -4 · 기울기 -1)은 그때 맞춘다.
- G7 쿠키는 `light2dcookie` 시나리오(무대 `maps/rendertest2d` — 같은 점광 둘, 한쪽만 창문 쿠키, 순간이동으로 맞바꿈)가 창살 무늬(쿠키 빛 안쪽의 어두운 픽셀 0.162, 쿠키 없는 빛 0)와 맞바꾼 사진의 차(0.095)를 단언한다. 네 백엔드가 같은 값이고 `AppScenarioTest`(hostgpu)가 돈다.
- G6: 2D 쇼케이스 씬에 자유 모양 · 곱하기 · 부드러운 그림자 빛을 두고 네 백엔드로 찍는다. 부드러운 그림자는 가림막 검사가 다섯 배다 — 토막 수백 개에서 GPU 시간을 잰다. 미리보기 렌더러(에디터 미리보기 창)는 쿠키 목록을 모으지 않아 쿠키 없이 그린다.
- G10: avatarsample_d 를 다시 임포트하면(`kVersion` 2 · `SECT`) 구간 메시 폴더가 사라지고 프리팹이 생긴다 — `toonshowcase.scene.xml` 의 구간 엔티티 열일곱을 프리팹 인스턴스 하나로 바꾼다. 1인칭 시점은 컴포넌트 단위라 모든 뷰가 같은 판정이다(분할 화면의 다른 플레이어 뷰를 따로 가르려면 뷰의 소유자 칸이 필요하다). 프리팹 XML 은 적은 칸만 쓰고 나머지는 기본값이다 — 스폰해 컴포넌트가 다 붙는지 본다.
- G12: 남는 차이 — 리그는 고정 스텝(1/60 초)이고 VRM 은 프레임 시간, 뼈 방향은 애니메이션 자세의 부모 방향(VRM 은 시뮬레이션된 부모 × 처음 로컬 회전), 0.x 끝 본의 가상 꼬리(7 cm)는 없다. 실제 모델로 흔들림을 본다.
- VRoid 텍스처 BC7 재임포트는 Release 로 한다([검증과 측정](../08_Verification.md) 2절). `ModelImporterTest.SkinnedModelImportsSkeletonClipsAndAttachments` 의 EditorTest 한도 30 → 120 초는 임포트를 줄이면 되돌린다.
