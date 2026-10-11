/**
 * @file XMLCatalog.h
 * @brief XML 데이터 표(카탈로그)의 공개 로드 함수 두 개(`loadFromResource`, `loadFromXMLText`)를 한 번만 적는 CRTP 베이스를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct XMLCatalogLoader
     * @brief `XMLCatalog` 가 부르는 문서 열기와 루트 찾기의 몸통입니다. 템플릿 밖에 두어서 카탈로그 헤더가 XML 헤더를 끌어오지 않게 합니다.
     * @details 실패 경고 문구는 `GameDataXML::loadRoot` 와 `parseRoot` 와 같다. 루트 읽기가 0 개이거나 false 이면 실패이다.
     */
    struct SW_GF_API XMLCatalogLoader
    {
        using ReadRootFunction = bool ( * )( void* pCatalog, const XMLNode& root, string_view sourceName );

        /** @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 @p pReadRoot 에 넘깁니다. */
        [[nodiscard]] static bool loadFile( void* pCatalog, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName );
        /** @brief XML 문자열 버전입니다. 테스트와 에디터 미리보기가 씁니다. */
        [[nodiscard]] static bool loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName );
    };
} // namespace sw

namespace sw
{
    /**
     * @class XMLCatalog
     * @brief 카탈로그가 물려받으면 `loadFromResource( path )` 와 `loadFromXMLText( xmlText, sourceName )` 가 생깁니다.
     * @details 카탈로그는 루트 원소 이름 `kXMLRootName` 과 루트 읽기 `loadRoot( const XMLNode&, string_view sourceName )` 만 두면 된다.
     *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
     *          둘 다 비공개로 두고 베이스를 friend 로 연다.
     *          다른 카탈로그를 함께 받는 루트 읽기(기술, 코스터 설계)는 `GameDataXML::loadFile` 의 문맥 버전을 그대로 쓴다.
     *          경로마다 한 번 읽어 나눠 쓰는 캐시 `GameDataCache<T>` 는 이 `loadFromResource` 를 부른다.
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
        [[nodiscard]] bool loadFromResource( string_view path ) { return XMLCatalogLoader::loadFile( this, &XMLCatalog::readRoot, path, TCatalog::kXMLRootName ); }
        /** @brief XML 문자열을 읽습니다. 테스트와 에디터 미리보기가 씁니다. @p sourceName 은 경고에 쓰는 이름입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} )
        {
            return XMLCatalogLoader::loadText( this, &XMLCatalog::readRoot, xmlText, sourceName, TCatalog::kXMLRootName );
        }

    private:
        [[nodiscard]] static bool readRoot( void* pCatalog, const XMLNode& root, string_view sourceName )
        {
            TCatalog& catalog = static_cast<TCatalog&>( *static_cast<XMLCatalog*>( pCatalog ) );
            return isLoaded( catalog.loadRoot( root, sourceName ) );
        }
        static constexpr bool isLoaded( uint32 loadedCount ) { return loadedCount > 0; }
        static constexpr bool isLoaded( bool bLoaded ) { return bLoaded; }
    };
} // namespace sw
