/**
 * @file XMLCatalog.h
 * @brief XML 데이터 표(카탈로그)의 로드 함수(`loadFromAsset` — 종류의 로더만 부른다, `loadFromXMLText`)를 한 번만 적는 CRTP 베이스를 정의합니다.
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
    class XMLCatalogRoot;

    /**
     * @struct XMLCatalogLoader
     * @brief `XMLCatalog` 가 부르는 문서 열기와 루트 찾기의 몸통입니다. 템플릿 밖에 두어서 카탈로그 헤더가 XML 헤더를 끌어오지 않게 합니다.
     * @details 실패 경고는 `XMLReadUtil::loadRootOrWarn` · `parseRootOrWarn` 이 낸다. 루트 읽기가 0 개이거나 false 이면 실패이다.
     *          루트 읽기 뒤에 카탈로그 참조 규칙(데이터 표 `engine/settings/datacatalogreferences.xml` — `DataCatalogReferenceRules`)을 보고 필수 참조가 깨졌으면 실패다.
     *          쿠커가 팩에 담기 전에 같은 표로 같은 검사를 한다 — 에디터 · 쿠커 · 런타임이 같은 답을 낸다.
     *          `GameDataXML::loadFromAsset` · `loadFromXMLText`(다른 카탈로그를 함께 받는 문맥 버전 포함)도 이 몸통을 쓴다 — 문서 열기 · 루트 찾기는 여기 하나다.
     */
    struct SW_GF_API XMLCatalogLoader
    {
        using ReadRootFunction = bool ( * )( void* pCatalog, const XMLCatalogRoot& root, string_view sourceName );

        /** @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 @p pReadRoot 에 넘깁니다. */
        [[nodiscard]] static bool loadFromAsset( void* pCatalog, ReadRootFunction pReadRoot, const AssetID& assetID, const utf8* pRootName );
        /** @brief XML 문자열 버전입니다. 테스트와 에디터 미리보기가 씁니다. */
        [[nodiscard]] static bool loadFromXMLText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName );

        /** @brief 루트 읽기를 부르고 참조 규칙을 봅니다(둘 다 돈다 — 오류를 한 번에 알린다). 둘 다 통과해야 true 입니다. */
        [[nodiscard]] static bool readAndValidate( void* pCatalog, ReadRootFunction pReadRoot, const XMLNode& root, string_view sourceName );

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
     * @brief 카탈로그가 물려받으면 종류의 로더가 부르는 `loadFromAsset( path )` 와 공개 `loadFromXMLText( xmlText, sourceName )` 가 생깁니다.
     * @details 카탈로그는 루트 원소 이름 `kXMLRootName` 과 루트 읽기 `loadRoot( const XMLCatalogRoot&, string_view sourceName )` 만 두면 된다. 루트 읽기는 원소를 손으로 고르지 않고 `XMLCatalogElement` 표로 돌린다(`XMLCatalogTable.h`).
     *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
     *          둘 다 비공개로 두고 베이스를 friend 로 연다.
     *          다른 카탈로그를 함께 받는 루트 읽기(기술, 코스터 설계)는 `GameDataXML::loadFromAsset` 의 문맥 버전을 그대로 쓴다.
     *          **사본을 만들지 않는다.** 파일에서 읽는 길은 공유 캐시 하나다 — 그 카탈로그의 .cpp 가 캐시를 인스턴스화하고(`SW_MODULE_SHARED_ASSET_CACHE`, 헤더에 `extern template`) 쓰는 쪽은
     *          `SharedAssetCache<카탈로그>::acquire` · `request` 로 같은 경로의 한 벌을 쥔다. 그래서 `loadFromAsset` 은 비공개이고 종류의 로더(`AssetTypeLoader`)만 friend 다 —
     *          쓰는 쪽이 멤버 · 지역 변수로 사본을 읽으면 다시 읽기가 닿지 않고 같은 파일을 여러 번 읽는다(언리얼 `UDataTable` · 유니티 `ScriptableObject` 처럼 에셋 한 벌을 참조한다).
     *          창구를 이 베이스에 두지 않는다 — 내보내는 카탈로그가 베이스 멤버를 모두 인스턴스화해 캐시가 없는 카탈로그에서 링크가 깨진다.
     *          받은 카탈로그는 `const` 이고 실행 상태는 쓰는 쪽이 따로 가진다. 다시 읽기는 기본이 제자리(`InPlace`)라 쥔 쪽 모두가 다음 읽기부터 새 값을 본다 — 쥔 쪽은
     *          항목 포인터를 프레임을 넘어 들지 않고 id 로 다시 찾는다. 항목에서 지은 것이 있으면 `SharedAssetCache<카탈로그>::getReloadCount` 가 바뀔 때 다시 짓는다.
     *          테스트와 에디터 미리보기는 `loadFromXMLText` 로 자기 사본을 만든다(파일이 아니라 글이다).
     * @code
     *     class SW_GF_API CropCatalog : public XMLCatalog<CropCatalog>
     *     {
     *         friend class XMLCatalog<CropCatalog>;
     *     ...
     *     private:
     *         static constexpr const utf8* kXMLRootName = "CropCatalog";
     *         uint32 loadRoot( const XMLCatalogRoot& root, string_view sourceName );
     *     };
     * @endcode
     */
    template <typename TCatalog>
    class XMLCatalog
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief XML 문자열을 읽습니다. 테스트와 에디터 미리보기가 씁니다. @p sourceName 은 경고에 쓰는 이름입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} )
        {
            return XMLCatalogLoader::loadFromXMLText( this, &XMLCatalog::readRoot, xmlText, sourceName, TCatalog::kXMLRootName );
        }

    protected:
        /** @brief 리소스 경로의 XML 을 읽습니다. 문서나 루트가 없거나 읽은 것이 없으면 false 입니다. 종류의 로더(공유 캐시 · 스키마 검사)와 자기 읽기를 감싸는 파생만 부른다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID )
        {
            return XMLCatalogLoader::loadFromAsset( this, &XMLCatalog::readRoot, assetID, TCatalog::kXMLRootName );
        }

    private:
        [[nodiscard]] static bool readRoot( void* pCatalog, const XMLCatalogRoot& root, string_view sourceName )
        {
            TCatalog& catalog = static_cast<TCatalog&>( *static_cast<XMLCatalog*>( pCatalog ) );
            return XMLCatalogLoader::isLoaded( catalog.loadRoot( root, sourceName ) );
        }
    };
} // namespace sw
