#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    namespace
    {
        struct SpriteComponentInternal
        {
            /** @brief 텍스처 인스턴스 표의 키입니다 — 부모 머티리얼과 텍스처. */
            struct TextureInstanceKey
            {
                const Material* _pParent{ nullptr };
                hashed_string   _texture{};

                bool operator==( const TextureInstanceKey& other ) const { return _pParent == other._pParent && _texture == other._texture; }
            };

            struct TextureInstanceKeyHash
            {
                size_t operator()( const TextureInstanceKey& key ) const noexcept
                {
                    return std::hash<const void*>{}( key._pParent ) ^ ( static_cast<size_t>( key._texture.getHash() ) * 0x9E3779B97F4A7C15ull );
                }
            };

            /**
             * @brief (머티리얼, 텍스처)의 인스턴스를 나눠 줍니다. 없으면 만듭니다.
             * @details 표는 **약한 참조**다(`MeshUtil::acquirePrimitive` 와 같은 모양) — 소유는 스프라이트에 있고 마지막 스프라이트가 놓으면
             *          인스턴스도 사라진다. 사라진 칸은 새로 만들 때 걷는다.
             */
            static shared_ptr<MaterialInstance> acquireTextureInstance( Material* pParent, hashed_string texture )
            {
                static mutex                                                                                 s_mutexInstance;
                static unordered_map<TextureInstanceKey, weak_ptr<MaterialInstance>, TextureInstanceKeyHash> s_mapInstance;

                const TextureInstanceKey key{ pParent, texture };
                std::scoped_lock<mutex>  lock{ s_mutexInstance };
                const auto               it = s_mapInstance.find( key );
                if ( it != s_mapInstance.end() )
                {
                    if ( shared_ptr<MaterialInstance> instance = it->second.lock() )
                        return instance;
                }

                for ( auto iter = s_mapInstance.begin(); iter != s_mapInstance.end(); )
                {
                    if ( iter->second.expired() )
                        iter = s_mapInstance.erase( iter );
                    else
                        ++iter;
                }
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( pParent );
                instance->setTextureParameter( hashed_string( "albedoMap" ), texture.c_str() );
                s_mapInstance[key] = instance;
                return instance;
            }
        };
    } // namespace

    SpriteComponent::SpriteComponent()
        : _textureName{}
        , _appliedTexture{}
        , _spriteName{}
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
        refreshTextureInstance();
    }

    void SpriteComponent::onPropertyChanged( hashed_string propertyName )
    {
        MeshComponent::onPropertyChanged( propertyName );
        static const hashed_string s_textureName( "_textureName" );
        if ( propertyName == s_textureName )
            refreshTextureInstance();
    }

    void SpriteComponent::setTextureName( string_view texture )
    {
        _textureName = string{ texture };
        refreshTextureInstance();
    }

    hashed_string SpriteComponent::getDefaultMaterialPath() const
    {
        static const hashed_string s_spriteMaterial( "engine/materials/sprite2d.material" );
        return s_spriteMaterial;
    }

    void SpriteComponent::refreshTextureInstance()
    {
        Material* pMaterial = getMaterial();
        if ( pMaterial == nullptr || _textureName.empty() )
        {
            // 이 컴포넌트가 건 인스턴스만 뗀다. 코드가 건 인스턴스(텍스처 칸을 쓰지 않는)는 그대로다.
            if ( _appliedTexture.empty() == false )
            {
                _appliedTexture = hashed_string{};
                setMaterialInstance( nullptr );
            }
            return;
        }

        const hashed_string     texture( _textureName.c_str() );
        const MaterialInstance* pCurrent = getRawMaterialInstance();
        if ( texture == _appliedTexture && pCurrent != nullptr && pCurrent->getParent() == pMaterial )
            return;
        _appliedTexture = texture;
        setMaterialInstance( SpriteComponentInternal::acquireTextureInstance( pMaterial, texture ) );
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
