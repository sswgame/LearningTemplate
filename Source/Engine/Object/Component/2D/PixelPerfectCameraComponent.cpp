#include "pch.h"

#include "Engine/Object/Component/2D/PixelPerfectCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "PixelPerfectCamera" );

    namespace
    {
        struct PixelPerfectCameraComponentInternal
        {
            static constexpr uint32 kBarCount = 4; ///< 좌 · 우 · 아래 · 위
            /** @brief 띠를 눈 앞에 놓는 거리(근평면의 배수). 근평면 바로 뒤라 장면의 어떤 스프라이트보다 앞이다. */
            static constexpr float32 kBarNearPlaneScale = 2.0f;
            /** @brief 띠를 바깥쪽으로 더 덮는 화면 픽셀 수(가장자리 반올림 틈을 막는다). */
            static constexpr float32 kBarOverlapPixels = 2.0f;
        };
    } // namespace

    PixelPerfectCameraComponent::PixelPerfectCameraComponent()
        : _pixelsPerUnit{ 16.0f }
        , _referenceResolution{ 320.0f, 180.0f }
        , _barSortingLayer{ "Overlay" }
        , _bCropX{ false }
        , _bCropY{ false }
        , _bPixelSnapping{ true }
        , _publishedSnapUnit{ 0.0f }
        , _layout{}
        , _barBatch{}
    {
    }

    void PixelPerfectCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 카메라를 따라가게 하는 컴포넌트(앞 그룹) 뒤, 시차 레이어(PostUpdate) 앞.
        setTickGroup( TickGroup::PostPhysics );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
        {
            _barBatch.setSorting( _barSortingLayer, Render2DSettings::kMaxOrderInLayer );
            _barBatch.setOwnerComponent( this );
            if ( _barBatch.initialize( *pManager, {}, PixelPerfectCameraComponentInternal::kBarCount ) == false )
                SW_LOG_WARNING( "Pixel perfect letterbox bars could not be created" );
        }
    }

    void PixelPerfectCameraComponent::onEndPlay()
    {
        publishSnapUnit( 0.0f );
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = ( pOwner != nullptr ) ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            pCamera->setViewOffset( float3{ 0.0f, 0.0f, 0.0f } );
        _barBatch.shutdown();
        Component::onEndPlay();
    }

    void PixelPerfectCameraComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_activeName( "_bActive" );
        if ( propertyName == s_activeName )
            _barBatch.markAllEntriesDirty();
    }

    void PixelPerfectCameraComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        _barBatch.markAllEntriesDirty();
    }

    void PixelPerfectCameraComponent::onTick( float32 /*deltaTime*/ )
    {
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const CameraRegistry& cameras = pManager->getCameraRegistry();
        (void)applyToCamera( cameras.getViewportWidth(), cameras.getViewportHeight() );
    }

    PixelPerfectLayout PixelPerfectCameraComponent::computeLayout( const float2& referenceResolution, float32 pixelsPerUnit, uint32 viewportWidth,
                                                                   uint32 viewportHeight, bool bCropX, bool bCropY )
    {
        PixelPerfectLayout layout{};
        const float32      ppu = MathUtil::max( pixelsPerUnit, 1e-3f );
        if ( viewportWidth == 0 || viewportHeight == 0 || referenceResolution._x < 1.0f || referenceResolution._y < 1.0f )
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

        pCamera->setOrthographic( true );
        pCamera->setOrthoHeight( _layout._orthoHeight );
        // 그리는 눈만 격자에 붙인다(트랜스폼은 그대로 — 감쇠로 따라가는 카메라가 반 픽셀 아래의 움직임을 잃지 않게).
        const float3 world = pCamera->getWorldPosition();
        const float3 offset{ snapToGrid( world._x, _layout._screenPixelUnit ) - world._x, snapToGrid( world._y, _layout._screenPixelUnit ) - world._y, 0.0f };
        pCamera->setViewOffset( offset );

        publishSnapUnit( _bPixelSnapping ? _layout._assetPixelUnit : 0.0f );
        layoutBars( pCamera->getCameraPosition(), viewportWidth, viewportHeight );
        return _layout;
    }

    void PixelPerfectCameraComponent::publishSnapUnit( float32 unit )
    {
        if ( unit == _publishedSnapUnit )
            return;
        _publishedSnapUnit          = unit;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = ( pOwner != nullptr ) ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 다른 오브젝트의 메시를 고치므로 틱 뒤로 미룬다(그 오브젝트의 틱 — 애니메이터의 프레임 넘기기 — 와 겹치지 않게).
        pManager->executeOrDeferPostTick( [pManager, unit]()
        {
            pManager->getCameraRegistry().setPixelSnapUnit( unit );
            for ( MeshComponent* pMesh : pManager->getPrimitiveRegistry().getAll() )
            {
                if ( pMesh != nullptr )
                    pMesh->setPixelSnapUnit( unit );
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
        const float4           fullUv{ 0.0f, 0.0f, 1.0f, 1.0f };
        if ( bShowX )
        {
            // 띠 폭은 정수 픽셀이다 — 안쪽 가장자리가 기준 해상도 × 배율의 가장자리와 같은 화면 픽셀에 떨어진다.
            const float32 barWidth = static_cast<float32>( _layout._barWidthPixels ) * unit + overlap;
            const float32 offsetX  = halfWidth + overlap - barWidth * 0.5f;
            _barBatch.setEntry( 0, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x - offsetX, eye._y, barZ }, barWidth, 2.0f * ( halfHeight + overlap ) ), fullUv,
                                black );
            _barBatch.setEntry( 1, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x + offsetX, eye._y, barZ }, barWidth, 2.0f * ( halfHeight + overlap ) ), fullUv,
                                black );
        }
        if ( bShowY )
        {
            const float32 barHeight = static_cast<float32>( _layout._barHeightPixels ) * unit + overlap;
            const float32 offsetY   = halfHeight + overlap - barHeight * 0.5f;
            _barBatch.setEntry( 2, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x, eye._y - offsetY, barZ }, 2.0f * ( halfWidth + overlap ), barHeight ), fullUv,
                                black );
            _barBatch.setEntry( 3, SpriteInstanceBatch::makeQuadWorld( float3{ eye._x, eye._y + offsetY, barZ }, 2.0f * ( halfWidth + overlap ), barHeight ), fullUv,
                                black );
        }
    }
} // namespace sw
