#include "pch.h"

#include "Engine/Object/Component/2D/SpriteRenderUtil.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Resource/WeakInternCache.h"

namespace sw
{
    namespace
    {
        struct SpriteRenderUtilInternal
        {
            /** @brief 텍스처 인스턴스 표의 키입니다 — 부모 머티리얼과 텍스처. */
            struct TextureInstanceKey
            {
                Material*     _pParent{ nullptr };
                hashed_string _texture{};
                hashed_string _normalMap{};

                bool operator==( const TextureInstanceKey& other ) const
                {
                    return _pParent == other._pParent && _texture == other._texture && _normalMap == other._normalMap;
                }
            };

            struct TextureInstanceKeyHash
            {
                size_t operator()( const TextureInstanceKey& key ) const noexcept
                {
                    return std::hash<const void*>{}( key._pParent ) ^ ( static_cast<size_t>( key._texture.getHash() ) * 0x9E3779B97F4A7C15ull ) ^
                           ( static_cast<size_t>( key._normalMap.getHash() ) * 0xC2B2AE3D27D4EB4Full );
                }
            };

            using TextureInstanceCache = WeakInternCache<TextureInstanceKey, MaterialInstance, TextureInstanceKeyHash>;

            /** @brief 부모 머티리얼에 텍스처(와 노멀 맵)만 덮어쓴 인스턴스를 만듭니다. */
            static shared_ptr<MaterialInstance> createTextureInstance( const TextureInstanceKey& key )
            {
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( key._pParent );
                instance->setTextureParameter( hashed_string( "albedoMap" ), key._texture.c_str() );
                if ( key._normalMap.empty() == false )
                    instance->setTextureParameter( hashed_string( "normalMap" ), key._normalMap.c_str() );
                return instance;
            }

            /** @brief (머티리얼, 텍스처) 인스턴스 표입니다. */
            static TextureInstanceCache& getTextureInstanceCache()
            {
                static TextureInstanceCache s_cache{ "SpriteTextureInstance" };
                return s_cache;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SpriteRenderUtil" );

    hashed_string SpriteRenderUtil::getSpriteMaterialPath()
    {
        static const hashed_string s_spriteMaterial( "engine/materials/sprite2d.material" );
        return s_spriteMaterial;
    }

    shared_ptr<MaterialInstance> SpriteRenderUtil::acquireTextureInstance( Material* pParent, hashed_string texture, hashed_string normalMap )
    {
        const SpriteRenderUtilInternal::TextureInstanceKey key{ pParent, texture, normalMap };
        return SpriteRenderUtilInternal::getTextureInstanceCache().acquire( key, &SpriteRenderUtilInternal::createTextureInstance );
    }

    IAssetCache& SpriteRenderUtil::getTextureInstanceCache()
    {
        return SpriteRenderUtilInternal::getTextureInstanceCache();
    }

    uint32 SpriteRenderUtil::resolveSortKey( const hashed_string& layerName, int32 orderInLayer, string_view ownerLabel )
    {
        const Render2DSettings& settings = Render2DSettings::getActive();
        uint32                  sortKey  = 0;
        if ( settings.resolveSortKey( layerName, orderInLayer, sortKey ) == false )
            SW_LOG_ERROR( "'%#': unknown sorting layer '%#' - drawn in 'Default' (layers are listed in render2d.xml)", ownerLabel, layerName.c_str() );
        return sortKey;
    }
} // namespace sw
