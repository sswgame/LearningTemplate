/**
 * @file GameDataXML.h
 * @brief 게임 데이터 XML 의 공통 읽기를 정의합니다.
 * @details 문서 열기, 루트 찾기, id 확인, 숫자 목록("1 0.5 0.2")과 토큰 목록("Spring,Fall") 읽기를 제공한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{

    /**
     * @struct GameDataXML
     * @brief 카탈로그(작물, 무기, 블록, 코스터 레이아웃, 몬스터 등)가 저마다 적던 "문서를 열고, 루트를 찾고, 실패하면 경고한다"를 한 곳에 모읍니다.
     * @details 경고 문구가 같아야 로그를 걸러 보기 쉽다.
     *          경로로 읽으면 @p outSourceName 이 절대 경로가 되어서, 이후 경고가 어느 파일인지 가리킨다.
     */
    struct SW_GF_API GameDataXML
    {
        /** @brief 리소스 경로의 XML 을 읽고 @p pRootName 루트를 찾습니다. 실패하면 경고하고 false 를 반환합니다. */
        [[nodiscard]] static bool loadRoot( XMLDocument& doc, string_view path, const utf8* pRootName, XMLNode& outRoot, string& outSourceName );
        /** @brief XML 문자열을 읽고 루트를 찾습니다. 테스트와 에디터 미리보기가 씁니다. 실패하면 경고하고 false 를 반환합니다. */
        [[nodiscard]] static bool parseRoot( XMLDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XMLNode& outRoot );
        /**
         * @brief 리소스 경로의 XML 을 열어 @p pRootName 루트를 카탈로그의 루트 읽기(@p pLoadRoot)에 넘깁니다. 문서나 루트가 없거나 읽은 것이 없으면 false 입니다.
         * @details 카탈로그마다 같았던 `loadFromResource` 의 몸통이다. 카탈로그는 이 한 줄만 두면 된다.
         * @code
         *     bool CropCatalog::loadFromResource( string_view path ) { return GameDataXML::loadFile( *this, &CropCatalog::loadRoot, path, "CropCatalog" ); }
         * @endcode
         *          루트 읽기는 읽은 항목 수(`uint32`, 0 이면 실패)나 성공 여부(`bool`)를 반환한다.
         *          멤버 포인터를 카탈로그 안에서 넘기므로 루트 읽기는 비공개로 둘 수 있다.
         */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), string_view path, const utf8* pRootName )
        {
            XMLDocument doc;
            XMLNode     root;
            string      sourceName;
            return loadRoot( doc, path, pRootName, root, sourceName ) && isLoaded( ( loader.*pLoadRoot )( root, sourceName ) );
        }
        /** @brief `loadFile` 의 XML 문자열 버전입니다. 테스트와 에디터 미리보기(`loadFromXMLText`)가 씁니다. */
        template <typename TLoader, typename TResult>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, string_view ), string_view xmlText, string_view sourceName,
                                            const utf8* pRootName )
        {
            XMLDocument doc;
            XMLNode     root;
            return parseRoot( doc, xmlText, sourceName, pRootName, root ) && isLoaded( ( loader.*pLoadRoot )( root, sourceName ) );
        }

        /** @brief `loadFile` 에 다른 카탈로그(기술, 코스터 설계)를 함께 넘기는 버전입니다. 루트 읽기가 `( root, context, sourceName )` 를 받습니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadFile( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            string_view path, const utf8* pRootName )
        {
            XMLDocument doc;
            XMLNode     root;
            string      sourceName;
            return loadRoot( doc, path, pRootName, root, sourceName ) && isLoaded( ( loader.*pLoadRoot )( root, context, sourceName ) );
        }
        /** @brief `loadText` 에 다른 카탈로그를 함께 넘기는 버전입니다. */
        template <typename TLoader, typename TResult, typename TContext>
        [[nodiscard]] static bool loadText( TLoader& loader, TResult ( TLoader::*pLoadRoot )( const XMLNode&, const TContext&, string_view ), const TContext& context,
                                            string_view xmlText, string_view sourceName, const utf8* pRootName )
        {
            XMLDocument doc;
            XMLNode     root;
            return parseRoot( doc, xmlText, sourceName, pRootName, root ) && isLoaded( ( loader.*pLoadRoot )( root, context, sourceName ) );
        }

        /** @brief `id` 속성을 반환합니다. 없거나 비면 "<원소> without an id - skipped" 를 경고하고 nullptr 를 반환합니다. */
        static const utf8* findRequiredID( const XMLNode& node, string_view sourceName );

        /**
         * @brief 쉼표, 공백, 탭, 세미콜론으로 나뉜 실수를 최대 @p maxCount 개 읽고 읽은 개수를 반환합니다.
         * @details 못 읽은 토큰은 그 칸을 건드리지 않고 넘어간다. 호출부가 기본값을 채워 두어야 한다.
         */
        static uint32 parseFloats( string_view text, float32* pOutValue, uint32 maxCount );
        /** @brief "r g b [a]" 같은 네 실수를 읽습니다. 빠진 성분은 @p fallback 의 것을 씁니다. */
        static float4 parseFloat4( string_view text, const float4& fallback );
        /** @brief "x y z" 같은 세 실수를 읽습니다. */
        static float3 parseFloat3( string_view text, const float3& fallback );

        /** @brief 루트 읽기가 읽은 항목 수를 성공 여부로 바꿉니다. 0 이면 실패입니다. */
        static constexpr bool isLoaded( uint32 loadedCount ) { return loadedCount > 0; }
        /** @brief 루트 읽기의 성공 여부를 그대로 반환합니다. */
        static constexpr bool isLoaded( bool bLoaded ) { return bLoaded; }

        /**
         * @brief @p separators 의 아무 글자로 나뉜 비지 않은 토큰마다 @p callback( string_view ) 을 부릅니다. 할당하지 않습니다.
         * @code
         *     GameDataXML::forEachToken( "Spring, Fall", ",; ", [&]( string_view token ) { ... } );
         * @endcode
         */

        template <typename TCallback>
        static void forEachToken( string_view text, string_view separators, TCallback&& callback )
        {
            size_t tokenStart = 0;
            while ( tokenStart < text.size() )
            {
                size_t tokenEnd = text.find_first_of( separators, tokenStart );
                if ( tokenEnd == string_view::npos )
                    tokenEnd = text.size();
                if ( tokenEnd > tokenStart )
                    callback( text.substr( tokenStart, tokenEnd - tokenStart ) );
                tokenStart = tokenEnd + 1;
            }
        }
    };
} // namespace sw
