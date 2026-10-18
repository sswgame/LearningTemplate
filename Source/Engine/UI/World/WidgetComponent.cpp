#include "pch.h"

#include "Engine/UI/World/WidgetComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    SW_LOG_CALLER( "WidgetComponent" );
} // namespace sw

namespace sw
{
    namespace
    {
        struct WidgetComponentInternal
        {
            /** @brief 가장자리에 붙인 마커가 화면 변에서 띄우는 거리(UI 단위)입니다. */
            static constexpr float32 kEdgeMargin = 24.0f;
            /** @brief 거리 배율의 범위입니다(멀어도 읽을 수 있고, 가까워도 화면을 덮지 않게 합니다). */
            static constexpr float32 kMinDistanceScale = 0.25f;
            static constexpr float32 kMaxDistanceScale = 2.0f;
            /** @brief 원근 나누기의 w 가 이보다 작으면 카메라 뒤(또는 눈 위치)로 봅니다. */
            static constexpr float32 kMinClipW = 1e-5f;
            /** @brief 그리기 크기를 정하지 않은 World 위젯의 렌더 텍스처 크기입니다. */
            static constexpr float32 kDefaultWorldWidth  = 256.0f;
            static constexpr float32 kDefaultWorldHeight = 128.0f;

            /**
             * @brief @p pOwner 의 씬이 받은 엔진 범위(`SceneEngineServices::_pEngineSubsystems`)에서 찾은 UI 시스템입니다. 서버처럼 없거나 시작 전이면 nullptr 입니다.
             * @details 전역 표(`engine::findSubsystem`)를 다시 찾지 않는다 — 씬 쪽은 만들 때 받은 묶음만 본다(`SceneEngineServices.h` 머리말).
             */
            static UISystem* findUISystem( const GameObject* pOwner )
            {
                const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                if ( pManager == nullptr )
                    return nullptr;
                UISystem* pUI = locateSubsystem<UISystem>( pManager->getSceneEngineServices()._pEngineSubsystems );
                return pUI != nullptr && pUI->isInitialized() ? pUI : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WidgetComponent::WidgetComponent()
        : Component{}
        , _pendingContent{}
        , _markerWidget{ kInvalidWidgetID }
        , _lastPlacement{}
        , _pUISystem{ nullptr }
        , _worldTree{}
        , _worldCanvas{}
        , _worldScratch{}
        , _worldRevision{ 1 }
        , _worldQuad{}
        , _bHidden{ SW_FALSE }
        , _documentPath{}
        , _space{ WidgetSpace::Screen }
        , _drawSize{}
        , _pivot{ 0.5f, 1.0f }
        , _worldSize{ 1.0f, 0.5f }
        , _screenOffset{}
        , _worldOffset{}
        , _maxDistance{ 0.0f }
        , _referenceDistance{ 10.0f }
        , _bClampToScreenEdge{ false }
        , _bScaleWithDistance{ false }
    {
    }

    WidgetComponent::~WidgetComponent()
    {
        bindUISystem( nullptr );
    }

    void WidgetComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 마커와 월드 사각형은 주 씬 컴포넌트의 월드 점을 따른다. 없으면 마커가 늘 접혀 아무것도 그리지 않으므로 조용히 넘기지 않는다.
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pOwner->getPrimarySceneComponent() == nullptr )
            SW_LOG_ERROR( "[UI] WidgetComponent on '%#' has no scene component to follow - add a SceneComponent first; the widget is never shown", pOwner->getName().c_str() );
        UISystem* pUI = WidgetComponentInternal::findUISystem( pOwner );
        bindUISystem( pUI );
        if ( pUI != nullptr && _space == WidgetSpace::World )
            createWorldQuad();
    }

    void WidgetComponent::onEndPlay()
    {
        destroyWorldQuad();
        bindUISystem( nullptr );
        Component::onEndPlay();
    }

    void WidgetComponent::bindUISystem( UISystem* pUISystem )
    {
        if ( _pUISystem == pUISystem )
            return;
        if ( _pUISystem != nullptr )
        {
            detachMarker();
            _pUISystem->unregisterWidgetComponent( *this );
        }
        _pUISystem = pUISystem;
        if ( _pUISystem == nullptr )
            return;
        _pUISystem->registerWidgetComponent( *this );
        attachMarker();
    }

    void WidgetComponent::forgetUISystem()
    {
        _pUISystem    = nullptr;
        _markerWidget = kInvalidWidgetID;
    }

    void WidgetComponent::setContent( unique_ptr<Widget> content )
    {
        detachMarker();
        _pendingContent = std::move( content );
        attachMarker();
    }

