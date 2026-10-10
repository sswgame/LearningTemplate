#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Animation/Sprite/SpriteClipCache.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/2D/SpriteMeshBuilder.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteComponent" );

    SpriteComponent::SpriteComponent()
        : _textureName{}
        , _normalMapName{}
        , _appliedTexture{}
        , _appliedNormalMap{}
        , _clipPath{}
        , _clipFrame{ 0 }
        , _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f }
        , _tint{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _sortingLayer{ "Default" }
        , _drawMode{ SpriteDrawMode::Simple }
        , _size{ 1.0f, 1.0f }
        , _sliceBorder{ 0.0f, 0.0f, 0.0f, 0.0f }
        , _bSliceMeshApplied{ SW_FALSE }
        , _orderInLayer{ 0 }
        , _clip{}
        , _loadedClipPath{}
    {
    }

    void SpriteComponent::onBeginPlay()
    {
        MeshComponent::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
    }

    void SpriteComponent::resolveRenderAssets()
    {
        MeshComponent::resolveRenderAssets();
        refreshClip();
        refreshTextureInstance();
        refreshSpriteInstanceData();
        refreshDrawModeMesh();
        refreshSortKey();
    }

    void SpriteComponent::onPropertyChanged( hashed_string propertyName )
    {
        MeshComponent::onPropertyChanged( propertyName );
        static const hashed_string s_textureName( "_textureName" );
        static const hashed_string s_normalMapName( "_normalMapName" );
        static const hashed_string s_clipPath( "_clipPath" );
        static const hashed_string s_clipFrame( "_clipFrame" );
        static const hashed_string s_uvRect( "_uvRect" );
        static const hashed_string s_tint( "_tint" );
        static const hashed_string s_sortingLayer( "_sortingLayer" );
        static const hashed_string s_orderInLayer( "_orderInLayer" );
        static const hashed_string s_drawMode( "_drawMode" );
        static const hashed_string s_size( "_size" );
        static const hashed_string s_sliceBorder( "_sliceBorder" );
        if ( propertyName == s_clipPath )
        {
            refreshClip();
            refreshTextureInstance();
            refreshSpriteInstanceData();
            refreshDrawModeMesh();
        }
        else if ( propertyName == s_textureName || propertyName == s_normalMapName )
        {
            refreshTextureInstance();
        }
        else if ( propertyName == s_clipFrame || propertyName == s_uvRect || propertyName == s_tint )
        {
            refreshSpriteInstanceData();
            refreshDrawModeMesh(); // 프레임마다 테두리가 다를 수 있다
        }
        else if ( propertyName == s_drawMode || propertyName == s_size || propertyName == s_sliceBorder )
        {
            refreshDrawModeMesh();
        }
        else if ( propertyName == s_sortingLayer || propertyName == s_orderInLayer )
        {
            refreshSortKey();
        }
    }

    void SpriteComponent::setTextureName( string_view texture )
    {
        _textureName = string{ texture };
        refreshTextureInstance();
    }

    void SpriteComponent::setNormalMapName( string_view normalMap )
    {
        _normalMapName = string{ normalMap };
        refreshTextureInstance();
    }

    void SpriteComponent::setClipPath( string_view path )
    {
        _clipPath = string{ path };
        refreshClip();
        refreshTextureInstance();
        refreshSpriteInstanceData();
        refreshDrawModeMesh();
    }

    void SpriteComponent::setClipFrame( int32 frame )
    {
        _clipFrame = MathUtil::max( frame, 0 );
        refreshSpriteInstanceData();
        refreshDrawModeMesh();
    }

    void SpriteComponent::setUvRect( const float4& uvRect )
    {
        _uvRect = uvRect;
        refreshSpriteInstanceData();
    }

    void SpriteComponent::setTint( const float4& tint )
    {
        _tint = tint;
        refreshSpriteInstanceData();
    }

    void SpriteComponent::setSortingLayer( const hashed_string& layerName )
    {
        _sortingLayer = layerName;
        refreshSortKey();
    }

    void SpriteComponent::setOrderInLayer( int32 order )
    {
        _orderInLayer = MathUtil::clamp( order, Render2DSettings::kMinOrderInLayer, Render2DSettings::kMaxOrderInLayer );
        refreshSortKey();
    }

    void SpriteComponent::setDrawMode( SpriteDrawMode mode )
    {
        _drawMode = mode;
        refreshDrawModeMesh();
    }

    void SpriteComponent::setSize( const float2& size )
    {
        _size = float2{ MathUtil::max( size._x, 0.0f ), MathUtil::max( size._y, 0.0f ) };
        refreshDrawModeMesh();
    }

    void SpriteComponent::setSliceBorder( const float4& border )
    {
        _sliceBorder = border;
        refreshDrawModeMesh();
    }

    float4 SpriteComponent::getEffectiveSliceBorder() const
    {
        if ( _clip != nullptr && _clip->getFrameCount() > 0 )
        {
            const SpriteClipFrame* pFrame = _clip->findFrame( MathUtil::clamp( _clipFrame, 0, _clip->getFrameCount() - 1 ) );
            if ( pFrame != nullptr && pFrame->hasBorder() )
                return pFrame->_border;
        }
        return _sliceBorder;
    }

    float4 SpriteComponent::getDisplayedUvRect() const
    {
        if ( _clip == nullptr || _clip->getFrameCount() == 0 )
            return _uvRect;
        const int32 lastFrame = _clip->getFrameCount() - 1;
        return _clip->findFrame( MathUtil::clamp( _clipFrame, 0, lastFrame ) )->_uvRect;
    }

    hashed_string SpriteComponent::getDefaultMaterialPath() const
    {
        return SpriteRenderUtil::getSpriteMaterialPath();
    }

    string_view SpriteComponent::getEffectiveTexture() const
    {
        if ( _textureName.empty() == false || _clip == nullptr )
            return _textureName;
        return _clip->_atlasPath;
    }

    void SpriteComponent::refreshClip()
    {
        if ( _clipPath == _loadedClipPath )
            return;
        _loadedClipPath = _clipPath;
        _clip           = SpriteClipCache::acquire( _clipPath );
        // 읽지 못한 경로는 `_loadedClipPath` 에 남아 다시 읽지 않는다. 경고는 로더가 이유와 함께 남겼다. 여기서는 무엇을 대신 그리는지만 말한다.
        if ( _clip == nullptr && _clipPath.empty() == false )
            SW_LOG_WARNING( "Sprite clip '%#' could not be read - the sprite shows its UV rect instead", _clipPath );
    }

    void SpriteComponent::refreshTextureInstance()
    {
        Material*         pMaterial   = getMaterial();
        const string_view textureName = getEffectiveTexture();
        if ( pMaterial == nullptr || textureName.empty() )
        {
            // 이 컴포넌트가 건 인스턴스만 뗀다. 코드가 건 인스턴스(텍스처 칸을 쓰지 않는)는 그대로다.
            if ( _appliedTexture.empty() == false )
            {
                _appliedTexture   = hashed_string{};
                _appliedNormalMap = hashed_string{};
                setMaterialInstance( nullptr );
            }
            return;
        }

        const hashed_string     texture( string{ textureName }.c_str() );
        const hashed_string     normalMap = _normalMapName.empty() ? hashed_string{} : hashed_string( string_view( _normalMapName ) );
        const MaterialInstance* pCurrent  = getRawMaterialInstance();
        if ( texture == _appliedTexture && normalMap == _appliedNormalMap && pCurrent != nullptr && pCurrent->getParent() == pMaterial )
            return;
        _appliedTexture   = texture;
        _appliedNormalMap = normalMap;
        setMaterialInstance( SpriteRenderUtil::acquireTextureInstance( pMaterial, texture, normalMap ) );
    }

    void SpriteComponent::refreshDrawModeMesh()
    {
        if ( _drawMode == SpriteDrawMode::Simple )
        {
            // 슬라이스 메시를 걸었던 것만 되돌린다. 처음부터 Simple 이면 메시 해석(`resolveRuntimeMesh`)이 건 사각형이 그대로다.
            if ( _bSliceMeshApplied == SW_FALSE )
                return;
            _bSliceMeshApplied       = SW_FALSE;
            const string_view meshId = getMeshId().empty() ? getDefaultMeshId() : string_view{ getMeshId() };
            setMesh( MeshUtil::acquirePrimitive( meshId ) );
            return;
        }
        SlicedSpriteDesc desc{};
        desc._size            = _size;
        desc._border          = getEffectiveSliceBorder();
        desc._bTiled          = ( _drawMode == SpriteDrawMode::Tiled ) ? SW_TRUE : SW_FALSE;
        shared_ptr<Mesh> mesh = SpriteMeshBuilder::acquireSlicedMesh( desc );
        _bSliceMeshApplied    = SW_TRUE;
        if ( mesh != getMesh() )
            setMesh( std::move( mesh ) );
    }

    void SpriteComponent::refreshSortKey()
    {
        const GameObject*   pOwner    = getOwner();
        const hashed_string ownerName = ( pOwner != nullptr ) ? pOwner->getName() : hashed_string( "SpriteComponent" );
        setSortKey( SpriteRenderUtil::resolveSortKey( _sortingLayer, _orderInLayer, ownerName.c_str() ) );
    }

    void SpriteComponent::refreshSpriteInstanceData()
    {
        setSpriteInstanceData( GPUSpriteInstanceData::make( getDisplayedUvRect(), _tint ) );
    }

    bool SpriteComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        // 스프라이트는 XY 평면의 사각형이다(Simple 은 단위 사각형, Sliced · Tiled 는 `_size`). Z 스케일은 두께가 없으니 보지 않는다.
        const float2   localSize    = ( _drawMode == SpriteDrawMode::Simple ) ? float2{ 1.0f, 1.0f } : _size;
        const float32  halfDiagonal = 0.5f * MathUtil::sqrt( localSize._x * localSize._x + localSize._y * localSize._y );
        const float4x4 world        = getWorldMatrix();
        const float3   scale        = world.getScale();
        outCenter                   = world.getTranslation();
        outRadius                   = halfDiagonal * MathUtil::max( scale._x, scale._y );
        return true;
    }

} // namespace sw
