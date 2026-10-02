#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/2D/SpriteRenderUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    SW_LOG_CALLER( "SpriteComponent" );

    SpriteComponent::SpriteComponent()
        : _textureName{}
        , _appliedTexture{}
        , _clipPath{}
        , _clipFrame{ 0 }
        , _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f }
        , _tint{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _clip{}
        , _loadedClipPath{}
    {
    }

    void SpriteComponent::onBeginPlay()
    {
        MeshComponent::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );

        GameObject* pGameObject = getOwner();
        if ( pGameObject != nullptr )
            pGameObject->addTag( "Sprite"_tag );
    }

    void SpriteComponent::onEndPlay()
    {
        MeshComponent::onEndPlay();
    }

    void SpriteComponent::resolveRenderAssets()
    {
        MeshComponent::resolveRenderAssets();
        refreshClip();
        refreshTextureInstance();
        refreshSpriteInstanceData();
    }

    void SpriteComponent::onPropertyChanged( hashed_string propertyName )
    {
        MeshComponent::onPropertyChanged( propertyName );
        static const hashed_string s_textureName( "_textureName" );
        static const hashed_string s_clipPath( "_clipPath" );
        static const hashed_string s_clipFrame( "_clipFrame" );
        static const hashed_string s_uvRect( "_uvRect" );
        static const hashed_string s_tint( "_tint" );
        if ( propertyName == s_clipPath )
        {
            refreshClip();
            refreshTextureInstance();
            refreshSpriteInstanceData();
        }
        else if ( propertyName == s_textureName )
        {
            refreshTextureInstance();
        }
        else if ( propertyName == s_clipFrame || propertyName == s_uvRect || propertyName == s_tint )
        {
            refreshSpriteInstanceData();
        }
    }

    void SpriteComponent::refreshFromClip()
    {
        refreshTextureInstance();    // 아틀라스가 바뀌었을 수 있다
        refreshSpriteInstanceData(); // 지금 프레임의 UV
    }

    void SpriteComponent::setTextureName( string_view texture )
    {
        _textureName = string{ texture };
        refreshTextureInstance();
    }

    void SpriteComponent::setClipPath( string_view path )
    {
        _clipPath = string{ path };
        refreshClip();
        refreshTextureInstance();
        refreshSpriteInstanceData();
    }

    void SpriteComponent::setClipFrame( int32 frame )
    {
        _clipFrame = MathUtil::max( frame, 0 );
        refreshSpriteInstanceData();
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
        _clip           = SpriteClipAsset::acquireShared( _clipPath );
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
                _appliedTexture = hashed_string{};
                setMaterialInstance( nullptr );
            }
            return;
        }

        const hashed_string     texture( string{ textureName }.c_str() );
        const MaterialInstance* pCurrent = getRawMaterialInstance();
        if ( texture == _appliedTexture && pCurrent != nullptr && pCurrent->getParent() == pMaterial )
            return;
        _appliedTexture = texture;
        setMaterialInstance( SpriteRenderUtil::acquireTextureInstance( pMaterial, texture ) );
    }

    void SpriteComponent::refreshSpriteInstanceData()
    {
        setSpriteInstanceData( GpuSpriteInstanceData::make( getDisplayedUvRect(), _tint ) );
    }

    bool SpriteComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        // 스프라이트는 XY 평면의 단위 사각형이다. Z 스케일은 두께가 없으니 보지 않는다.
        constexpr float32 kUnitQuadHalfDiagonal = 0.70710678f;
        const float4x4    world                 = getWorldMatrix();
        const float3      scale                 = world.getScale();
        outCenter                               = world.getTranslation();
        outRadius                               = kUnitQuadHalfDiagonal * MathUtil::max( scale._x, scale._y );
        return true;
    }

} // namespace sw
