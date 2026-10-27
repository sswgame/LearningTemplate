/**
 * @file GameModuleTypeNames.h
 * @brief 게임 모듈이 선언한 리플렉션 타입 이름 — 이 실행 파일이 게임 모듈을 올리지 않아 모르는 타입이지만 게임 팩 자원에는 놓여 있는 것들입니다.
 * @details EngineTest 는 게임 모듈을 링크하지 않는다. 게임 팩의 씬 · 프리팹이 게임 컴포넌트를 담고 있으면 읽을 때 "알 수 없는 형식 — 빈 컴포넌트로 보존" Warning 이 난다(엔진의 정상 동작).
 *          그 Warning 만 의도된 줄로 표시하고(`test::ScopedDefensiveTestLog` 의 거름 글), 게임 소스에 선언되지 않은 이름(오타 · 지운 타입)은 표식 없이 남아 그대로 실패한다.
 *          상용 엔진도 모듈이 없는 곳에서 에셋을 열면 같은 종류의 경고(언리얼의 없는 클래스 · 유니티의 Missing Script)를 내고 데이터는 보존한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"

#include <cctype>

namespace gamemoduletypes
{
    /**
     * @brief 게임 모듈(`Source/Games` 아래 헤더)이 `REFLECT` 로 선언한 타입 이름을 모읍니다(설정 구조체도 게임 모듈 타입입니다).
     * @param resourceRoot `Resource/` 의 경로입니다. 저장소 루트는 그 윗 폴더입니다.
     */
    inline void collectTypeNames( const sw::string& resourceRoot, sw::vector<sw::string>& outListTypeName )
    {
        outListTypeName.clear();
        const sw::string       repositoryRoot = sw::FileUtil::getDirectoryPart( sw::FileUtil::trimTrailingSlashes( resourceRoot ) );
        sw::vector<sw::string> listHeader;
        if ( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( repositoryRoot, "Source/Games" ), ".h", listHeader, true ) == false )
            return;
        for ( const sw::string& headerPath : listHeader )
        {
            sw::string text;
            if ( sw::FileUtil::readTextFile( headerPath, text ) == false )
                continue;
            for ( size_t reflectPos = text.find( "REFLECT(" ); reflectPos != sw::string::npos; reflectPos = text.find( "REFLECT(", reflectPos + 1 ) )
            {
                // 다음 `class` · `struct` 중 가까운 것이 이 REFLECT 의 타입이다.
                const size_t classPos   = text.find( "class ", reflectPos );
                const size_t structPos  = text.find( "struct ", reflectPos );
                const bool   bStruct    = structPos != sw::string::npos && ( classPos == sw::string::npos || structPos < classPos );
                const size_t keywordPos = bStruct ? structPos : classPos;
                if ( keywordPos == sw::string::npos )
                    break;
                size_t nameStart = keywordPos + ( bStruct ? 7 : 6 );
                while ( nameStart < text.size() && text.compare( nameStart, 3, "SW_" ) == 0 )
                {
                    // 내보내기 매크로(`SW_API` 등)를 건너뛴다.
                    nameStart = text.find( ' ', nameStart );
                    if ( nameStart == sw::string::npos )
                        break;
                    ++nameStart;
                }
                if ( nameStart == sw::string::npos )
                    break;
                size_t nameEnd = nameStart;
                while ( nameEnd < text.size() && ( std::isalnum( static_cast<uint8>( text[nameEnd] ) ) != 0 || text[nameEnd] == '_' ) )
                {
                    ++nameEnd;
                }
                if ( nameEnd > nameStart )
                    outListTypeName.push_back( text.substr( nameStart, nameEnd - nameStart ) );
            }
        }
    }

    /** @brief 컴포넌트 로더가 모르는 타입을 보존할 때 내는 Warning 의 문구 조각입니다(`'<타입>' ... of unknown type '<타입>'`). 게임 모듈 타입마다 하나입니다. */
    inline sw::vector<sw::string> makeUnknownTypeNeedles( const sw::vector<sw::string>& listTypeName )
    {
        sw::vector<sw::string> listNeedle;
        listNeedle.reserve( listTypeName.size() );
        for ( const sw::string& typeName : listTypeName )
        {
            listNeedle.push_back( "of unknown type '" + typeName + "'" );
        }
        return listNeedle;
    }
} // namespace gamemoduletypes