    Widget* WidgetComponent::getContent() const
    {
        if ( _pendingContent != nullptr )
            return _pendingContent.get();
        if ( _worldTree != nullptr )
            return _worldTree->getRoot();
        return _pUISystem != nullptr ? _pUISystem->findScreenMarker( _markerWidget ) : nullptr;
    }

    void WidgetComponent::setScaleWithDistance( bool bScale, float32 referenceDistance )
    {
        _bScaleWithDistance = bScale;
        _referenceDistance  = MathUtil::max( 0.01f, referenceDistance );
    }

    string WidgetComponent::getRenderTargetPath() const
    {
        return "rendertarget/widget_" + to_string( getComponentID() );
    }

    void WidgetComponent::attachMarker()
    {
        UISystem* const pUI = _pUISystem;
        if ( pUI == nullptr || _pendingContent == nullptr )
            return;
        if ( _space == WidgetSpace::World )
        {
            // World: 자기 트리(화면 스택 밖이라 포커스와 포인터가 없으며, 입력은 백로그에 있다)에 배치하고 렌더 텍스처로 칠한다.
            if ( _worldTree == nullptr )
                _worldTree = make_unique<WidgetTree>();
            _worldTree->setRoot( std::move( _pendingContent ) );
            return;
        }
        if ( _markerWidget != kInvalidWidgetID )
            return;
        // 첫 위치가 정해지기 전에는 접어 둔다(왼쪽 위에 한 프레임 보이지 않게 하기 위해서다).
        _pendingContent->setVisibility( WidgetVisibility::Collapsed );
        _markerWidget = pUI->addScreenMarker( std::move( _pendingContent ) );
    }

    void WidgetComponent::detachMarker()
    {
        _worldTree.reset();
        if ( _markerWidget == kInvalidWidgetID )
            return;
        if ( _pUISystem != nullptr )
            _pUISystem->removeScreenMarker( _markerWidget );
        _markerWidget = kInvalidWidgetID;
    }

    WidgetMarkerPlacement WidgetComponent::computeMarkerPlacement( const float4x4& viewProjection, const float3& cameraPosition, const float3& worldPosition,
                                                                   const float2& viewportSize ) const
    {
        using Internal = WidgetComponentInternal;
        WidgetMarkerPlacement placement{};
        placement._distance = float3::getDistance( cameraPosition, worldPosition );
        if ( _maxDistance > 0.0f && placement._distance > _maxDistance )
        {
            placement._bVisible = SW_FALSE;
            return placement;
        }
        if ( _bScaleWithDistance && placement._distance > 0.0f )
            placement._scale = MathUtil::clamp( _referenceDistance / placement._distance, Internal::kMinDistanceScale, Internal::kMaxDistanceScale );

        const float4 clip    = float4::transform( float4{ worldPosition._x, worldPosition._y, worldPosition._z, 1.0f }, viewProjection );
        const bool   bBehind = clip._w < Internal::kMinClipW;
        const float2 center{ viewportSize._x * 0.5f, viewportSize._y * 0.5f };
        float2       screen{};
        if ( bBehind == false )
        {
            // NDC(y 위가 +) → UI 단위(y 아래가 +).
            const float32 invW = 1.0f / clip._w;
            screen             = float2{ ( clip._x * invW * 0.5f + 0.5f ) * viewportSize._x, ( 0.5f - clip._y * invW * 0.5f ) * viewportSize._y };
        }
        screen._x += _screenOffset._x;
        screen._y += _screenOffset._y;
        const bool bOnScreen = bBehind == false && 0.0f <= screen._x && screen._x <= viewportSize._x && 0.0f <= screen._y && screen._y <= viewportSize._y;
        if ( bOnScreen )
        {
            placement._position = screen;
            return placement;
        }
        if ( _bClampToScreenEdge == false )
        {
            placement._bVisible = SW_FALSE;
            return placement;
        }
        // 가장자리에 붙인다. 화면 가운데에서 그 점 쪽으로 가다가 안쪽 사각형 변에 닿는 위치다. 카메라 뒤면 나누기 전 클립 좌표의 방향을 쓴다
        // (오른쪽 뒤면 오른쪽 변에 붙이는데, 그쪽으로 도는 것이 가깝기 때문이다).
        float2 direction = bBehind ? float2{ clip._x, -clip._y } : float2{ screen._x - center._x, screen._y - center._y };
        if ( direction._x == 0.0f && direction._y == 0.0f )
            direction = float2{ 0.0f, 1.0f };
        const float32 halfWidth  = MathUtil::max( 0.0f, center._x - Internal::kEdgeMargin );
        const float32 halfHeight = MathUtil::max( 0.0f, center._y - Internal::kEdgeMargin );
        const float32 scaleX     = direction._x != 0.0f ? halfWidth / MathUtil::abs( direction._x ) : MathUtil::kMaxFloat;
        const float32 scaleY     = direction._y != 0.0f ? halfHeight / MathUtil::abs( direction._y ) : MathUtil::kMaxFloat;
        const float32 reach      = MathUtil::min( scaleX, scaleY );
        placement._position      = float2{ center._x + direction._x * reach, center._y + direction._y * reach };
        placement._bClamped      = SW_TRUE;
        // UI 는 y 아래가 +이므로 위(0, -1)가 0 도, 오른쪽(1, 0)이 90 도다.
        placement._edgeAngle = MathUtil::atan2( direction._x, -direction._y );
        return placement;
    }

