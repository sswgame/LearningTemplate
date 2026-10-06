/**
 * @file PixelPerfectCameraComponent.h
 * @brief 픽셀 아트용 카메라 — 정수 배율 · 직교 높이 · 화면 픽셀 격자 스냅 · 레터박스입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/SpriteInstanceBatch.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct PixelPerfectLayout
     * @brief 뷰포트 하나에서 픽셀 퍼펙트 카메라가 고른 값입니다(`PixelPerfectCameraComponent::computeLayout`).
     */
    struct PixelPerfectLayout
    {
        int32   _zoom{ 1 };               ///< 자산 픽셀 하나가 차지하는 화면 픽셀 수(정수, 1 이상)
        float32 _orthoHeight{ 1.0f };     ///< 카메라의 직교 높이(월드) — 뷰포트 높이 / (배율 × PPU)
        float32 _screenPixelUnit{ 0.0f }; ///< 화면 픽셀 하나의 월드 길이 = 1 / (배율 × PPU). 그리는 눈이 이 격자에 붙는다
        float32 _assetPixelUnit{ 0.0f };  ///< 자산 픽셀 하나의 월드 길이 = 1 / PPU. 스프라이트 원점이 이 격자에 붙는다(픽셀 스냅)
        int32   _barWidthPixels{ 0 };     ///< 기준 해상도 밖 좌우 띠 하나의 픽셀 폭(가로 자르기가 꺼져 있으면 0)
        int32   _barHeightPixels{ 0 };    ///< 기준 해상도 밖 위아래 띠 하나의 픽셀 높이(세로 자르기가 꺼져 있으면 0)
    };
} // namespace sw

namespace sw
{
    /**
     * @class PixelPerfectCameraComponent
     * @brief 같은 오브젝트의 카메라를 픽셀 아트용으로 맞춥니다 — 유니티 Pixel Perfect Camera(2D) · Godot 의 정수 배율 스트레치 + 2D 스냅 자리입니다.
     * @details 프레임마다(틱 그룹 PostPhysics — 카메라를 따라가게 하는 컴포넌트보다 늦고 시차 레이어보다 이르게):
     *          (1) 뷰포트 픽셀 크기(`CameraRegistry::getViewportWidth/Height`, 엔진 루프가 틱 전에 적는다)에서 **정수 배율**
     *              `max(1, min(⌊폭/기준폭⌋, ⌊높이/기준높이⌋))` 을 고르고, 직교 높이를 `뷰포트 높이 / (배율 × PPU)` 로 둔다 — 자산 픽셀 하나가
     *              늘 화면 픽셀 배율×배율 칸이다.
     *          (2) 그리는 눈을 화면 픽셀 격자(1 / (배율 × PPU))에 붙인다 — 트랜스폼은 두고 `CameraComponent::setViewOffset` 만 바꾼다(감쇠로 따라가는
     *              카메라가 반 픽셀 아래의 움직임을 잃지 않게).
     *          (3) 픽셀 스냅이 켜져 있으면 **자산 픽셀** 격자(1 / PPU)를 카메라 등록부와 씬의 모든 메시에 알린다 — sprite2d.hlsl 이 스프라이트
     *              원점을 그 격자에 붙여(`GpuSpriteInstanceData::_pixelSnap`) 배율 4 에서도 스프라이트가 화면 픽셀 1 칸이 아니라 자산 픽셀 1 칸(화면 4 칸)씩
     *              움직인다 — 두 스프라이트의 아트 픽셀이 어긋나지 않는다(유니티 Pixel Snapping 과 같은 격자). 화면 픽셀 격자로 붙이면 래스터화가
     *              이미 하는 일이라 아무것도 바뀌지 않는다. 단위가 바뀔 때만 알리고, 틱 뒤로 미룬다(다른 오브젝트의 틱과 겹치지 않게).
     *          (4) 자르기(`_bCropX` · `_bCropY`)가 켜져 있으면 기준 해상도 × 배율 밖을 검은 띠로 덮는다(정렬 레이어 `_barSortingLayer` 의 맨 위).
     *
     *          빠진 것: 유니티의 Upscale Render Texture(기준 해상도로 그린 뒤 키우기 — 렌더러에 저해상도 타깃이 필요하다), Stretch Fill.
     *          카메라는 회전하지 않는 2D 카메라(+Z 를 봄)를 가정합니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Pixel Perfect Camera", Tooltip = "Integer zoom, pixel-grid snapping and letterboxing for pixel art" )
    class SW_API PixelPerfectCameraComponent : public Component
    {
    public:
        REFLECT_BODY();
        PixelPerfectCameraComponent();
        virtual ~PixelPerfectCameraComponent() override = default;

        void onBeginPlay() override;
        /** @brief 스냅 단위 · 눈 오프셋 · 띠를 거둡니다. */
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;
        /** @brief 켜고 끄면(`_bActive`) 띠 배치를 더티로 — 빌더가 다시 본다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트를 켜고 끄면 띠 배치를 더티로 — 빌더가 다시 본다. */
        void onOwnerActiveInHierarchyChanged() override;

