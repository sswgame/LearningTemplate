#include "pch.h"

#include "Engine/Content/Pack/PackConfig.h"

namespace sw
{
    namespace
    {
        struct PackConfigInternal
        {
            /**
             * @brief 게임 @p root 의 규칙 하나(@p gameRule)를 @p inoutList 에 합칩니다. 이름(@p getName)이 같은 엔진 규칙은 지우지 않고 그 게임 팩을 빼고,
             *        게임 규칙은 그 게임 팩에만 닿게 덧붙인다. 같은 게임이 같은 이름을 이미 합쳤으면 그것을 덮는다.
             */
            template <typename RuleType, typename NameGetter>
            static void mergeScoped( vector<RuleType>& inoutList, RuleType gameRule, const string& root, NameGetter&& getName )
            {
                for ( RuleType& existing : inoutList )
                {
                    if ( getName( existing ) != getName( gameRule ) )
                        continue;
                    if ( existing._scope._packRoot == root )
                    {
                        gameRule._scope._packRoot = root;
                        existing                  = std::move( gameRule ); // 같은 게임이 다시 합친다
                        return;
                    }
                    if ( existing._scope._packRoot.empty() )
                        existing._scope._listExcludedPackRoot.push_back( root ); // 엔진 규칙은 이 게임 팩에서만 물러난다
                }
                gameRule._scope._packRoot = root;
                inoutList.push_back( std::move( gameRule ) );
            }
        };
    } // namespace
} // namespace sw

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
            // 게임 규칙의 패턴은 그 게임 팩 루트 기준이다 — 루트를 붙인다(닿는 범위는 `_scope` 가 따로 막는다).
            PackAlwaysCookRule scoped = gameRule;
            for ( string& pattern : scoped._listPattern )
            {
                pattern = root + "/" + pattern;
            }
            PackConfigInternal::mergeScoped( _listAlwaysCook, std::move( scoped ), root, []( const PackAlwaysCookRule& rule ) -> const string&
            { return rule._name; } );
        }
        for ( const PackEntryIDField& gameField : gameRules._listEntryIDField )
        {
            PackConfigInternal::mergeScoped( _listEntryIDField, gameField, root, []( const PackEntryIDField& field ) -> const string&
            { return field._field; } );
        }
    }
} // namespace sw