    void WidgetComponent::updateScreenMarker( const UIViewport& viewport, WidgetMarkerView& inoutView )
    {
        if ( _markerWidget == kInvalidWidgetID || viewport._physicalSize._y <= 0.0f )
            return;
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return; // 오브젝트 밖이다(테스트가 `applyPlacement` 로 직접 놓는다)
        const GameObjectManager* pManager = pOwner->getManager();
        if ( pManager != inoutView._pManager )
        {
            inoutView._pManager = pManager;
            inoutView._pCamera  = pManager != nullptr ? pManager->getCameraRegistry().selectCamera( CameraRole::Game ) : nullptr;
            if ( inoutView._pCamera != nullptr )
            {
                inoutView._viewProjection = inoutView._pCamera->getViewProjectionMatrix( viewport._physicalSize._x / viewport._physicalSize._y );
                inoutView._cameraPosition = inoutView._pCamera->getCameraPosition();
            }
        }
        const SceneComponent* pScene        = pOwner->getPrimarySceneComponent();
        const bool            bMarkerHidden = pScene == nullptr || inoutView._pCamera == nullptr || isActive() == false || _bHidden == SW_TRUE;
        if ( bMarkerHidden )
        {
            WidgetMarkerPlacement hidden{};
            hidden._bVisible = SW_FALSE;
            applyPlacement( hidden );
            return;
        }
        const float3 anchor = pScene->getWorldPosition() + _worldOffset;
        applyPlacement( computeMarkerPlacement( inoutView._viewProjection, inoutView._cameraPosition, anchor, viewport._size ) );
    }

    void WidgetComponent::applyPlacement( const WidgetMarkerPlacement& placement )
    {
        _lastPlacement  = placement;
        Widget* pMarker = _pUISystem != nullptr ? _pUISystem->findScreenMarker( _markerWidget ) : nullptr;
        if ( pMarker == nullptr )
            return;
        if ( placement._bVisible == SW_FALSE )
        {
            pMarker->setVisibility( WidgetVisibility::Collapsed );
            return;
        }
        pMarker->setVisibility( WidgetVisibility::HitTestInvisible ); // 마커는 클릭을 막지 않는다

        // 위치와 배율은 화면 앵커가 정한다(셰이더가 옮긴다). 위젯은 피벗이 앵커 점(원점)에 오도록 배치하므로, 움직여도 다시 배치하거나 칠하지 않는다.
        _pUISystem->setScreenMarkerAnchor( pMarker->getPaintAnchor(), placement._position, placement._scale );
        // 고정 크기면 그 크기를 쓴다(레이아웃 경계). 아니면 지난 원하는 크기로 피벗을 맞춘다 — 슬롯은 크기가 바뀔 때만 바뀐다.
        const bool       bFixed = _drawSize._x > 0.0f && _drawSize._y > 0.0f;
        const float2     size   = bFixed ? _drawSize : pMarker->getDesiredSize();
        WidgetLayoutSlot slot   = pMarker->getLayoutSlot();
        const float2     topLeft{ -_pivot._x * size._x, -_pivot._y * size._y };
        const float2     bottomRight{ topLeft._x + size._x, topLeft._y + size._y };
        const bool       bSlotChanged = slot._offsetMin != topLeft || slot._offsetMax != bottomRight || slot._anchorMin != float2{} || slot._anchorMax != float2{} ||
                                  slot._bAutoSize != ( bFixed == false ) || slot._widthOverride != ( bFixed ? size._x : 0.0f ) ||
                                  slot._heightOverride != ( bFixed ? size._y : 0.0f );
        if ( bSlotChanged )
        {
            slot._anchorMin      = float2{};
            slot._anchorMax      = float2{};
            slot._offsetMin      = topLeft;
            slot._offsetMax      = bottomRight;
            slot._bAutoSize      = bFixed == false;
            slot._widthOverride  = bFixed ? size._x : 0.0f;
            slot._heightOverride = bFixed ? size._y : 0.0f;
            pMarker->setLayoutSlot( slot );
        }
        onMarkerPlaced( *pMarker, placement );
    }

