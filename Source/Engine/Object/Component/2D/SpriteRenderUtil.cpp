#include "pch.h"

#include "Engine/Object/Component/2D/SpriteRenderUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Graphics/Material/MaterialInstance.h"

namespace sw
{
    namespace
    {
        struct SpriteRenderUtilInternal
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    hashed_string SpriteRenderUtil::getSpriteMaterialPath()
    {
        static const hashed_string s_spriteMaterial( "engine/materials/sprite2d.material" );
        return s_spriteMaterial;
    }

    shared_ptr<MaterialInstance> SpriteRenderUtil::acquireTextureInstance( Material* pParent, hashed_string texture )
    {
        using Key     = SpriteRenderUtilInternal::TextureInstanceKey;
        using KeyHash = SpriteRenderUtilInternal::TextureInstanceKeyHash;
        static mutex                                                   s_mutexInstance;
        static unordered_map<Key, weak_ptr<MaterialInstance>, KeyHash> s_mapInstance;

        const Key               key{ pParent, texture };
        std::scoped_lock<mutex> lock{ s_mutexInstance };
        const auto              it = s_mapInstance.find( key );
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
} // namespace sw
