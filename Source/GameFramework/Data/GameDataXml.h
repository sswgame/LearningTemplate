/**
 * @file GameDataXml.h
 * @brief 게임 데이터 XML 의 공통 읽기 — 문서 열기 · 루트 찾기 · id 확인 · 숫자 목록("1 0.5 0.2") · 토큰 목록("Spring,Fall")입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlDocument;
    class XmlNode;

    /**
     * @struct GameDataXml
     * @brief 카탈로그(작물 · 무기 · 블록 · 코스터 레이아웃 · 몬스터 …)가 저마다 적던 "문서를 열고 루트를 찾고 실패하면 경고" 를 한 곳에 둡니다.
     * @details 경고 문구가 같아야 로그를 걸러 보기 쉽습니다. 경로로 읽으면 @p outSourceName 이 절대 경로가 되어 이후 경고가 어느 파일인지 가리킵니다.
     */
    struct SW_GF_API GameDataXml
    {
        /** @brief 리소스 경로의 XML 을 읽고 @p pRootName 루트를 찾습니다. 실패하면 경고하고 false 입니다. */
        [[nodiscard]] static bool loadRoot( XmlDocument& doc, string_view path, const utf8* pRootName, XmlNode& outRoot, string& outSourceName );
        /** @brief XML 글을 읽고 루트를 찾습니다(시험 · 에디터 미리보기). 실패하면 경고하고 false 입니다. */
        [[nodiscard]] static bool parseRoot( XmlDocument& doc, string_view xmlText, string_view sourceName, const utf8* pRootName, XmlNode& outRoot );
        /** @brief `id` 속성을 돌려줍니다. 없거나 비면 "<원소> without an id - skipped" 를 경고하고 nullptr 입니다. */
        static const utf8* findRequiredId( const XmlNode& node, string_view sourceName );

        /**
         * @brief 쉼표 · 공백 · 탭 · 세미콜론으로 나뉜 실수를 최대 @p maxCount 개 읽습니다. 읽은 개수입니다.
         * @details 못 읽은 토큰은 그 칸을 건드리지 않고 넘깁니다(호출부가 기본값을 채워 둔다).
         */
        static uint32 parseFloats( string_view text, float32* pOutValue, uint32 maxCount );
        /** @brief "r g b [a]" 같은 네 실수입니다. 빠진 성분은 @p fallback 의 것입니다. */
        static float4 parseFloat4( string_view text, const float4& fallback );
        /** @brief "x y z" 같은 세 실수입니다. */
        static float3 parseFloat3( string_view text, const float3& fallback );

        /**
         * @brief @p separators 의 아무 글자로 나뉜 비지 않은 토큰마다 @p callback( string_view ) 을 부릅니다(할당 없음).
         * @code
         *     GameDataXml::forEachToken( "Spring, Fall", ",; ", [&]( string_view token ) { ... } );
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
