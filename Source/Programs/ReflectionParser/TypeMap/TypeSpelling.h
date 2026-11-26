/**
 * @file TypeSpelling.h
 * @brief ReflectionParser 의 형식 철자 다루기(`<>` 밖 쉼표 분할 · 마지막 범위 이름 · 바깥 템플릿 이름)
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TypeSpelling — 템플릿 인자 토큰 분할 · 범위 이름
    // ------------------------------------------------------------------------------
    /** @brief 형식 철자 · 범위 이름 다루기(`<>` 밖 쉼표 분할 · 마지막 범위 이름 · 바깥 템플릿 이름) */
    struct TypeSpelling
    {
        /** @brief `<>` · `()` 밖의 `,` 만 분할하고 각 토큰을 trim 합니다(`map<K, V>` · builtins 의 Layout 칸 `f32(_x,_y)` 가 한 토큰). */
        static vector<string> splitCommaRespectingAngles( string_view inner )
        {
            vector<string> listResult;
            int32          depth      = 0;
            size_t         tokenStart = 0;

            for ( size_t index = 0; index < inner.size(); ++index )
            {
                const utf8 character = inner[index];
                if ( character == '<' || character == '(' )
                    ++depth;
                else if ( character == '>' || character == ')' )
                    --depth;

                if ( character == ',' && depth == 0 )
                {
                    if ( index > tokenStart )
                    {
                        string_view token = StringUtil::trim( inner.substr( tokenStart, index - tokenStart ) );
                        if ( token.empty() == false )
                            listResult.emplace_back( token );
                    }
                    tokenStart = index + 1;
                }
            }
            if ( inner.size() > tokenStart )
            {
                string_view token = StringUtil::trim( inner.substr( tokenStart ) );
                if ( token.empty() == false )
                    listResult.emplace_back( token );
            }
            return listResult;
        }

        /** @brief FQN(예: "sw::EState::Idle")에서 말단 식별자 이름("Idle")을 추출합니다. */
        static string_view scopeLeaf( string_view fqn )
        {
            string_view  name = StringUtil::trim( fqn );
            const size_t last = name.rfind( "::" );
            if ( last != string_view::npos && last + 2 < name.size() )
                name = name.substr( last + 2 );
            return name;
        }

        /**
         * @brief 타입 표기의 바깥 템플릿 이름입니다(`const sw::unordered_map<K, V>` → "unordered_map"). 템플릿이 아니면 빈 값입니다.
         * @details 컨테이너 규칙은 이것과 같아야 맞는다 — 표기 어디에든 규칙 이름이 들어 있으면 맞다고 보면 `TextureAsset` ·
         *          `Offset2D` 에 set, `Bitmap` 에 map, `Playlist` 에 list 가 들어 있어 그 프로퍼티가 `SetWrapper` · `MapWrapper` 로 나가
         *          생성 파일 안에서 빌드가 깨진다(컨테이너 원소 타입도 같다).
         */
        static string_view outerTemplateName( string_view typeSpelling )
        {
            const size_t open = typeSpelling.find( '<' );
            if ( open == string_view::npos )
                return {};
            string_view head = StringUtil::trim( typeSpelling.substr( 0, open ) );
            for ( const string_view qualifier : { string_view{ "const " }, string_view{ "volatile " } } )
            {
                while ( StringUtil::startsWith( head, qualifier ) )
                {
                    head = StringUtil::trim( head.substr( qualifier.size() ) );
                }
            }
            return scopeLeaf( head );
        }
    };
} // namespace sw
