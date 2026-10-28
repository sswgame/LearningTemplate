#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/MatchModeCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

namespace sw
{
    SW_LOG_CALLER( "MatchModeCatalog" );

    bool MatchModeCatalog::parseCompactMode( string_view text, MatchModeDefinition& outMode, string& outError )
    {
        constexpr const utf8* kShapeError = "must be <id>:<team count>x<team size>";
        const size_t          colon       = text.find( ':' );
        if ( colon == string_view::npos || colon == 0 )
        {
            outError = kShapeError;
            return false;
        }
        const string_view shape = text.substr( colon + 1 );
        const size_t      cross = shape.find( 'x' );
        if ( cross == string_view::npos )
        {
            outError = kShapeError;
            return false;
        }
        MatchModeDefinition mode;
        const bool          bNumbers = StringUtil::parseInt( shape.substr( 0, cross ), mode._teamCount ) && StringUtil::parseInt( shape.substr( cross + 1 ), mode._teamSize );
        if ( bNumbers == false )
        {
            outError = kShapeError;
            return false;
        }
        // 팀 수 · 인원의 범위는 `MatchModeDefinition` 의 `PROPERTY` 하나가 원본이다. 손으로 다시 적지 않는다.
        // 범위를 볼 수 없는 프로세스(타입 레지스트리 없음)는 그 사실이 이유로 나온다 — 올바른 형식을 형식 오류로 오해하지 않게 한다.
        outError = ConfigRangeUtil::describeRangeErrors( mode );
        if ( outError.empty() == false )
            return false;
        outMode._modeID    = string( text.substr( 0, colon ) );
        outMode._teamCount = mode._teamCount;
        outMode._teamSize  = mode._teamSize;
        return true;
    }

    uint32 MatchModeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 `MatchModeDefinition` 의 초기값, 범위는 그 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Mode" ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadMode( node, sourceName ) )
                ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Mode> entries", sourceName );
        return loadedCount;
    }

    bool MatchModeCatalog::loadMode( const XMLNode& node, string_view sourceName )
    {
        // 정의의 id 필드(`_modeID`)은 와이어 · 저장 이름이라 데이터의 `id` 속성은 손으로 읽는다.
        MatchModeDefinition mode;
        bool                bValid = GameDataXML::readFields( node, mode, sourceName, { "id" } );
        const utf8*         pID    = node.findAttribute( "id" );
        if ( StringUtil::isNullOrEmpty( pID ) )
        {
            SW_LOG_ERROR( "%#: <Mode> needs id=\"...\"", sourceName );
            return false;
        }
        mode._modeID = pID;
        if ( bValid )
            _listMode.push_back( std::move( mode ) );
        return bValid;
    }
} // namespace sw