        /**
         * @brief 뷰포트 하나에서 배율 · 직교 높이 · 스냅 단위 · 띠를 고릅니다(순수 함수 — 시험이 직접 부릅니다).
         * @details 기준 해상도나 PPU 가 0 이하이거나 뷰포트가 0 이면 배율 1 · 띠 0 입니다. 뷰포트가 기준보다 작으면 배율은 1 이고 띠는 0 입니다(잘리지 않는다).
         */
        static PixelPerfectLayout computeLayout( const float2& referenceResolution, float32 pixelsPerUnit, uint32 viewportWidth, uint32 viewportHeight,
                                                 bool bCropX, bool bCropY );
        /** @brief 값을 단위 격자의 가장 가까운 점으로 붙입니다(반은 위로). 단위가 0 이하면 그대로입니다. */
        static float32 snapToGrid( float32 value, float32 unit );

        /** @brief 뷰포트 크기를 주고 카메라를 맞춥니다(틱이 카메라 등록부의 크기로 부릅니다. 시험은 직접 부릅니다). 마지막 배치를 돌려줍니다. */
        PixelPerfectLayout applyToCamera( uint32 viewportWidth, uint32 viewportHeight );
        /** @brief 마지막으로 고른 배치입니다. */
        const PixelPerfectLayout& getLayout() const { return _layout; }

        float32       getPixelsPerUnit() const { return _pixelsPerUnit; }
        void          setPixelsPerUnit( float32 pixelsPerUnit ) { _pixelsPerUnit = pixelsPerUnit; }
        const float2& getReferenceResolution() const { return _referenceResolution; }
        void          setReferenceResolution( const float2& resolution ) { _referenceResolution = resolution; }
        void          setCrop( bool bCropX, bool bCropY );
        void          setPixelSnapping( bool bPixelSnapping ) { _bPixelSnapping = bPixelSnapping; }

    private:
        /** @brief 스냅 단위를 등록부와 씬의 메시에 알립니다. 값이 그대로면 아무것도 하지 않습니다. 틱 중이면 틱 뒤로 미룹니다. */
        void publishSnapUnit( float32 unit );
        /** @brief 레터박스 띠 넷을 눈 둘레에 놓습니다(자르지 않는 축의 띠는 숨깁니다). */
        void layoutBars( const float3& eye, uint32 viewportWidth, uint32 viewportHeight );

        PROPERTY( Category = "Pixel Perfect", DisplayName = "Pixels Per Unit", Tooltip = "Asset pixels in one world unit", Min = 1.0 )
        float32 _pixelsPerUnit;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Reference Resolution", Tooltip = "Resolution the art is authored for (pixels)", Min = 1.0 )
        float2 _referenceResolution;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Bar Sorting Layer", Tooltip = "Sorting layer of the letterbox bars (render2d.xml)" )
        hashed_string _barSortingLayer;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Crop X", Tooltip = "Cover the area outside the reference width with bars (pillarbox)" )
        bool _bCropX;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Crop Y", Tooltip = "Cover the area outside the reference height with bars (letterbox)" )
        bool _bCropY;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Pixel Snapping", Tooltip = "Snap sprite origins to the screen pixel grid" )
        bool                _bPixelSnapping;
        float32             _publishedSnapUnit; ///< 마지막으로 알린 스냅 단위(0 = 끔). 저장하지 않습니다
        PixelPerfectLayout  _layout;            ///< 마지막 배치. 저장하지 않습니다
        SpriteInstanceBatch _barBatch;          ///< 레터박스 띠 넷(좌 · 우 · 아래 · 위). 저장하지 않습니다
    };
} // namespace sw
