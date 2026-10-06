/**
 * @file EngineDefaultAssets.h
 * @brief enginedefaultassets.xml 에서 읽는 엔진 셸 경로입니다(Scene / App / FrameRenderer / RHI).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) EngineDefaultAssets — 씬 폴백 머티리얼 · 셸 InputMap · 파이프라인 · 셰이더 폴백
    //    게임플레이 경로는 GameSettings, 에디터 도구 경로는 EditorToolDefaults
    // ------------------------------------------------------------------------------
    /**
     * @brief enginedefaultassets.xml 의 엔진 셸 경로입니다.
     * @details 읽기는 `XmlSerializer` 가 PROPERTY 그래프로 합니다. 필드를 하나 추가하면 읽기가 저절로
     *          따라옵니다(필드마다 손으로 읽으면 한 줄을 빠뜨릴 때 값이 조용히 기본값으로 남습니다).
     */
    REFLECT()
    struct SW_API EngineDefaultAssets
    {
        REFLECT_BODY();
        PROPERTY()
        string _defaultMaterial{ "engine/materials/defaultmaterial.material" }; ///< 씬 폴백 머티리얼
        PROPERTY()
        string _shellInputMap{ "engine/input/default.input.xml" }; ///< App 셸 InputMap
        PROPERTY()
        string _userSettingsSchema{ "engine/settings/engine.settings.xml" }; ///< 플레이어 옵션 메뉴의 엔진 설정 스키마(`UserSettingsManager`)
        PROPERTY()
        string _telemetrySchema{ "engine/telemetry/engine.telemetry.xml" }; ///< 엔진 텔레메트리 사건 스키마(`TelemetryService`)
        PROPERTY()
        string _cultureTable{ "engine/localization/engine.cultures.json" }; ///< 문화권 표 — 복수형 규칙 · 숫자 · 날짜 형식 · 쓰기 방향 · 글꼴 대체(`LocalizationManager`)
        PROPERTY()
        string _localizationProject{ "engine/localization/engine.locproject.json" }; ///< 엔진 문자열(설정 메뉴 등)의 로컬라이제이션 프로젝트
        PROPERTY()
        string _fontCatalog{ "engine/fonts/fontcatalog.xml" }; ///< 글꼴 카탈로그 — 저장소 글꼴 가족 · 시스템 글꼴 가족(`FontSystem`)

        PROPERTY()
        string _defaultForwardPipeline{ "engine/pipeline/forwardpipeline.xml" }; ///< 포워드 렌더 파이프라인(프레임 그래프)
        PROPERTY()
        string _defaultDeferredPipeline{ "engine/pipeline/deferredpipeline.xml" }; ///< 디퍼드 렌더 파이프라인
        PROPERTY()
        string _defaultRenderPass{ "engine/renderpass/defaultrenderpass.xml" }; ///< 기본 렌더 패스 바인드 틀

        PROPERTY()
        string _shaderShadowDepth{ "engine/shaders/shadowdepth.hlsl" }; ///< 그림자 깊이 패스
        PROPERTY()
        string _shaderForwardLit{ "engine/shaders/forwardlit.hlsl" }; ///< 포워드 조명
        PROPERTY()
        string _shaderGBuffer{ "engine/shaders/gbuffer.hlsl" }; ///< 디퍼드 G 버퍼 채우기
        PROPERTY()
        string _shaderDeferredLighting{ "engine/shaders/deferredlighting.hlsl" }; ///< 디퍼드 조명
        PROPERTY()
        string _shaderPostBloom{ "engine/shaders/postbloom.hlsl" }; ///< 후처리 블룸
        PROPERTY()
        string _shaderPostOutline{ "engine/shaders/postoutline.hlsl" }; ///< 후처리 외곽선
        PROPERTY()
        string _shaderFullscreenBlit{ "engine/shaders/fullscreenblit.hlsl" }; ///< 전체 화면 복사
        PROPERTY()
        string _shaderGpuCull{ "engine/shaders/gpucull.hlsl" }; ///< GPU 컬링 · 드로우 커맨드 생성(컴퓨트)
        PROPERTY()
        string _shaderInstanceAnim{ "engine/shaders/instanceanim.hlsl" }; ///< 인스턴스 애니메이션(컴퓨트)
        PROPERTY()
        string _shaderMeshMorph{ "engine/shaders/meshmorph.hlsl" }; ///< 모프 타깃(컴퓨트)
        PROPERTY()
        string _shaderMeshSkin{ "engine/shaders/meshskin.hlsl" }; ///< 스키닝(컴퓨트)
        PROPERTY()
        string _shaderInstanceSort{ "engine/shaders/instancesort.hlsl" }; ///< 투명 인스턴스 바이토닉 정렬(컴퓨트)
        PROPERTY()
        string _shaderFullscreenTriangle{ "engine/shaders/fullscreentriangle.hlsl" }; ///< 전체 화면 삼각형 정점 셰이더
        PROPERTY()
        string _shaderSsao{ "engine/shaders/ssao.hlsl" }; ///< SSAO
        PROPERTY()
        string _shaderTaa{ "engine/shaders/taa.hlsl" }; ///< TAA
        PROPERTY()
        string _shaderTonemap{ "engine/shaders/tonemap.hlsl" }; ///< 톤 매핑
        PROPERTY()
        string _shaderToon{ "engine/shaders/toon.hlsl" }; ///< 셀 셰이딩 머티리얼 셰이더 — 메시 외곽선 패스의 기본 셰이더이기도 하다
        PROPERTY()
        string _shaderCanvas{ "engine/shaders/canvas.hlsl" }; ///< 화면 2D(UI · 월드 글자) 사각형 — 사각형 하나 = 인스턴스 하나, SDF 모양 · 글리프

        /** @brief 리소스 경로(XML)에서 엔진 테이블을 로드합니다. 빈 경로면 path::kEngineDefaultAssets 를 씁니다. */

        [[nodiscard]] bool loadFromResource( string_view assetRelativePath = {} );
    };
} // namespace sw
