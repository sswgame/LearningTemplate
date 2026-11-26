/**
 * @file RenderPassNames.h
 * @brief 첨부 이름과 패스 상수 · 자원 이름의 hashed_string 캐시입니다(프레임 렌더러의 번역 단위들이 함께 쓴다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    /**
     * @struct AttachmentNames
     * @brief 어태치먼트와 패스 리소스 이름의 hashed_string 캐시입니다.
     * @details PassConstantNames 와 같은 이유로 둔다. hashed_string 생성은 전역 레지스트리 intern
     *          (FNV 해시, 32-way 샤드 뮤텍스, 조회)이다. 이 이름들은 모두 코드 리터럴이라
     *          값이 고정이므로 한 번만 intern 한다. 특히 `bindEngineTextures` 는 DX11 과 GL 경로에서
     *          드로우 호출마다 불리므로 이름을 그 자리에서 만들면 안 된다.
     * @note 문자열이 필요한 곳에는 `view()` 를 씁니다. 락 없는 O(1) 포인터 역참조입니다.
     */
    struct AttachmentNames
    {
        /// 첨부 이름 리터럴입니다(파이프라인 XML 의 첨부 `_name`).
        static constexpr auto kSwapchain         = "Swapchain";
        static constexpr auto kSceneColor        = "SceneColor";
        static constexpr auto kSceneDepth        = "SceneDepth";
        static constexpr auto kShadowMap         = "ShadowMap";
        static constexpr auto kLocalShadowMap    = "LocalShadowMap";
        static constexpr auto kGBufferAlbedo     = "GBufferAlbedo";
        static constexpr auto kGBufferNormal     = "GBufferNormal";
        static constexpr auto kGBufferMaterial   = "GBufferMaterial";
        static constexpr auto kGBufferCustomData = "GBufferCustomData";

        hashed_string _swapchain{ AttachmentNames::kSwapchain };
        hashed_string _sceneColor{ AttachmentNames::kSceneColor };
        hashed_string _sceneDepth{ AttachmentNames::kSceneDepth };
        hashed_string _shadowMap{ AttachmentNames::kShadowMap };
        /// 점광 · 스폿 그림자 아틀라스 — 첨부 이름이 곧 역할 이름(`g_LocalShadowMapIndex`)이다. 그림자 캐시 층도 복원 드로우 동안 이 이름으로 건다.
        hashed_string _localShadowMap{ AttachmentNames::kLocalShadowMap };
        hashed_string _gbufferAlbedo{ AttachmentNames::kGBufferAlbedo };
        hashed_string _gbufferNormal{ AttachmentNames::kGBufferNormal };
        hashed_string _gbufferMaterial{ AttachmentNames::kGBufferMaterial };
        hashed_string _gbufferCustomData{ AttachmentNames::kGBufferCustomData };

        /// 셰이더가 보는 이름입니다(어태치먼트 이름과 다를 수 있습니다. registerPassTexture 의 canonical 인자).
        /// 값은 RenderPassInputRole 의 이름과 같습니다. 역할로 바인딩하고 셰이더가 `g_<Role>Index` 로 읽습니다.
        hashed_string _sourceColor{ "SourceColor" };
        hashed_string _ambientOcclusion{ "AmbientOcclusion" };
        hashed_string _bloomBright{ "BloomBright" };
        /// 반투명 앞에 떠 둔 장면 색 사본(SceneColorCopy 패스의 출력) — 반투명 패스가 선언한 입력을 역할로 건다(물의 굴절).
        hashed_string _sceneColorCopy{ "SceneColorCopy" };
        /// 반투명 앞에 떠 둔 장면 깊이 사본(SceneDepthCopy 패스의 출력, R32 실수) — 반투명 패스가 선언한 입력을 역할로 건다(물의 화면 공간 두께).
        hashed_string _sceneDepthCopy{ "SceneDepthCopy" };
        /// 화면 속도(SceneVelocity, 불투명 지오메트리 패스의 출력). TAA · 모션 블러가 선언한 입력을 역할로 건다(`g_VelocityIndex`).
        hashed_string _velocity{ "Velocity" };
        /// 모션 블러 이웃 최대 속도(MotionBlurNeighborMax 출력) — 모션 블러가 `g_VelocityNeighborMaxIndex` 로 읽는다.
        hashed_string _velocityNeighborMax{ "VelocityNeighborMax" };
        /// DBuffer 데칼 셋(Decal 패스의 출력) — 베이스 패스(G버퍼 · 포워드 불투명)가 선언한 입력을 역할로 건다(`g_DBufferAIndex` · `g_DBufferBIndex` · `g_DBufferCIndex`).
        hashed_string _dbufferA{ "DBufferA" };
        hashed_string _dbufferB{ "DBufferB" };
        hashed_string _dbufferC{ "DBufferC" };
        /// 볼륨 구름(VolumetricClouds 패스의 저해상도 출력) — 스카이 패스가 선언한 입력을 역할로 건다(`g_SkyCloudsIndex`).
        hashed_string _skyClouds{ "SkyClouds" };
        /// 화면 왜곡 오프셋(SceneDistortion, 왜곡 누적 패스의 출력). 왜곡 적용 패스가 선언한 입력을 역할로 건다(`g_DistortionIndex`).
        hashed_string _distortion{ "Distortion" };
        /// 접촉 그림자 마스크(ContactShadow 패스의 출력, 채널 넷) — 조명 · 포워드 불투명 패스가 선언한 입력을 역할로 건다(`g_ContactShadowIndex`).
        hashed_string _contactShadow{ "ContactShadow" };
        /// 2D 빛 쿠키 아틀라스(Light2DCookieAtlas) — 파이프라인 첨부가 아니라 렌더러가 소유한 텍스처를 역할 이름으로 건다.
        hashed_string _light2DCookieAtlas{ "Light2DCookieAtlas" };
        /// 자동 노출의 이번 프레임 EV(1x1, 뷰의 `_autoExposureHistory`) — 톤맵하는 패스가 `g_AutoExposureIndex` 로 읽는다. 렌더러가 소유한 텍스처다.
        hashed_string _autoExposure{ "AutoExposure" };
        /// 컬러 그레이딩 LUT(뷰의 후처리 값 `_colorGradingLUT`, `PathTextureSet`) — 톤맵하는 패스가 `g_ColorGradingLUTIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _colorGradingLUT{ "ColorGradingLUT" };
        /// 겹친 볼륨이 섞는 둘째 컬러 그레이딩 LUT(뷰의 후처리 값 `_colorGradingLUTBlend`) — 톤맵하는 패스가 `g_ColorGradingLUTBlendIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _colorGradingLUTBlend{ "ColorGradingLUTBlend" };
        /// 블룸 먼지 마스크(뷰의 후처리 값 `_bloomDirtMask`) — 블룸을 더하는 패스가 `g_BloomDirtMaskIndex` 로 읽는다. 텍스처 캐시가 소유한 텍스처다.
        hashed_string _bloomDirtMask{ "BloomDirtMask" };
        /// 표면 변형 층의 높이 텍스처(패킷의 `SurfaceDeformBinding`) — 씬 지오메트리 패스의 정점 셰이더가 `g_SurfaceDeformIndex` 로 읽는다. 텍스처 캐시가 소유한 CPU 텍스처다.
        hashed_string _surfaceDeform{ "SurfaceDeform" };
        /// 비 가림 맵의 깊이(`FrameRainOcclusion`) — 표면 셰이더가 `g_RainOcclusionIndex` 로 읽는다. 렌더러가 소유한 텍스처다.
        hashed_string _rainOcclusion{ "RainOcclusion" };
    };

    /**
     * @brief 프로세스 전역 AttachmentNames 를 반환합니다.
     * @details 함수 지역 static 이라 문자열 레지스트리보다 먼저 초기화될 위험이 없다.
     */
    inline const AttachmentNames& attachmentNames()
    {
        static const AttachmentNames s_names{};
        return s_names;
    }

    /**
     * @brief PassCB 와 리소스 이름의 hashed_string 캐시입니다.
     * @details hashed_string 생성은 전역 문자열 레지스트리에 intern 하는 작업이다(FNV 해시,
     *          샤드 공유락, 조회). 리터럴은 값이 고정이므로(`g_World` 처럼 드로우마다 쓰는 것도) 한 번만 만들어 다시 쓴다.
     */
    struct PassConstantNames
    {
        hashed_string _lightViewProj{ "g_LightViewProj" };
        hashed_string _viewProj{ "g_ViewProj" };
        /// @brief 뷰-투영의 역행렬입니다. 디퍼드 조명이 깊이에서 월드 위치를 복원합니다.
        hashed_string _invViewProj{ "g_InvViewProj" };
        /// @brief 지난번 이 뷰를 그린 뷰-투영입니다(모션 블러가 화면 속도를 구한다, motion_blur.hlsl). 처음 · 컷 프레임은 이번 것과 같다.
        hashed_string _prevViewProj{ "g_PrevViewProj" };
        hashed_string _temporalJitter{ "g_TemporalJitter" }; ///< 이번 프레임 투영 지터(xy, NDC) — 화면 속도가 뺀다
        hashed_string _textureMipBias{ "g_TextureMipBias" }; ///< 머티리얼 텍스처 밉 바이어스(x = log2(렌더 높이 / 출력 높이), `ViewTextureMipBiasUtil`)
        hashed_string _world{ "g_World" };
        hashed_string _keyLightDirIntensity{ "g_KeyLightDirIntensity" };
        hashed_string _keyLightColor{ "g_KeyLightColor" };
        /// @brief 하늘 그러데이션입니다(스카이 패스). 배치는 `SkyGradient` 머리 주석 — 천정(rgb, w 지수) · 지평선(rgb) · 땅(rgb, w 세기).
        hashed_string _skyZenith{ "g_SkyZenith" };
        hashed_string _skyHorizon{ "g_SkyHorizon" };
        hashed_string _skyGround{ "g_SkyGround" };
        /// @brief 대기 산란과 구름 층입니다(스카이 패스). 배치는 `FrameRenderer::FrameSkyState` 주석.
        hashed_string _skyAtmosphereRayleigh{ "g_SkyAtmosphereRayleigh" };
        hashed_string _skyAtmosphereMie{ "g_SkyAtmosphereMie" };
        hashed_string _skyAtmosphereGeometry{ "g_SkyAtmosphereGeometry" };
        hashed_string _skyCloudShape{ "g_SkyCloudShape" };
        hashed_string _skyCloudWind{ "g_SkyCloudWind" };
        hashed_string _skyCloudColor{ "g_SkyCloudColor" };
        hashed_string _skyCloudVolume{ "g_SkyCloudVolume" };
        hashed_string _skyCloudSun{ "g_SkyCloudSun" };
        hashed_string _skyCloudAmbient{ "g_SkyCloudAmbient" };
        hashed_string _skyCloudGround{ "g_SkyCloudGround" };
        hashed_string _temporalSample{ "g_TemporalSample" }; ///< 기록 첨부를 쓰는 패스마다 그 기록의 시간 표본(`FrameRecorder::bindOutputHistory`)
        /// @brief 스카이 라이트의 프로브 버퍼 자리입니다(lighting.hlsli `SkyLight`). 배치는 `LightProbeElementBuilder::getSkyLight`.
        hashed_string _skyLight{ "g_SkyLight" };
        /// @brief 높이 · 거리 지수 안개입니다(안개 패스). 배치는 `FogSettings` 머리 주석 — 색 · 밀도 / 높이 감쇠 · 기준 높이 · 시작 거리 · 최대 불투명도 / 방향광 산란 / 볼륨 안개.
        hashed_string _fogColorDensity{ "g_FogColorDensity" };
        hashed_string _fogHeightParams{ "g_FogHeightParams" };
        hashed_string _fogInscatter{ "g_FogInscatter" };
        hashed_string _fogVolumeParams{ "g_FogVolumeParams" };
        /// @brief 표면 변형 층의 창(`SurfaceDeformBinding::_params`) — 지면 · 지형의 정점 셰이더와 그림자 패스가 같은 값을 읽는다.
        hashed_string _surfaceDeformParams{ "g_SurfaceDeformParams" };
        /// @brief 전역 머티리얼 값(`MaterialGlobals`)입니다.
        hashed_string _materialGlobalWeather{ "g_MaterialGlobalWeather" };
        hashed_string _materialGlobalRain{ "g_MaterialGlobalRain" };
        hashed_string _materialGlobalEnvironment{ "g_MaterialGlobalEnvironment" };
        /// @brief 비 가림 맵의 뷰-투영과 (깊이 바이어스 NDC, 노멀 오프셋 m, 1 / 한 변, 켬)입니다(`FrameRainOcclusion`).
        hashed_string _rainOcclusionViewProj{ "g_RainOcclusionViewProj" };
        hashed_string _rainOcclusionParams{ "g_RainOcclusionParams" };
        /// @brief 안개 패스의 진단 보기입니다(`FrameFog::makeDebugParams`, fog.hlsl).
        hashed_string _fogDebugParams{ "g_FogDebugParams" };
        hashed_string _postProcessMaterial{ "g_PostProcessMaterial" };   ///< 후처리 머티리얼 그리기마다의 무게(x)
        hashed_string _decalFirst{ "g_DecalFirst" };                     ///< 데칼 인스턴스 그리기 묶음의 첫 원소(decal.hlsl — SV_InstanceID 에 더한다)
        hashed_string _decalCount{ "g_DecalCount" };                     ///< 데칼 구조 버퍼의 원소 수 — 셰이더가 그 밖을 읽지 않는다
        hashed_string _swDecals{ "SwDecals" };                           ///< 데칼 구조 버퍼(decal.hlsl g_SwDecals, `FrameRenderState::_decalBuffer`)
        hashed_string _lensFlareTexture{ "g_LensFlareTexture" };         ///< 렌즈 플레어 Image 구간의 텍스처 번호(MaterialTexture::sampleIndex 의 뜻, 없으면 kInvalidIndex)
        hashed_string _deferredTileCapacity{ "g_DeferredTileCapacity" }; ///< 디퍼드 조명 타일 목록의 종류 하나 칸 수(타일 그리기 — deferred_lighting.hlsl VSTile)
        hashed_string _shadowParams{ "g_ShadowParams" };
        /// @brief 방향광 캐스케이드 배치입니다 — x 수, y 칸 배율, z 경계 섞기 폭, w 거리 끝에서 옅어지기 시작하는 깊이. `ViewShadowData::_cascadeLayout`.
        hashed_string _shadowCascadeLayout{ "g_ShadowCascadeLayout" };
        /// @brief 캐스케이드마다 (배율, 이동 x, 이동 y, 깊이 끝)입니다. 이름이 넷인 까닭: 배열 멤버의 리플렉션 이름이 백엔드마다 다르다(GL 은 `[0]` 이 붙는다).
        hashed_string _arrShadowCascade[shaderslot::kShadowCascadeMax]{ hashed_string{ "g_ShadowCascade0" }, hashed_string{ "g_ShadowCascade1" },
                                                                        hashed_string{ "g_ShadowCascade2" }, hashed_string{ "g_ShadowCascade3" } };
        /// @brief 그림자 필터입니다 — x 품질(0 PCF 3x3, 1 PCSS), y 방향광 반그림자 배율, z · w 커널 반지름 상한 · 하한(텍셀). `ShadowFilter::makeShaderParams`.
        hashed_string _shadowFilter{ "g_ShadowFilter" };
        /// @brief 접촉 그림자 채널 넷의 빛 원소 번호입니다(-1 = 빈 채널). `FrameShadowData::_contactShadowLights`, contact_shadow.hlsl.
        hashed_string _contactShadowLights{ "g_ContactShadowLights" };
        hashed_string _bloomParams{ "g_BloomParams" };
        /// @brief 카메라별 색 보정입니다 — x 노출 배율(2^EV), y 채도, z 대비, w 비네트 세기. `PostProcessValues`, post_grade.hlsli.
        hashed_string _cameraGrade{ "g_CameraGrade" };
        /// @brief 카메라별 화면 틀입니다 — x 레터박스로 남길 가로/세로 비율(0 = 없음), y 지금 뷰의 가로/세로 비율, z 색각 보정 방식(화면 출력 뷰만, 렌더 텍스처는 0). post_grade.hlsli.
        hashed_string _cameraFrame{ "g_CameraFrame" };
        /// @brief 화면 출력입니다 — x 톤 곡선 어깨(SDR 1, HDR 출력에 그리는 뷰는 `FrameDisplayEncode::getToneShoulder`). display_encode.hlsli.
        hashed_string _displayOutput{ "g_DisplayOutput" };
        /// @brief 카메라별 피사계 심도입니다 — x 초점 거리(m), y 착란원 반지름 배율(px), z 반지름 상한(px), w 켬(1) · 끔(0, 끈 뷰는 패스를 기록하지 않아 셰이더는 읽지 않는다). depth_of_field.hlsl.
        hashed_string _cameraLens{ "g_CameraLens" };
        /// @brief 컬러 그레이딩 화이트 밸런스 행 셋입니다(`PostGradeConstants::_arrWhiteBalance`, post_grade.hlsli).
        hashed_string _arrGradeWhiteBalance[3]{ hashed_string{ "g_GradeWhiteBalance0" }, hashed_string{ "g_GradeWhiteBalance1" }, hashed_string{ "g_GradeWhiteBalance2" } };
        /// @brief 컬러 그레이딩 범위 상수입니다 — 범위(그림자 · 중간 · 밝은 부분)마다 채도 · 대비 · 감마 · 게인 · 오프셋(`PostGradeConstants::_arrRange` 와 같은 차례).
        /// @brief 컬러 그레이딩 범위 상수의 수입니다(`PostGradeConstants::kRangeCount * kRangeFieldCount`, FrameRecorder 가 static_assert 로 맞춘다).
        static constexpr uint32 kGradeRangeConstantCount = 15;
        hashed_string           _arrGradeRange[kGradeRangeConstantCount]{ hashed_string{ "g_GradeShadowsSaturation" }, hashed_string{ "g_GradeShadowsContrast" }, hashed_string{ "g_GradeShadowsGamma" },
                                                                          hashed_string{ "g_GradeShadowsGain" }, hashed_string{ "g_GradeShadowsOffset" }, hashed_string{ "g_GradeMidtonesSaturation" },
                                                                          hashed_string{ "g_GradeMidtonesContrast" }, hashed_string{ "g_GradeMidtonesGamma" }, hashed_string{ "g_GradeMidtonesGain" },
                                                                          hashed_string{ "g_GradeMidtonesOffset" }, hashed_string{ "g_GradeHighlightsSaturation" }, hashed_string{ "g_GradeHighlightsContrast" },
                                                                          hashed_string{ "g_GradeHighlightsGamma" }, hashed_string{ "g_GradeHighlightsGain" }, hashed_string{ "g_GradeHighlightsOffset" } };
        /// @brief 컬러 그레이딩 범위 경계 · 켬(`PostGradeConstants::_params`)과 LUT 크기 · 세기(`_lut`)입니다.
        hashed_string _gradeParams{ "g_GradeParams" };
        hashed_string _gradeLUT{ "g_GradeLUT" };
        /// @brief 렌즈 효과(색수차 세기 · 시작 반경 · 렌즈 왜곡 · 선명화)와 필름 그레인(세기 · 크기 px)입니다. post_grade.hlsli 의 `CameraGrade::applyLens` · `applyFilmGrain`.
        hashed_string _lensParams{ "g_LensParams" };
        hashed_string _grainParams{ "g_GrainParams" };
        /// @brief 블룸 렌즈(먼지 마스크 세기 · 플레어 세기 · 고스트 간격 · 헤일로 반지름)와 플레어 색입니다. post_bloom.hlsli 의 `Bloom::computeLensFlare` · `applyDirt`.
        hashed_string _bloomLens{ "g_BloomLens" };
        hashed_string _flareTint{ "g_FlareTint" };
        /// @brief 화면 공간 반사(세기 · 최대 거리 · 두께 · 최대 거칠기)입니다. screen_space_reflection.hlsl.
        hashed_string _ssrParams{ "g_SSRParams" };
        /// @brief 화면 공간 반사의 HZB(xy 지은 깊이 사각형 px, z 밉 수)입니다. screen_space_reflection.hlsl 의 hi-z trace.
        hashed_string _ssrHZBParams{ "g_SSRHZBParams" };
        hashed_string _outlineColor{ "g_OutlineColor" };
        hashed_string _outlineParams{ "g_OutlineParams" };
        hashed_string _sourceTexel{ "g_SourceTexel" };
        /// @brief 뷰 사각형 배율(xy 이번, zw 지난 프레임)입니다. binding.hlsli PassCB, `FrameRendererUtil::makeViewRectConstants`.
        hashed_string _viewRectScale{ "g_ViewRectScale" };
        /// @brief 뷰 사각형 크기(xy 픽셀)와 풀 텍셀(zw = 1 / 풀 픽셀)입니다.
        hashed_string _viewRectSize{ "g_ViewRectSize" };
        /// @brief 출력 해상도 공간의 사각형 크기(xy 픽셀)와 출력 풀 텍셀(zw)입니다 — 시간 업스케일 뒤 공간. 업스케일하지 않으면 `_viewRectSize` 와 같다.
        hashed_string _outputRectSize{ "g_OutputRectSize" };
        /// @brief 시간 업스케일(x 켬 1 · 끔 0, y 렌더 / 출력 배율)입니다. taa.hlsl.
        hashed_string _temporalUpscale{ "g_TemporalUpscale" };
        hashed_string _flags{ "g_Flags" };
        /// @brief 인스턴스 버퍼 원소 수입니다. 셰이더 InstanceData::load 가 범위를 제한합니다.
        hashed_string _swInstanceCount{ "g_SwInstanceCount" };
        /// @brief 가시 인스턴스 ID 목록의 칸 수(가시 구간의 합)입니다. PassCB g_SwVisibleSlotCount 에 대응하며, 셰이더 InstanceData::resolveID 가 슬롯 범위를 제한합니다.
        hashed_string _swVisibleSlotCount{ "g_SwVisibleSlotCount" };
        /// @brief 배치의 머티리얼 데이터 버퍼 원소 수입니다. 셰이더 SW_MATERIAL 이 클램프합니다.
        hashed_string _swMaterialCount{ "g_SwMaterialCount" };
        hashed_string _swInstances{ "SwInstances" };
        /// @brief GPU 가 변형한 정점 풀입니다. binding.hlsli 의 g_SwMorphVertices(t7)와 PassCB g_SwMorphVerticesIndex 에 대응합니다.
        hashed_string _swMorphVertices{ "SwMorphVertices" };
        /// @brief 그 풀의 원소 수입니다. PassCB g_SwMorphVertexCount 에 대응합니다.
        hashed_string _swMorphVertexCount{ "g_SwMorphVertexCount" };
        /// @brief 스킨 칸 → 결과 시작 표입니다. binding.hlsli 의 g_SwSkinResultOffsets(t14)와 PassCB g_SwSkinResultOffsetsIndex 에 대응합니다.
        hashed_string _swSkinResultOffsets{ "SwSkinResultOffsets" };
        /// @brief 그 표의 칸 수입니다. PassCB g_SwSkinResultOffsetCount 에 대응합니다.
        hashed_string _swSkinResultOffsetCount{ "g_SwSkinResultOffsetCount" };
        /// @brief 스킨 결과의 이번 벌 이동(정점)입니다. PassCB g_SwSkinResultShift 에 대응합니다(`GPUMeshMorphPool::getSkinCurrentShift`).
        hashed_string _swSkinResultShift{ "g_SwSkinResultShift" };
        /// @brief 지난 프레임 스킨 결과가 든 벌의 이동(정점, 없으면 무효)입니다. PassCB g_SwSkinPreviousShift 에 대응합니다.
        hashed_string _swSkinPreviousShift{ "g_SwSkinPreviousShift" };
        /// @brief 인스턴스마다의 지난 월드 행렬입니다. binding.hlsli 의 g_SwPrevInstanceWorlds(t15)와 PassCB g_SwPrevInstanceWorldsIndex 에 대응합니다.
        hashed_string _swPrevInstanceWorlds{ "SwPrevInstanceWorlds" };
        /// @brief 그 버퍼의 원소 수입니다. PassCB g_SwPrevInstanceWorldCount 에 대응합니다.
        hashed_string _swPrevInstanceWorldCount{ "g_SwPrevInstanceWorldCount" };
        /// @brief 컬링이 만든 가시 인스턴스 ID 목록입니다. binding.hlsli g_SwVisibleInstanceIDs 와 "SwVisibleInstanceIDs" 에 대응합니다.
        hashed_string _swVisibleInstanceIDs{ "SwVisibleInstanceIDs" };
        /// @brief 배치의 머티리얼 데이터 구조 버퍼입니다. binding.hlsli g_SwMaterials 와 "SwMaterials" 에 대응하며, 배치마다 등록합니다.
        hashed_string _swMaterials{ "SwMaterials" };
        /// @brief 씬 라이트 구조 버퍼입니다. lighting.hlsli g_SwLights 와 "SwLights" 에 대응하며, 패스당 한 번 바인딩합니다.
        hashed_string _swLights{ "SwLights" };
        /// @brief 씬 배치 테이블입니다. binding.hlsli g_SwBatches 와 "SwBatches" 에 대응하며, 패스당 한 번 바인딩합니다. 드로우는 배치 번호만 전달합니다.
        hashed_string _swBatches{ "SwBatches" };
        /// @brief 그 테이블의 원소 수입니다. PassCB g_SwBatchCount 에 대응합니다.
        hashed_string _swBatchCount{ "g_SwBatchCount" };
        /// @brief 씬 라이트 버퍼의 빛 원소 수입니다. PassCB g_SwLightCount 에 대응하며, 0이면 셰이더가 빛을 돌지 않습니다(앰비언트만).
        hashed_string _swLightCount{ "g_SwLightCount" };
        /// @brief 씬 라이트 버퍼의 전체 원소 수(빛 + 그림자 타일 원소)입니다. PassCB g_SwLightElementCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightBuffer::loadElement).
        hashed_string _swLightElementCount{ "g_SwLightElementCount" };
        /// @brief 지금 뷰의 라이트 그리드 첫 단어입니다. PassCB g_SwLightGridBase 에 대응하며, 없으면 constant::invalid_index::kUint32 입니다(셰이더는 목록 전체를 돕니다).
        hashed_string _swLightGridBase{ "g_SwLightGridBase" };
        /// @brief 라이트 그리드 버퍼의 단어 수입니다. PassCB g_SwLightGridWordCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightGrid::loadWord).
        hashed_string _swLightGridWordCount{ "g_SwLightGridWordCount" };
        /// @brief 뷰마다의 라이트 그리드 버퍼입니다. lighting.hlsli g_SwLightGrid(t12)와 "SwLightGrid" 에 대응하며, 패스당 한 번 바인딩합니다(CPU · GPU 생산자 공통).
        hashed_string _swLightGrid{ "SwLightGrid" };
        /// @brief 라이트 프로브 SH9 버퍼입니다. lighting.hlsli g_SwLightProbes(t13)와 "SwLightProbes" 에 대응하며, 볼륨이 있는 프레임에만 바인딩합니다.
        hashed_string _swLightProbes{ "SwLightProbes" };
        /// @brief 라이트 프로브 버퍼의 원소(float4) 수입니다. PassCB g_SwLightProbeElementCount 에 대응하며, 셰이더가 그 밖을 읽지 않습니다(LightProbe::loadFromXML).
        hashed_string _swLightProbeElementCount{ "g_SwLightProbeElementCount" };
        /// @brief 정점 애니메이션(VAT) 테이블입니다. binding.hlsli g_SwVertexAnimation 과 "SwVertexAnimation" 에 대응하며, 패스당 한 번 바인딩합니다.
        hashed_string _swVertexAnimation{ "SwVertexAnimation" };
        /// @brief 그 테이블의 원소 수입니다. PassCB g_SwVertexAnimationCount 에 대응합니다.
        hashed_string _swVertexAnimationCount{ "g_SwVertexAnimationCount" };
        /// @brief VAT 시계(초)입니다. PassCB g_SwVertexAnimationTime 에 대응합니다.
        hashed_string _swVertexAnimationTime{ "g_SwVertexAnimationTime" };
        /// @brief 머티리얼 애니메이션 시계(초)입니다. PassCB g_SwMaterialTime 에 대응하며, VAT 표가 없어도 패스마다 겁니다(툰 UV 애니메이션).
        hashed_string _swMaterialTime{ "g_SwMaterialTime" };
    };

    /**
     * @brief 프로세스 전역 PassConstantNames 를 반환합니다.
     * @details 함수 지역 static 이라 첫 호출 때 한 번만, 스레드 안전하게 만들어진다. 전역 정적
     *          객체로 두면 문자열 레지스트리보다 먼저 초기화될 수 있다.
     */
    inline const PassConstantNames& passConstantNames()
    {
        static const PassConstantNames s_names{};
        return s_names;
    }
} // namespace sw
