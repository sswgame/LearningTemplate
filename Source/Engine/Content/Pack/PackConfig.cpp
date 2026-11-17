#include "pch.h"

#include "Engine/Content/Pack/PackConfig.h"

namespace sw
{
    void PackCookRules::mergeGameRules( string_view gamePackRoot, const GameCookRules& gameRules )
    {
        string root{ gamePackRoot };
        while ( root.empty() == false && root.back() == '/' )
        {
            root.pop_back();
        }
        for ( const PackAlwaysCookRule& gameRule : gameRules._listAlwaysCook )
        {
            // 게임 규칙의 패턴은 그 게임 팩 루트 기준이다 — 루트를 붙여 다른 게임 팩의 파일에 맞지 않게 한다.
            PackAlwaysCookRule scoped = gameRule;
            for ( string& pattern : scoped._listPattern )
            {
                pattern = root + "/" + pattern;
            }
            bool bReplaced = false;
            for ( PackAlwaysCookRule& existing : _listAlwaysCook )
            {
                if ( existing._name != scoped._name )
                    continue;
                existing  = scoped; // 같은 이름은 게임 쪽이 이긴다
                bReplaced = true;
                break;
            }
            if ( bReplaced == false )
                _listAlwaysCook.push_back( std::move( scoped ) );
        }
        for ( const PackEntryIDField& gameField : gameRules._listEntryIDField )
        {
            bool bReplaced = false;
            for ( PackEntryIDField& existing : _listEntryIDField )
            {
                if ( existing._field != gameField._field )
                    continue;
                existing  = gameField;
                bReplaced = true;
                break;
            }
            if ( bReplaced == false )
                _listEntryIDField.push_back( gameField );
        }
    }
} // namespace sw