    void WidgetComponent::onMarkerPlaced( Widget& marker, const WidgetMarkerPlacement& placement )
    {
        (void)marker;
        (void)placement;
    }

    float2 WidgetComponent::getWorldTextureSize() const
    {
        if ( _drawSize._x > 0.0f && _drawSize._y > 0.0f )
            return float2{ MathUtil::ceil( _drawSize._x ), MathUtil::ceil( _drawSize._y ) };
        return float2{ WidgetComponentInternal::kDefaultWorldWidth, WidgetComponentInternal::kDefaultWorldHeight };
    }

    void WidgetComponent::updateWorldCanvas( const UILayoutContext& baseLayout, const UIPaintContext& basePaint )
    {
        if ( _space != WidgetSpace::World || _worldTree == nullptr )
            return;
        // 텍스처 픽셀은 UI 단위다(배율 1). 안전 영역은 없다. 글자 배율과 방향은 화면 UI 와 같다.
        const float2    size   = getWorldTextureSize();
        UILayoutContext layout = baseLayout;
        layout._viewportSize   = size;
        layout._safeInsets     = float4{};
        layout._uiScale        = 1.0f;
        (void)UILayoutPass::update( *_worldTree, layout );

        UIPaintContext paint = basePaint;
        paint._uiScale       = 1.0f;
        _worldScratch.clear();
        _worldScratch._targetSize = size;
        CanvasPainter painter( _worldScratch, 1.0f );
        (void)UIPaintPass::paint( *_worldTree, paint, painter, _worldScratch );
        if ( _worldScratch.isSameContent( _worldCanvas ) && _worldScratch._targetSize == _worldCanvas._targetSize )
            return;
        std::swap( _worldCanvas, _worldScratch );
        ++_worldRevision;
    }

    void WidgetComponent::appendWorldCanvas( vector<CanvasTargetDrawList>& inoutListTarget ) const
    {
        if ( _space != WidgetSpace::World || _worldTree == nullptr )
            return;
        CanvasTargetDrawList& target = inoutListTarget.emplace_back();
        target._targetPath           = hashed_string( getRenderTargetPath() );
        target._list                 = _worldCanvas;
        target._list._targetSize     = getWorldTextureSize();
        target._clearColor           = float4{};
        target._contentRevision      = _worldRevision;
    }

    void WidgetComponent::createWorldQuad()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _worldQuad.isValid() )
            return;
        // 스프라이트 사각형(양면, 단위 크기)에 렌더 텍스처를 읽는 머티리얼 인스턴스를 붙인다. 캔버스는 프리멀티플라이로 그리므로 셰이더가 직선 알파로 되돌린다.
        GameObject*    pQuad = pManager->createGameObject( hashed_string( string( pOwner->getName().c_str() ) + "_WidgetQuad" ) );
        MeshComponent* pMesh = pQuad != nullptr ? pQuad->addComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        (void)pQuad->attachToParent( pOwner ); // 방금 만든 같은 씬의 사각형이라 거부될 이유가 없다(틱 중이면 미뤄진다)
        // 렌더 텍스처 크기는 처음 만드는 쪽이 정한다. 머티리얼 인스턴스(렌더 스레드)가 캔버스보다 먼저 빌려도 위젯 크기로 만들어지도록 먼저 알린다.
        const float2  textureSize   = getWorldTextureSize();
        AssetManager* pAssetManager = pManager->findAssetManager();
        if ( pAssetManager != nullptr )
        {
            pAssetManager->getAssetCache<TextureCache>().declareRenderTarget( getRenderTargetPath(), static_cast<uint32>( textureSize._x ),
                                                                              static_cast<uint32>( textureSize._y ) );
        }
        pMesh->setMeshID( "Sprite" );
        pMesh->setMaterialPath( SpriteRenderUtil::getSpriteMaterialPath().c_str() );
        pMesh->setLocalScale( float3{ _worldSize._x, _worldSize._y, 1.0f } );
        if ( pMesh->getMaterial() != nullptr )
        {
            shared_ptr<MaterialInstance> instance = MaterialInstance::create( pMesh->getMaterial() );
            instance->setTextureParameter( hashed_string( "albedoMap" ), getRenderTargetPath() );
            instance->setParameter( hashed_string( "premultipliedTexture" ), "1" );
            pMesh->setMaterialInstance( std::move( instance ) );
        }
        _worldQuad = pQuad->getHandle();
    }

    void WidgetComponent::destroyWorldQuad()
    {
        if ( _worldQuad.isValid() == false )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pQuad    = pManager != nullptr ? pManager->findGameObjectByID( _worldQuad.objectID() ) : nullptr;
        if ( pQuad != nullptr )
            pQuad->destroy();
        _worldQuad = GameObjectHandle{};
    }
} // namespace sw
