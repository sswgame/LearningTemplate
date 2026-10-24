#include "pch.h"

#include "Engine/Object/Component/2D/SpriteRenderUtil.h"

#include "Core/Common/HashUtil.h"
#include "Core/Memory/LifetimeScope.h"

#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/WeakInternCache.h"

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

            using TextureInstanceCache = WeakInternCache<TextureInstanceKey, MaterialInstance, TextureInstanceKeyHash>;

            /** @brief 부모 머티리얼에 텍스처(와 노멀 맵)만 덮어쓴 인스턴스를 만듭니다. */
            static shared_ptr<MaterialInstance> createTextureInstance( const TextureInstanceKey& key )
            {
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( key._pParent );
                instance->setTextureParameter( hashed_string( "_albedoMap" ), key._texture.c_str() );
                if ( key._normalMap.empty() == false )
                    instance->setTextureParameter( hashed_string( "_normalMap" ), key._normalMap.c_str() );
                return instance;
            }

            /** @brief 캐시를 엔진 수명 범위가 만들 수 있게 종류 이름을 쥔 껍데기입니다(`ScopeOwned` 는 기본 생성한다). */
            struct TextureInstanceCacheHolder
            {
                TextureInstanceCache _cache{ "SpriteTextureInstance" };
            };

            /** @brief (머티리얼, 텍스처) 인스턴스 표의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(처음 쓸 때 만들어진다). */
            inline static ScopeOwned<TextureInstanceCacheHolder> s_textureInstanceCache{};

            /** @brief (머티리얼, 텍스처) 인스턴스 표입니다. */
            static TextureInstanceCache& getTextureInstanceCache()
            {
                TextureInstanceCacheHolder* const pHolder = s_textureInstanceCache.getOrCreate();
                SW_ASSERT( pHolder != nullptr ); // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다 — 캐시를 쓸 엔진이 없다
                return pHolder->_cache;
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

namespace sw
{
    SW_BUILT_IN_SHARED_ASSET_CACHE( SpriteTextureInstance, &SpriteRenderUtil::getTextureInstanceCache );
} // namespace sw
