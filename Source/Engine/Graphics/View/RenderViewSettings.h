/**
 * @file Engine/Graphics/View/RenderViewSettings.h
 * @brief 뷰 하나의 설정 값(보기 모드 · 출력 사각형 · 후처리 값)입니다 — 렌더 스레드 · RHI 형식이 없는 값이라 렌더러를 짓지 않는 구성(전용 서버)의 호스트도 읽습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Graphics/Material/MaterialRenderBinding.h"
#include "Engine/Graphics/View/CameraDepthOfFieldValues.h"
#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Graphics/View/RenderViewMode.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct PostProcessMaterialDraw
     * @brief 후처리 머티리얼 그리기 하나의 값입니다(값 타입 — 게임 스레드에서 렌더 스레드로 넘어간다). 렌더 스레드는 Material 을 보지 않고 이 사본만 읽는다(`MaterialRenderBinding`).
     * @details 만드는 곳은 `PostProcessMaterialSet::resolve`(블렌더블 차례 그대로), 읽는 곳은 `FramePostProcessMaterials::prepareView`(PSO · 원소 버퍼)와 PostProcessMaterial 패스다.
     */
    struct PostProcessMaterialDraw
    {
        MaterialRenderBinding        _binding;      ///< 원소 바이트 · stride · 텍스처 번호의 불변 사본(프록시는 쓰지 않는다 — 원소 버퍼는 렌더 스레드가 머티리얼마다 둔다)
        string                       _shaderPath;   ///< 머티리얼 셰이더(PostProcess 영역 — post_process_material.hlsli)
        hashed_string                _materialPath; ///< 원소 버퍼의 열쇠(같은 머티리얼을 여러 뷰가 그려도 한 번 올린다)
        float32                      _weight{ 0.0f };
        PostProcessBlendableLocation _location{ PostProcessBlendableLocation::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderViewSettings
     * @brief 뷰 하나의 출력 사각형, 해상도 배율, 끌 기능, 컷 표시, 보기 모드를 담습니다. 주 시점과 추가 뷰가 같은 묶음을 씁니다(값 타입이며 게임 스레드에서 렌더 스레드로 넘어갑니다).
     */
    struct RenderViewSettings
    {
        float4  _screenRect{ 0.0f, 0.0f, 1.0f, 1.0f }; ///< 출력 안의 사각형(x, y, 너비, 높이. 0..1, 왼쪽 위 원점). 렌더 텍스처는 항상 전체다.
        float32 _resolutionScale{ 1.0f };              ///< 출력 크기에 곱하는 내부 해상도 — 첨부 풀의 크기(바뀌면 풀을 다시 만든다)
        float32 _rectScale{ 1.0f };                    ///< 풀 안에서 그리는 사각형의 배율(자동 렌더 배율, 0 초과 1 이하) — 바뀌어도 풀을 다시 만들지 않는다(`FrameViewTarget::updateViewRect`)
        /// @brief 이 뷰를 그리는 카메라의 후처리 값(노출 · 블룸 배율 · 비네트 · 채도 · 대비 · 레터박스)입니다. 후처리를 끈 뷰와 표면 값을 보이는 보기 모드는 읽지 않습니다.
        PostProcessValues _postProcess{};
        /**
         * @brief 이 뷰가 그릴 후처리 머티리얼입니다 — `_postProcess._listBlendable` 가운데 머티리얼 읽기를 마친 것만, 같은 차례로(`PostProcessMaterialSet::resolve`, 게임 스레드).
         * @details 자리마다의 PostProcessMaterial 패스가 자기 자리의 것을 그리고, 그 자리에 하나도 없으면 패스가 빠진다(`shouldDrawPostProcessMaterial`).
         */
        vector<PostProcessMaterialDraw> _listPostProcessMaterial;
        /// @brief 이 뷰를 그리는 카메라의 피사계 심도 렌즈 값(초점 거리 · 조리개 · 센서 높이)입니다. 피사계 심도 패스가 `g_CameraLens` 로 읽습니다(`FramePassExecutor::applyViewPassConstants`).
        CameraDepthOfFieldValues _depthOfField{};
        /**
         * @brief 원근 투영의 세로 배율 `1 / tan( fovY / 2 )` 입니다. 직교 카메라는 0 이며 피사계 심도가 없습니다.
         * @details 착란원의 화면 크기는 초점 길이에 달렸고, 초점 길이는 센서 높이와 시야각에서 나온다. 뷰-투영 행렬에서 되짚지 않고 카메라에서 받는다.
         */
        float32        _projectionScaleY{ 0.0f };
        uint8          _bShadows{ SW_TRUE };             ///< 그림자를 그린다(끄면 그림자 맵을 지우기만 하므로 모두 밝다).
        uint8          _bPostProcess{ SW_TRUE };         ///< 후처리(블룸, 외곽선, 톤맵, TAA)를 적용한다.
        uint8          _bCut{ SW_FALSE };                ///< 이번 프레임에 화면이 끊겼다(언리얼 `bCameraCut`). 시간 누적 기록(TAA · 모션 블러 · 자동 노출)을 버린다(`FrameViewTarget::_bHistoryReset`).
        RenderViewMode _viewMode{ RenderViewMode::Lit }; ///< 이 뷰의 보기 모드(에디터 씬 뷰만 바꾸고, 게임 뷰는 Lit 이다).
        /**
         * @brief 정수 업스케일 배율입니다(0 = 끔). N 이면 내부 해상도가 사각형 픽셀 / N(내림)이고 Present 가 점 샘플로 N 배 키워 사각형 가운데에 놓는다.
         * @details 픽셀 아트의 Upscale Render Texture(유니티 Pixel Perfect Camera) — 낮은 해상도에서 그려 자산 픽셀이 화면 픽셀 N × N 으로 고르게 커진다.
         *          해상도 배율(`_resolutionScale`)은 보지 않는다. 주 시점은 남는 띠를 검게 지우고, 화면 사각형 뷰는 아래 그림을 남긴다.
         */
        uint8 _upscaleZoom{ 0 };

        /** @brief 사각형이 출력 전체인지 나타냅니다. */
        bool isFullRect() const { return _screenRect._x <= 0.0f && _screenRect._y <= 0.0f && _screenRect._z >= 1.0f && _screenRect._w >= 1.0f; }
        /** @brief 효과 후처리(블룸, 외곽선, SSAO, TAA)를 적용하는지 나타냅니다. 끈 뷰와 표면 값을 보이는 보기 모드는 원본을 넘깁니다. */
        bool usesPostProcess() const { return _bPostProcess == SW_TRUE && RenderViewModeUtil::getInfo( _viewMode )._bShowsBuffer == false; }
        /** @brief 톤맵을 적용하는지 나타냅니다. 표면 값을 보이는 보기 모드(Normals, Depth, Overdraw)는 색을 바꾸지 않습니다. */
        bool usesTonemap() const { return RenderViewModeUtil::getInfo( _viewMode )._bShowsBuffer == false; }
        /** @brief @p location 자리에 그릴 후처리 머티리얼이 있는지 나타냅니다. 후처리를 끈 뷰는 없다고 답한다(효과 패스와 같은 규칙). */
        bool shouldDrawPostProcessMaterial( PostProcessBlendableLocation location ) const
        {
            if ( usesPostProcess() == false )
                return false;
            for ( const PostProcessMaterialDraw& draw : _listPostProcessMaterial )
            {
                if ( draw._location == location && draw._weight > 0.0f )
                    return true;
            }
            return false;
        }
    };
} // namespace sw
