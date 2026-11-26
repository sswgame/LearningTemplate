#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Visual/2D/SortingLayerNameValidation.h"
#include "Engine/Visual/2D/SpriteInstanceBatch.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    class ValidationContext;

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

    class CameraComponent;

    /**
     * @class PixelPerfectCameraComponent
     * @brief 같은 오브젝트의 카메라를 픽셀 아트용으로 맞춥니다 — 유니티 Pixel Perfect Camera(2D) · Godot 의 정수 배율 스트레치 + 2D 스냅 자리입니다.
     * @details 프레임마다(틱 그룹 PostPhysics — 카메라를 따라가게 하는 컴포넌트보다 늦고 시차 레이어보다 이르게):
     *          (1) 뷰포트 픽셀 크기(`CameraRegistry::getViewportWidth/Height`, 엔진 루프가 틱 전에 적는다)에서 정수 배율
     *              `max(1, min(⌊폭/기준폭⌋, ⌊높이/기준높이⌋))` 을 고르고, 직교 높이를 `뷰포트 높이 / (배율 × PPU)` 로 둔다 — 자산 픽셀 하나가
     *              늘 화면 픽셀 배율×배율 칸이다.
     *          (2) 그리는 눈을 화면 픽셀 격자(1 / (배율 × PPU))에 붙인다 — 트랜스폼은 두고 `CameraComponent::setViewOffset` 만 바꾼다(감쇠로 따라가는
     *              카메라가 반 픽셀 아래의 움직임을 잃지 않게).
     *          (3) 픽셀 스냅이 켜져 있으면 자산 픽셀 격자(1 / PPU)를 카메라 레지스트리와 씬의 모든 메시에 알린다 — sprite_2d.hlsl 이 스프라이트
     *              원점을 그 격자에 붙여(`GPUSpriteInstanceData::_pixelSnap`) 배율 4 에서도 스프라이트가 화면 픽셀 1 칸이 아니라 자산 픽셀 1 칸(화면 4 칸)씩
     *              움직인다 — 두 스프라이트의 아트 픽셀이 어긋나지 않는다(유니티 Pixel Snapping 과 같은 격자). 화면 픽셀 격자로 붙이면 래스터화가
     *              이미 하는 일이라 아무것도 바뀌지 않는다. 단위가 바뀔 때만 알리고, 틱 뒤로 미룬다(다른 오브젝트의 틱과 겹치지 않게).
     *          (4) 자르기(`_bCropX` · `_bCropY`)가 켜져 있으면 기준 해상도 × 배율 밖을 검은 띠로 덮는다(정렬 레이어 `_barSortingLayer` 의 맨 위).
     *
     *          (5) Upscale Render Texture(`_bUpscaleRenderTexture`)면 카메라 출력의 정수 업스케일 배율을 고른 배율로 둔다 — 렌더러가 뷰포트 / 배율로 그리고
     *              점 샘플로 키워 가운데에 놓는다(`RenderViewSettings::_upscaleZoom`). 회전하거나 반 픽셀에 걸린 스프라이트도 아트 픽셀 격자에 남는다.
     *
     *          빠진 것: 유니티의 Stretch Fill(정수가 아닌 배율로 화면 채우기).
     *          카메라는 회전하지 않는 2D 카메라(+Z 를 봄)를 가정합니다.
     */
    REFLECT( Category = "Rendering 2D", DisplayName = "Pixel Perfect Camera", Tooltip = "Integer zoom, pixel-grid snapping and letterboxing for pixel art" )
    class PixelPerfectCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostPhysics; ///< 카메라를 따라가게 하는 컴포넌트(앞 그룹) 뒤, 시차 레이어(PostUpdate) 앞.
        PixelPerfectCameraComponent();
        virtual ~PixelPerfectCameraComponent() override = default;

        void onBeginPlay() override;
        /** @brief 스냅 단위 · 눈 오프셋 · 띠를 거둡니다. */
        void onEndPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 켜고 끄면(`_bActive`) 띠 배치를 더티로 — 빌더가 다시 본다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트를 켜고 끄면 띠 배치를 더티로 — 빌더가 다시 본다. */
        void onOwnerActiveInHierarchyChanged() override;

        /**
         * @brief 뷰포트 하나에서 배율 · 직교 높이 · 스냅 단위 · 띠를 고릅니다(순수 함수 — 테스트가 직접 부릅니다).
         * @details 기준 해상도나 PPU 가 0 이하이거나 뷰포트가 0 이면 배율 1 · 띠 0 입니다. 뷰포트가 기준보다 작으면 배율은 1 이고 띠는 0 입니다(잘리지 않는다).
         */
        static PixelPerfectLayout computeLayout( const float2& referenceResolution, float32 pixelsPerUnit, uint32 viewportWidth, uint32 viewportHeight,
                                                 bool bCropX, bool bCropY );
        /** @brief 값을 단위 격자의 가장 가까운 점으로 붙입니다(반은 위로). 단위가 0 이하면 그대로입니다. */
        static float32 snapToGrid( float32 value, float32 unit );

        /** @brief 뷰포트 크기를 주고 카메라를 맞춥니다(틱이 카메라 레지스트리의 크기로 부릅니다. 테스트는 직접 부릅니다). 마지막 배치를 돌려줍니다. */
        PixelPerfectLayout applyToCamera( uint32 viewportWidth, uint32 viewportHeight );
        /** @brief 마지막으로 고른 배치입니다. */
        const PixelPerfectLayout& getLayout() const { return _layout; }

        float32       getPixelsPerUnit() const { return _pixelsPerUnit; }
        void          setPixelsPerUnit( float32 pixelsPerUnit ) { _pixelsPerUnit = pixelsPerUnit; }
        const float2& getReferenceResolution() const { return _referenceResolution; }
        void          setReferenceResolution( const float2& resolution ) { _referenceResolution = resolution; }
        void          setCrop( bool bCropX, bool bCropY );
        void          setPixelSnapping( bool bPixelSnapping ) { _bPixelSnapping = bPixelSnapping; }
        /** @brief Upscale Render Texture 를 켭니다 — 카메라 출력의 정수 업스케일 배율(`CameraRenderOutput::_upscaleZoom`)을 고른 배율로 둡니다. */
        void setUpscaleRenderTexture( bool bUpscale ) { _bUpscaleRenderTexture = bUpscale; }
        bool isUpscaleRenderTexture() const { return _bUpscaleRenderTexture; }

    private:
        /** @brief `_barSortingLayer` 가 정렬 레이어 표에 있는지 봅니다(`SortingLayerNameValidation`). */
        void validateBarSortingLayer( ValidationContext& context ) const;

    private:
        /** @brief 스냅 단위를 레지스트리와 씬의 메시에 알립니다. 값이 그대로면 아무것도 하지 않습니다. 틱 중이면 틱 뒤로 미룹니다. */
        void publishSnapUnit( float32 unit );
        /** @brief 레터박스 띠 넷을 눈 둘레에 놓습니다(자르지 않는 축의 띠는 숨깁니다). */
        void layoutBars( const float3& eye, uint32 viewportWidth, uint32 viewportHeight );

        PROPERTY( Category = "Pixel Perfect", DisplayName = "Pixels Per Unit", Tooltip = "Asset pixels in one world unit", Min = 1.0 )
        float32 _pixelsPerUnit;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Reference Resolution", Tooltip = "Resolution the art is authored for (pixels)", Min = 1.0 )
        float2 _referenceResolution;
        PROPERTY( DefaultName, Category = "Pixel Perfect", DisplayName = "Bar Sorting Layer", Tooltip = "Sorting layer of the letterbox bars (render2d.xml)", Validate = validateBarSortingLayer )
        hashed_string _barSortingLayer{ "Overlay" };
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Crop X", Tooltip = "Cover the area outside the reference width with bars (pillarbox)" )
        bool _bCropX;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Crop Y", Tooltip = "Cover the area outside the reference height with bars (letterbox)" )
        bool _bCropY;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Pixel Snapping", Tooltip = "Snap sprite origins to the screen pixel grid" )
        bool _bPixelSnapping;
        PROPERTY( Category = "Pixel Perfect", DisplayName = "Upscale Render Texture",
                  Tooltip = "Render at viewport / zoom and enlarge with point sampling, so rotated or sub-pixel sprites stay on the art pixel grid" )
        bool                             _bUpscaleRenderTexture;
        SW_TRANSIENT float32             _publishedSnapUnit; ///< 마지막으로 알린 스냅 단위(0 = 끔). 저장하지 않습니다
        SW_TRANSIENT PixelPerfectLayout  _layout;            ///< 마지막 배치. 저장하지 않습니다
        SW_TRANSIENT SpriteInstanceBatch _barBatch;          ///< 레터박스 띠 넷(좌 · 우 · 아래 · 위). 저장하지 않습니다
    };

    namespace
    {
        struct PixelPerfectCameraComponentInternal
        {
            static constexpr uint32 kBarCount = 4; ///< 좌 · 우 · 아래 · 위
            /** @brief 띠를 눈 앞에 놓는 거리(근평면의 배수). 근평면 바로 뒤라 장면의 어떤 스프라이트보다 앞이다. */
            static constexpr float32 kBarNearPlaneScale = 2.0f;
            /** @brief 띠를 바깥쪽으로 더 덮는 화면 픽셀 수(가장자리 반올림 틈을 막는다). */
            static constexpr float32 kBarOverlapPixels = 2.0f;

            /** @brief 카메라 출력의 정수 업스케일 배율을 바꿉니다. 같으면 아무것도 하지 않습니다. */
            static void applyUpscaleZoom( CameraComponent& camera, uint32 upscaleZoom )
            {
                if ( camera.getRenderOutput()._upscaleZoom == upscaleZoom )
                    return;
                CameraRenderOutput output = camera.getRenderOutput();
                output._upscaleZoom       = upscaleZoom;
                camera.setRenderOutput( output );
            }
        };
    } // namespace

    PixelPerfectCameraComponent::PixelPerfectCameraComponent()
        : _pixelsPerUnit{ 16.0f }
        , _referenceResolution{ 320.0f, 180.0f }
        , _bCropX{ false }
        , _bCropY{ false }
        , _bPixelSnapping{ true }
        , _bUpscaleRenderTexture{ false }
        , _publishedSnapUnit{ 0.0f }
        , _layout{}
        , _barBatch{}
    {
    }

    void PixelPerfectCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _barBatch.setSorting( _barSortingLayer, Render2DSettings::kMaxOrderInLayer );
            _barBatch.setOwnerComponent( this );
            if ( _barBatch.initialize( *pManager, {}, PixelPerfectCameraComponentInternal::kBarCount ) == false )
                SW_LOG_WARNING_ID( "PixelPerfectCamera.PixelPerfectLetterboxBars", "Pixel perfect letterbox bars could not be created" );
        }
    }

    void PixelPerfectCameraComponent::onEndPlay()
    {
        publishSnapUnit( 0.0f );
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = ( pOwner != nullptr ) ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
        {
            pCamera->setViewOffset( float3{ 0.0f, 0.0f, 0.0f } );
            if ( _bUpscaleRenderTexture )
                PixelPerfectCameraComponentInternal::applyUpscaleZoom( *pCamera, 0u );
        }
        _barBatch.shutdown();
        Component::onEndPlay();
    }

    void PixelPerfectCameraComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( propertyName == getActivePropertyName() )
            _barBatch.markAllEntriesDirty();
    }

    void PixelPerfectCameraComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        _barBatch.markAllEntriesDirty();
    }

    void PixelPerfectCameraComponent::onTick( TickContext& context )
    {
        const CameraRegistry& cameras = context.getManager().getCameraRegistry();
        (void)applyToCamera( cameras.getViewportWidth(), cameras.getViewportHeight() ); // 돌려주는 배치는 _layout 에도 남는다 — 여기서는 카메라에 거는 것만 필요하다
    }

    PixelPerfectLayout PixelPerfectCameraComponent::computeLayout( const float2& referenceResolution, float32 pixelsPerUnit, uint32 viewportWidth,
                                                                   uint32 viewportHeight, bool bCropX, bool bCropY )
    {
        PixelPerfectLayout layout{};
        const float32      ppu                 = MathUtil::max( pixelsPerUnit, 1e-3f );
        const bool         bInvalidLayoutInput = viewportWidth == 0 || viewportHeight == 0 || referenceResolution._x < 1.0f || referenceResolution._y < 1.0f;
        if ( bInvalidLayoutInput )
        {
            layout._orthoHeight     = ( viewportHeight > 0 ) ? static_cast<float32>( viewportHeight ) / ppu : referenceResolution._y / ppu;
            layout._screenPixelUnit = 1.0f / ppu;
            layout._assetPixelUnit  = 1.0f / ppu;
            return layout;
        }
        const int32 zoomX  = static_cast<int32>( static_cast<float32>( viewportWidth ) / referenceResolution._x );
        const int32 zoomY  = static_cast<int32>( static_cast<float32>( viewportHeight ) / referenceResolution._y );
        layout._zoom       = MathUtil::max( 1, MathUtil::min( zoomX, zoomY ) );
        const float32 zoom = static_cast<float32>( layout._zoom );
        // 자산 픽셀 하나 = 화면 픽셀 zoom × zoom. 직교 높이는 뷰포트 전체가 보이는 월드 높이다.
        layout._orthoHeight     = static_cast<float32>( viewportHeight ) / ( zoom * ppu );
        layout._screenPixelUnit = 1.0f / ( zoom * ppu );
        layout._assetPixelUnit  = 1.0f / ppu;
        // 띠는 기준 해상도 × 배율 밖이다. 뷰포트가 기준보다 작으면(배율 1 로 묶인 경우) 덮을 것이 없다.
        const int32 keptWidth  = static_cast<int32>( referenceResolution._x ) * layout._zoom;
        const int32 keptHeight = static_cast<int32>( referenceResolution._y ) * layout._zoom;
        if ( bCropX )
            layout._barWidthPixels = MathUtil::max( 0, ( static_cast<int32>( viewportWidth ) - keptWidth ) / 2 );
        if ( bCropY )
            layout._barHeightPixels = MathUtil::max( 0, ( static_cast<int32>( viewportHeight ) - keptHeight ) / 2 );
        return layout;
    }

    float32 PixelPerfectCameraComponent::snapToGrid( float32 value, float32 unit )
    {
        if ( unit <= 0.0f )
            return value;
        return MathUtil::floor( value / unit + 0.5f ) * unit;
    }

    void PixelPerfectCameraComponent::setCrop( bool bCropX, bool bCropY )
    {
        _bCropX = bCropX;
        _bCropY = bCropY;
    }

    PixelPerfectLayout PixelPerfectCameraComponent::applyToCamera( uint32 viewportWidth, uint32 viewportHeight )
    {
        _layout                  = computeLayout( _referenceResolution, _pixelsPerUnit, viewportWidth, viewportHeight, _bCropX, _bCropY );
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = ( pOwner != nullptr ) ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
            return _layout;

        // Upscale Render Texture 면 렌더러가 뷰포트 / 배율(내림)로 그리고 점 샘플로 키운다 — 렌더 픽셀 하나가 자산 픽셀 하나라 직교 높이는 그 높이 / PPU 이고,
        // 눈은 자산 픽셀 격자에 붙인다(화면 픽셀로 붙이면 렌더 픽셀 사이에 걸려 장면이 반 픽셀씩 흔들린다). 유니티 Pixel Perfect Camera 의 Upscale Render Texture 와 같다.
        const uint32  upscaleZoom = _bUpscaleRenderTexture ? static_cast<uint32>( _layout._zoom ) : 0u;
        const float32 orthoHeight = upscaleZoom > 0 ? static_cast<float32>( MathUtil::max( 1u, viewportHeight / upscaleZoom ) ) / MathUtil::max( _pixelsPerUnit, 1e-3f )
                                                    : _layout._orthoHeight;
        const float32 eyeUnit     = upscaleZoom > 0 ? _layout._assetPixelUnit : _layout._screenPixelUnit;
        pCamera->setOrthographic( true );
        pCamera->setOrthoHeight( orthoHeight );
        PixelPerfectCameraComponentInternal::applyUpscaleZoom( *pCamera, upscaleZoom );
        // 그리는 눈만 격자에 붙인다(트랜스폼은 그대로 — 감쇠로 따라가는 카메라가 반 픽셀 아래의 움직임을 잃지 않게).
        const float3 world = pCamera->getWorldPosition();
        const float3 offset{ snapToGrid( world._x, eyeUnit ) - world._x, snapToGrid( world._y, eyeUnit ) - world._y, 0.0f };
        pCamera->setViewOffset( offset );

        publishSnapUnit( _bPixelSnapping ? _layout._assetPixelUnit : 0.0f );
        layoutBars( pCamera->getCameraPosition(), viewportWidth, viewportHeight );
        return _layout;
    }

    void PixelPerfectCameraComponent::publishSnapUnit( float32 unit )
    {
        if ( unit == _publishedSnapUnit )
            return;
        _publishedSnapUnit                = unit;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 다른 오브젝트의 메시를 고치므로 틱 뒤로 미룬다(그 오브젝트의 틱인 애니메이터의 프레임 넘기기와 겹치지 않게).
        pManager->executeOrDeferPostTickOnScene( [unit]( GameObjectManager& sceneManager )
        {
            sceneManager.getCameraRegistry().setPixelSnapUnit( unit );
            // 레지스트리는 코어 기반만 알고, 들어오는 타입은 메시 계열뿐이다(`PrimitiveComponent.h` 의 불변식).
            for ( PrimitiveComponent* pPrimitive : sceneManager.getPrimitiveRegistry().getAll() )
            {
                if ( pPrimitive != nullptr )
                    static_cast<MeshComponent*>( pPrimitive )->setPixelSnapUnit( unit );
            }
        } );
    }

    void PixelPerfectCameraComponent::layoutBars( const float3& eye, uint32 viewportWidth, uint32 viewportHeight )
    {
        using Internal = PixelPerfectCameraComponentInternal;
        if ( _barBatch.isInitialized() == false )
            return;
        const bool bShowX = _layout._barWidthPixels > 0;
        const bool bShowY = _layout._barHeightPixels > 0;
        _barBatch.setEntryVisible( 0, bShowX );
        _barBatch.setEntryVisible( 1, bShowX );
        _barBatch.setEntryVisible( 2, bShowY );
        _barBatch.setEntryVisible( 3, bShowY );
        if ( bShowX == false && bShowY == false )
            return;

        const GameObject*      pOwner     = getOwner();
        const CameraComponent* pCamera    = pOwner->getComponent<CameraComponent>();
        const float32          unit       = _layout._screenPixelUnit;
        const float32          halfWidth  = 0.5f * static_cast<float32>( viewportWidth ) * unit;
        const float32          halfHeight = 0.5f * static_cast<float32>( viewportHeight ) * unit;
        const float32          overlap    = Internal::kBarOverlapPixels * unit;
        const float32          barZ       = eye._z + pCamera->getNearPlane() * Internal::kBarNearPlaneScale;
        const float4           black{ 0.0f, 0.0f, 0.0f, 1.0f };
        const float4           fullUV{ 0.0f, 0.0f, 1.0f, 1.0f };
        if ( bShowX )
        {
            // 띠 폭은 정수 픽셀이다 — 안쪽 가장자리가 기준 해상도 × 배율의 가장자리와 같은 화면 픽셀에 떨어진다.
            const float32 barWidth = static_cast<float32>( _layout._barWidthPixels ) * unit + overlap;
            const float32 offsetX  = halfWidth + overlap - barWidth * 0.5f;
            _barBatch.setEntry( 0, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x - offsetX, eye._y, barZ }, barWidth, 2.0f * ( halfHeight + overlap ) ), fullUV,
                                black );
            _barBatch.setEntry( 1, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x + offsetX, eye._y, barZ }, barWidth, 2.0f * ( halfHeight + overlap ) ), fullUV,
                                black );
        }
        if ( bShowY )
        {
            const float32 barHeight = static_cast<float32>( _layout._barHeightPixels ) * unit + overlap;
            const float32 offsetY   = halfHeight + overlap - barHeight * 0.5f;
            _barBatch.setEntry( 2, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x, eye._y - offsetY, barZ }, 2.0f * ( halfWidth + overlap ), barHeight ), fullUV,
                                black );
            _barBatch.setEntry( 3, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x, eye._y + offsetY, barZ }, 2.0f * ( halfWidth + overlap ), barHeight ), fullUV,
                                black );
        }
    }
} // namespace sw

namespace sw
{
    void PixelPerfectCameraComponent::validateBarSortingLayer( ValidationContext& context ) const
    {
        SortingLayerNameValidation::validate( context, "_barSortingLayer", _barSortingLayer );
    }
} // namespace sw

#include "PixelPerfectCameraComponent.cpp.gen.inl"
