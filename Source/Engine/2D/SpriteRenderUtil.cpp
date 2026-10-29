#include "pch.h"

#include "Engine/2D/SpriteRenderUtil.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cache/WeakInternCache.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
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
                    return std::hash<const void*>{}( key._pParent ) ^ ( static_cast<size_t>( key._texture.getHash() ) * HashUtil::kGoldenRatio64 ) ^
                           ( static_cast<size_t>( key._normalMap.getHash() ) * 0xC2B2AE3D27D4EB4Full );
                }
            };

            /** @brief (머티리얼, 텍스처) 인스턴스 표입니다 — 에셋 매니저가 내장 캐시로 만들어 소유한다. */
            class TextureInstanceCache final : public WeakInternCache<TextureInstanceKey, MaterialInstance, TextureInstanceKeyHash>
            {
            public:
                static constexpr const utf8* kAssetKindName = "SpriteTextureInstance";
                TextureInstanceCache()
                    : WeakInternCache{ kAssetKindName }
                {
                }
            };

            /** @brief 부모 머티리얼에 텍스처(와 노멀 맵)만 덮어쓴 인스턴스를 만듭니다. */
            static shared_ptr<MaterialInstance> createTextureInstance( const TextureInstanceKey& key )
            {
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( key._pParent );
                instance->setTextureParameter( hashed_string( "_albedoMap" ), key._texture.c_str() );
                if ( key._normalMap.empty() == false )
                    instance->setTextureParameter( hashed_string( "_normalMap" ), key._normalMap.c_str() );
                return instance;
            }

            /** @brief (머티리얼, 텍스처) 인스턴스 표입니다. 엔진 서비스가 없으면(캐시를 쓸 엔진이 없다) 멈춘다. */
            static TextureInstanceCache& getTextureInstanceCache() { return engine::getSubsystem<AssetManager>().getAssetCache<TextureInstanceCache>(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SpriteRenderUtil" );

    hashed_string SpriteRenderUtil::getSpriteMaterialPath()
    {
        static const hashed_string s_spriteMaterial( engine::getSubsystem<EngineDefaultAssets>()._spriteMaterial.c_str() ); // 경로의 원본은 enginedefaultassets.xml
        return s_spriteMaterial;
    }

    shared_ptr<MaterialInstance> SpriteRenderUtil::acquireTextureInstance( Material* pParent, hashed_string texture, hashed_string normalMap )
    {
        const SpriteRenderUtilInternal::TextureInstanceKey key{ pParent, texture, normalMap };
        return SpriteRenderUtilInternal::getTextureInstanceCache().acquire( key, &SpriteRenderUtilInternal::createTextureInstance );
    }

    uint32 SpriteRenderUtil::resolveSortKey( const hashed_string& layerName, int32 orderInLayer, string_view ownerLabel )
    {
        const Render2DSettings& settings = engine::getSubsystem<Render2DSettings>();
        uint32                  sortKey  = 0;
        if ( settings.resolveSortKey( layerName, orderInLayer, sortKey ) == false )
            SW_LOG_ERROR( "'%#': unknown sorting layer '%#' - drawn in 'Default' (layers are listed in render2d.xml)", ownerLabel, layerName.c_str() );
        return sortKey;
    }
} // namespace sw

namespace sw
{
    using SpriteTextureInstanceCache = SpriteRenderUtilInternal::TextureInstanceCache;
    SW_BUILT_IN_ASSET_CACHE_WITHOUT_RELOAD( SpriteTextureInstanceCache );
} // namespace sw
