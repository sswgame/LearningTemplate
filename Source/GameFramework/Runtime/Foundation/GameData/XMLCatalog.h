/**
 * @file XMLCatalog.h
 * @brief XML 데이터 표(카탈로그)의 공개 로드 함수(`loadFromAsset`, `loadFromXMLText`)와 공유 창구(`acquire`, `request`)를 한 번만 적는 CRTP 베이스를 정의합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct XMLCatalogLoader
     * @brief `XMLCatalog` 가 부르는 문서 열기와 루트 찾기의 몸통입니다. 템플릿 밖에 두어서 카탈로그 헤더가 XML 헤더를 끌어오지 않게 합니다.
     * @details 실패 경고는 `XMLReadUtil::loadRootOrWarn` · `parseRootOrWarn` 이 낸다. 루트 읽기가 0 개이거나 false 이면 실패이다.
     *          `GameDataXML::loadFile` · `loadText`(다른 카탈로그를 함께 받는 문맥 버전 포함)도 이 몸통을 쓴다 — 문서 열기 · 루트 찾기는 여기 하나다.
     */
    struct SW_GF_API XMLCatalogLoader
    {
        using ReadRootFunction = bool ( * )( void* pCatalog, const XMLNode& root, string_view sourceName );

        /** @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 @p pReadRoot 에 넘깁니다. */
        [[nodiscard]] static bool loadFile( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName );
        /** @brief XML 문자열 버전입니다. 테스트와 에디터 미리보기가 씁니다. */
        [[nodiscard]] static bool loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName );

        /** @brief 루트 읽기가 읽은 항목 수를 성공 여부로 바꿉니다. 0 이면 실패입니다. */
        static constexpr bool isLoaded( uint32 loadedCount ) { return loadedCount > 0; }
        /** @brief 루트 읽기의 성공 여부를 그대로 반환합니다. */
        static constexpr bool isLoaded( bool bLoaded ) { return bLoaded; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLCatalog
     * @brief 카탈로그가 물려받으면 `loadFromAsset( path )` 와 `loadFromXMLText( xmlText, sourceName )` 가 생깁니다.
     * @details 카탈로그는 루트 원소 이름 `kXMLRootName` 과 루트 읽기 `loadRoot( const XMLNode&, string_view sourceName )` 만 두면 된다.
     *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
     *          둘 다 비공개로 두고 베이스를 friend 로 연다.
     *          다른 카탈로그를 함께 받는 루트 읽기(기술, 코스터 설계)는 `GameDataXML::loadFile` 의 문맥 버전을 그대로 쓴다.
     *          경로마다 한 번 읽어 나눠 쓰려면 `acquire` · `request` 를 쓴다. 그 카탈로그의 .cpp 가 공유 캐시를 인스턴스화해야 한다(`SW_MODULE_SHARED_ASSET_CACHE`, 헤더에 `extern template`).
     *          받은 카탈로그는 `const` 이고 실행 상태는 쓰는 쪽이 따로 가진다. 다시 읽으면 쥔 쪽은 옛 객체를 끝까지 쥐고(`Detach`) `getReloadCount` 가 바뀌면 다시 받는다.
     * @code
     *     class SW_GF_API CropCatalog : public XMLCatalog<CropCatalog>
     *     {
     *         friend class XMLCatalog<CropCatalog>;
     *     ...
     *     private:
     *         static constexpr const utf8* kXMLRootName = "CropCatalog";
     *         uint32 loadRoot( const XMLNode& root, string_view sourceName );
     *     };
     * @endcode
     */
    template <typename TCatalog>
    class XMLCatalog
    {
    public:
        /** @brief 리소스 경로의 XML 을 읽습니다. 문서나 루트가 없거나 읽은 것이 없으면 false 입니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID )
        {
            return XMLCatalogLoader::loadFile( this, &XMLCatalog::readRoot, assetID, TCatalog::kXMLRootName );
        }
        /** @brief 경로의 카탈로그를 나눠 받습니다(같은 경로는 같은 객체). 읽을 수 없으면 nullptr 입니다. 워커에서 불러도 됩니다. */
        [[nodiscard]] static shared_ptr<const TCatalog> acquire( const AssetID& assetID ) { return SharedAssetCache<TCatalog>::acquire( assetID.view() ); }
        /** @brief `acquire` 의 비동기 판입니다. 핸들이 살아 있는 동안 준비된 카탈로그도 삽니다. */
        [[nodiscard]] static SharedAssetHandle<TCatalog> request( const AssetID& assetID ) { return SharedAssetCache<TCatalog>::request( assetID.view() ); }
        /** @brief 공유 카탈로그를 다시 읽은 횟수입니다. 쓰는 쪽은 값이 바뀌면 다시 받습니다. */
        [[nodiscard]] static uint32 getReloadCount() { return SharedAssetCache<TCatalog>::getReloadCount(); }

        /** @brief XML 문자열을 읽습니다. 테스트와 에디터 미리보기가 씁니다. @p sourceName 은 경고에 쓰는 이름입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} )
        {
            return XMLCatalogLoader::loadText( this, &XMLCatalog::readRoot, xmlText, sourceName, TCatalog::kXMLRootName );
        }

    private:
        [[nodiscard]] static bool readRoot( void* pCatalog, const XMLNode& root, string_view sourceName )
        {
            TCatalog& catalog = static_cast<TCatalog&>( *static_cast<XMLCatalog*>( pCatalog ) );
            return XMLCatalogLoader::isLoaded( catalog.loadRoot( root, sourceName ) );
        }
    };
} // namespace sw
