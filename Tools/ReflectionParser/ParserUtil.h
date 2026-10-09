/**
 * @file ParserUtil.h
 * @brief ReflectionParser 경로 조합·공통 토큰 분할
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) ParserUtil — emit 경로 조합 · 템플릿 인자 토큰 분할
    // ------------------------------------------------------------------------------
    /** @brief 생성 경로 조합(.gen.cpp / .gen.h)과 `<>` 밖 쉼표 분할 */
    struct ParserUtil
    {
        /**
         * @brief 출력 디렉터리 + stem + 확장자(.gen.cpp / .gen.h)로 생성 경로를 만듭니다.
         * @note FileUtil::normalizePath 는 Windows 에서 소문자로 바꾸므로, CMake OUTPUT 과 맞추려고 쓰지 않습니다.
         */
        static string makeGeneratedPath( const string& outputDir, const string& sourceFilePath,
                                         const string_view extension )
        {
            string fileName = FileUtil::removeExtension( FileUtil::getFileNamePart( sourceFilePath ) );
            fileName += extension;
            return FileUtil::joinPath( outputDir, fileName );
        }

        /**
         * @brief 소스 헤더를 clang --include 루트 기준 include 문자열로 바꿉니다.
         */
        static string makeHeaderIncludePath( const string& sourceFilePath, const vector<string>& listIncludePath )
        {
            const string sourceSep = FileUtil::normalizeSeparators( sourceFilePath );
            for ( const string& includeRoot : listIncludePath )
            {
                const string rootSep  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( includeRoot ) );
                const string rootNorm = FileUtil::normalizePath( rootSep );
                const string srcNorm  = FileUtil::normalizePath( sourceSep );
                if ( rootNorm.empty() || FileUtil::startsWithPathComponent( srcNorm, rootNorm ) == false )
                    continue;
                if ( srcNorm.size() == rootNorm.size() )
                    continue;
                return FileUtil::suffixAfterPathComponent( sourceSep, rootSep );
            }
            return FileUtil::getFileNamePart( sourceFilePath );
        }

        /** @brief `<>` 밖의 `,` 만 분할하고 각 토큰을 trim 합니다. */
        static vector<string> splitCommaRespectingAngles( string_view inner )
        {
            vector<string> listResult;
            int32          depth      = 0;
            size_t         tokenStart = 0;

            for ( size_t index = 0; index < inner.size(); ++index )
            {
                const utf8 character = inner[index];
                if ( character == '<' )
                    ++depth;
                else if ( character == '>' )
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
         * @brief 타입 표기의 **바깥 템플릿 이름**입니다(`const sw::unordered_map<K, V>` → "unordered_map"). 템플릿이 아니면 빈 값입니다.
         * @details 컨테이너 규칙은 이것과 **같아야** 맞는다 — 표기 어디에든 규칙 이름이 들어 있으면 맞다고 보면 `TextureAsset` ·
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
