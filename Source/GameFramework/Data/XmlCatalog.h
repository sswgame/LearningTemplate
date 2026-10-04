/**
 * @file XmlCatalog.h
 * @brief XML 데이터 표(카탈로그)의 공개 로드 창구 두 개(`loadFromResource` · `loadFromXmlText`)를 한 번만 적는 CRTP 베이스입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /**
     * @struct XmlCatalogLoader
     * @brief `XmlCatalog` 가 부르는 문서 열기 · 루트 찾기 몸통입니다 — 템플릿 밖에 두어 카탈로그 헤더가 XML 헤더를 끌어오지 않게 합니다.
     * @details 실패 경고 문구는 `GameDataXml::loadRoot` · `parseRoot` 와 같습니다(루트 읽기가 0 개 · false 이면 실패).
     */
    struct SW_GF_API XmlCatalogLoader
    {
        using ReadRootFunction = bool ( * )( void* pCatalog, const XmlNode& root, string_view sourceName );

        /** @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 @p pReadRoot 에 넘깁니다. */
        [[nodiscard]] static bool loadFile( void* pCatalog, ReadRootFunction pReadRoot, string_view path, const utf8* pRootName );
        /** @brief XML 글 판입니다(시험 · 에디터 미리보기). */
        [[nodiscard]] static bool loadText( void* pCatalog, ReadRootFunction pReadRoot, string_view xmlText, string_view sourceName, const utf8* pRootName );
    };
} // namespace sw

namespace sw
{
    /**
     * @class XmlCatalog
     * @brief 카탈로그가 물려받으면 `loadFromResource( path )` · `loadFromXmlText( xmlText, sourceName )` 가 생깁니다.
     * @details 카탈로그는 루트 원소 이름 `kXmlRootName` 과 루트 읽기 `loadRoot( const XmlNode&, string_view sourceName )`(읽은 항목 수 `uint32` —
     *          0 이면 실패 — 또는 성공 여부 `bool`)만 둡니다. 둘 다 비공개로 두고 베이스를 friend 로 엽니다. 다른 카탈로그를 함께 받는 루트 읽기
     *          (기술 · 코스터 설계)는 `GameDataXml::loadFile` 의 문맥 판을 그대로 씁니다. 경로마다 한 번 읽어 나눠 쓰는 캐시는 `GameDataCache<T>` 가
     *          이 `loadFromResource` 를 부릅니다.
     * @code
     *     class SW_GF_API CropCatalog : public XmlCatalog<CropCatalog>
     *     {
     *         friend class XmlCatalog<CropCatalog>;
     *     ...
     *     private:
     *         static constexpr const utf8* kXmlRootName = "CropCatalog";
     *         uint32 loadRoot( const XmlNode& root, string_view sourceName );
     *     };
     * @endcode
     */
    template <typename TCatalog>
    class XmlCatalog
    {
    public:
        /** @brief 리소스 경로의 XML 을 읽습니다. 문서 · 루트가 없거나 읽은 것이 없으면 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view path ) { return XmlCatalogLoader::loadFile( this, &XmlCatalog::readRoot, path, TCatalog::kXmlRootName ); }
        /** @brief XML 글을 읽습니다(시험 · 에디터 미리보기). @p sourceName 은 경고에 쓰는 이름입니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} )
        {
            return XmlCatalogLoader::loadText( this, &XmlCatalog::readRoot, xmlText, sourceName, TCatalog::kXmlRootName );
        }

    private:
        [[nodiscard]] static bool readRoot( void* pCatalog, const XmlNode& root, string_view sourceName )
        {
            TCatalog& catalog = static_cast<TCatalog&>( *static_cast<XmlCatalog*>( pCatalog ) );
            return isLoaded( catalog.loadRoot( root, sourceName ) );
        }
        static constexpr bool isLoaded( uint32 loadedCount ) { return loadedCount > 0; }
        static constexpr bool isLoaded( bool bLoaded ) { return bLoaded; }
    };
} // namespace sw
