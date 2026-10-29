#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetFormat.h"

namespace sw::editor
{
    namespace
    {
        using AssetTypeList = EditorExtensionRegistry<EditorAssetTypeRegistration>;

        /** @brief 규칙 하나가 경로에 맞은 결과입니다. 맞지 않으면 `_pRule` 이 nullptr 입니다. */
        struct RuleMatch
        {
            const EditorAssetTypeRegistration* _pType{ nullptr };
            const EditorAssetMatchRule*        _pRule{ nullptr };
            size_t                             _suffixLength{ 0 }; ///< 맞은 접미사의 길이. 같은 우선순위에서 긴 쪽이 이긴다
        };

        struct EditorAssetTypeInternal
        {
            /** @brief 에디터가 여는 접미사의 역할인지 반환합니다. 쿠킹본 이름(`Cooked` · `CookedAlias`)은 에셋으로 열지 않습니다. */
            static bool isOpenableRole( AssetSuffixRole role ) { return role == AssetSuffixRole::Plain || role == AssetSuffixRole::CookSource; }

            /**
             * @brief 규칙 하나의 접미사를 하나씩 @p func 에 넘깁니다. 쿠킹 규칙과 엔진 종류 규칙은 엔진 정의에서 가져옵니다(정적 문자열).
             * @details 판정 · 패널 매핑 · 감시 확장자 · 임포트 확장자가 모두 이 순회 하나로 접미사를 읽는다. 목록을 규칙마다 만들어 두지 않는다.
             */
            template <typename Func>
            static void forEachRuleSuffix( const EditorAssetMatchRule& rule, Func&& func )
            {
                if ( rule._mode == EditorAssetMatchMode::CookableSource )
                {
                    vector<string_view> listSuffix{};
                    AssetCookPath::appendSourceSuffixes( *rule._pEngineType, listSuffix );
                    for ( const string_view suffix : listSuffix )
                    {
                        func( suffix );
                    }
                    return;
                }
                if ( rule._mode == EditorAssetMatchMode::EngineType )
                {
                    const AssetTypeDef& type = *rule._pEngineType;
                    for ( uint32 suffixIndex = 0; suffixIndex < type._suffixCount; ++suffixIndex )
                    {
                        if ( isOpenableRole( type._pSuffix[suffixIndex]._role ) )
                            func( type._pSuffix[suffixIndex]._text );
                    }
                    return;
                }
                for ( uint32 index = 0; index < rule._suffixCount; ++index )
                {
                    func( rule._pSuffix[index] );
                }
            }

            /** @brief 규칙이 경로에 맞으면 맞은 접미사의 길이를, 아니면 0 을 반환합니다. 쿠킹 규칙은 경로 길이를 씁니다(우선순위가 이미 가장 앞이다). */
            static size_t matchRule( const EditorAssetMatchRule& rule, string_view path )
            {
                if ( rule._mode == EditorAssetMatchMode::CookableSource )
                    return AssetCookPath::isCookableSource( path, *rule._pEngineType ) ? path.size() : 0;

                size_t matchedLength{ 0 };
                forEachRuleSuffix( rule, [&]( string_view suffix )
                {
                    const bool bMatched = rule._mode == EditorAssetMatchMode::Extension ? FileUtil::hasExtension( path, suffix ) : StringUtil::endsWith( path, suffix, true );
                    if ( bMatched && suffix.size() > matchedLength )
                        matchedLength = suffix.size();
                } );
                if ( matchedLength == 0 )
                    return 0;
                if ( rule._pfnAcceptPath != nullptr && rule._pfnAcceptPath( path ) == false )
                    return 0;
                return matchedLength;
            }

            /** @brief @p candidate 가 @p best 보다 앞서면 true 입니다. 우선순위 → 접미사 길이 → 종류 순서(먼저 본 것) 차례입니다. */
            static bool isBetterMatch( const EditorAssetMatchRule& candidate, size_t candidateLength, const RuleMatch& best )
            {
                if ( best._pRule == nullptr )
                    return true;
                if ( candidate._priority != best._pRule->_priority )
                    return candidate._priority < best._pRule->_priority;
                return candidateLength > best._suffixLength;
            }

            /** @brief 경로에 가장 앞서 맞는 규칙을 찾습니다. 맞는 것이 없으면 빈 결과입니다. */
            static RuleMatch findBestMatch( string_view path )
            {
                RuleMatch best{};
                if ( path.empty() )
                    return best;
                const uint32 typeCount = AssetTypeList::getCount();
                for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
                {
                    const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
                    for ( uint32 ruleIndex = 0; ruleIndex < registration._ruleCount; ++ruleIndex )
                    {
                        const EditorAssetMatchRule& rule = registration._pRule[ruleIndex];
                        // 이길 수 없는 규칙은 경로를 보지 않는다 — 우선순위가 뒤인 규칙이 대부분이다.
                        if ( best._pRule != nullptr && rule._priority > best._pRule->_priority )
                            continue;
                        const size_t matchedLength = matchRule( rule, path );
                        if ( matchedLength == 0 || isBetterMatch( rule, matchedLength, best ) == false )
                            continue;
                        best = RuleMatch{ &registration, &rule, matchedLength };
                    }
                }
                return best;
            }

            /** @brief 종류의 규칙 하나에라도 맞으면 true 입니다. */
            static bool matchesAnyRule( const EditorAssetTypeRegistration& registration, string_view path )
            {
                for ( uint32 ruleIndex = 0; ruleIndex < registration._ruleCount; ++ruleIndex )
                {
                    if ( matchRule( registration._pRule[ruleIndex], path ) != 0 )
                        return true;
                }
                return false;
            }

            static bool containsSuffix( const vector<string>& listSuffix, string_view suffix )
            {
                return VectorUtil::containsIf( listSuffix, [&]( const string& existing )
                { return StringUtil::equals( existing, suffix, true ); } );
            }

            /** @brief 규칙 하나의 접미사를 중복 없이 @p outListSuffix 에 추가합니다. */
            static void appendUniqueRuleSuffixes( const EditorAssetMatchRule& rule, vector<string>& outListSuffix )
            {
                forEachRuleSuffix( rule, [&]( string_view suffix )
                {
                    if ( containsSuffix( outListSuffix, suffix ) == false )
                        outListSuffix.push_back( string{ suffix } );
                } );
            }

            /** @brief 핫 리로드 대상 규칙(캐시나 임포터가 있다)이면 true 입니다. */
            static bool isReloadable( const EditorAssetMatchRule& rule ) { return rule._reload._pCacheKindName != nullptr || rule._reload._pfnImportSource != nullptr; }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorAssetMatchRule EditorAssetMatchRule::engineType( const AssetTypeDef& def, const AssetReloadRoute& reload )
    {
        EditorAssetMatchRule rule{};
        rule._mode        = EditorAssetMatchMode::EngineType;
        rule._priority    = EditorAssetMatchPriority::Suffix;
        rule._pEngineType = &def;
        rule._reload      = reload;
        return rule;
    }

    EditorAssetMatchRule EditorAssetMatchRule::cookableSource( const AssetTypeDef& def, const AssetReloadRoute& reload )
    {
        EditorAssetMatchRule rule{};
        rule._mode        = EditorAssetMatchMode::CookableSource;
        rule._priority    = EditorAssetMatchPriority::Path;
        rule._pEngineType = &def;
        rule._reload      = reload;
        return rule;
    }

    EditorAssetMatchRule EditorAssetMatchRule::suffixes( EditorAssetMatchMode mode, const string_view* pSuffix, uint32 count, EditorAssetMatchPriority priority, const AssetReloadRoute& reload )
    {
        SW_LOG_ASSERT( mode == EditorAssetMatchMode::EndsWith || mode == EditorAssetMatchMode::Extension, "suffix rules are EndsWith or Extension" );
        SW_LOG_ASSERT( pSuffix != nullptr && count > 0, "a suffix rule needs at least one suffix" );
        EditorAssetMatchRule rule{};
        rule._mode        = mode;
        rule._priority    = priority;
        rule._pSuffix     = pSuffix;
        rule._suffixCount = count;
        rule._reload      = reload;
        return rule;
    }

    bool EditorAssetTypeRegistry::matches( const EditorAssetType& kind, string_view path )
    {
        if ( path.empty() )
            return false;
        const EditorAssetTypeRegistration* pRegistration = findKindInfo( kind );
        return pRegistration != nullptr && EditorAssetTypeInternal::matchesAnyRule( *pRegistration, path );
    }

    bool EditorAssetTypeRegistry::matches( const EditorAssetType& kind, const utf8* pPath )
    {
        if ( pPath == nullptr )
            return false;
        return matches( kind, string_view{ pPath } );
    }

    bool EditorAssetTypeRegistry::matchesAny( string_view path )
    {
        return findKindInfoForPath( path ) != nullptr;
    }

    bool EditorAssetTypeRegistry::matchesOther( string_view path )
    {
        if ( path.empty() )
            return false;
        const uint32 typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            if ( registration._bOtherExcluded && EditorAssetTypeInternal::matchesAnyRule( registration, path ) )
                return false;
        }
        return true;
    }

    const EditorAssetTypeRegistration* EditorAssetTypeRegistry::findKindInfoForPath( string_view path )
    {
        return EditorAssetTypeInternal::findBestMatch( path )._pType;
    }

    EditorAssetType EditorAssetTypeRegistry::findKind( string_view path )
    {
        const EditorAssetTypeRegistration* pRegistration = findKindInfoForPath( path );
        return pRegistration != nullptr ? pRegistration->getType() : EditorAssetType{};
    }

    const EditorAssetTypeRegistration* EditorAssetTypeRegistry::findKindInfo( const EditorAssetType& kind )
    {
        if ( kind.isUnknown() )
            return nullptr;
        return AssetTypeList::find( kind.getID() );
    }

    uint32 EditorAssetTypeRegistry::getKindCount()
    {
        return AssetTypeList::getCount();
    }

    const EditorAssetTypeRegistration& EditorAssetTypeRegistry::getKindAt( uint32 index )
    {
        return AssetTypeList::getAt( index );
    }

    uint32 EditorAssetTypeRegistry::getGeneration()
    {
        return AssetTypeList::getList().getGeneration();
    }

    const utf8* EditorAssetTypeRegistry::getPanelTitle( const EditorAssetType& kind )
    {
        const EditorAssetTypeRegistration* pRegistration = findKindInfo( kind );
        const bool                         bHasPanel     = pRegistration != nullptr && pRegistration->_pPanelTitle != nullptr;
        return bHasPanel ? pRegistration->_pPanelTitle : "";
    }

    EditorAssetType EditorAssetTypeRegistry::findPanelKindForPath( string_view assetPath )
    {
        if ( assetPath.empty() )
            return EditorAssetType{};

        // 가장 긴 접미사가 이긴다. `.animgraph.xml` 이 `.json` 보다 구체적이다. 전용 패널이 있는 종류의 접미사만 본다.
        size_t                             bestLength{ 0 };
        const EditorAssetTypeRegistration* pBest{ nullptr };
        const uint32                       typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            if ( registration._pPanelTitle == nullptr )
                continue;
            for ( uint32 ruleIndex = 0; ruleIndex < registration._ruleCount; ++ruleIndex )
            {
                EditorAssetTypeInternal::forEachRuleSuffix( registration._pRule[ruleIndex], [&]( string_view suffix )
                {
                    if ( suffix.size() <= bestLength || StringUtil::endsWith( assetPath, suffix, true ) == false )
                        return;
                    bestLength = suffix.size();
                    pBest      = &registration;
                } );
            }
        }
        return pBest != nullptr ? pBest->getType() : EditorAssetType{};
    }

    void EditorAssetTypeRegistry::collectBrowserFilters( vector<EditorAssetBrowserFilter>& outListFilter )
    {
        outListFilter.clear();
        outListFilter.push_back( EditorAssetBrowserFilter{ "All", EditorAssetType{}, false } );
        const uint32 typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            outListFilter.push_back( EditorAssetBrowserFilter{ registration._pBrowserLabel, registration.getType(), false } );
        }
        outListFilter.push_back( EditorAssetBrowserFilter{ "Other", EditorAssetType{}, true } );
    }

    void EditorAssetTypeRegistry::collectNewAssetTemplates( vector<const EditorNewAssetTemplate*>& outListTemplate )
    {
        outListTemplate.clear();
        const uint32 typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            if ( registration._pNewAssetTemplate != nullptr )
                outListTemplate.push_back( registration._pNewAssetTemplate );
        }
    }

    const IEditorAssetTypeActions* EditorAssetTypeRegistry::findActionsForPath( string_view path )
    {
        const EditorAssetTypeRegistration* pRegistration = findKindInfoForPath( path );
        return pRegistration != nullptr ? pRegistration->_pActions : nullptr;
    }

    AssetReloadRoute EditorAssetTypeRegistry::findReloadRoute( string_view path )
    {
        // 판정과 같은 규칙이 이긴다. 이미지는 SpriteClip 이미지 규칙보다 앞선 Texture 규칙으로 간다.
        const RuleMatch best = EditorAssetTypeInternal::findBestMatch( path );
        return best._pRule != nullptr ? best._pRule->_reload : AssetReloadRoute{};
    }

    void EditorAssetTypeRegistry::appendReloadableSuffixes( vector<string>& outListSuffix )
    {
        const uint32 typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            for ( uint32 ruleIndex = 0; ruleIndex < registration._ruleCount; ++ruleIndex )
            {
                if ( EditorAssetTypeInternal::isReloadable( registration._pRule[ruleIndex] ) )
                    EditorAssetTypeInternal::appendUniqueRuleSuffixes( registration._pRule[ruleIndex], outListSuffix );
            }
        }
    }

    void EditorAssetTypeRegistry::appendSuffixes( const EditorAssetType& kind, vector<string>& outListSuffix )
    {
        const EditorAssetTypeRegistration* pRegistration = findKindInfo( kind );
        if ( pRegistration == nullptr )
            return;
        for ( uint32 ruleIndex = 0; ruleIndex < pRegistration->_ruleCount; ++ruleIndex )
        {
            EditorAssetTypeInternal::appendUniqueRuleSuffixes( pRegistration->_pRule[ruleIndex], outListSuffix );
        }
    }

    void EditorAssetTypeRegistry::appendImportExtensions( vector<string>& outListExtension )
    {
        const uint32 typeCount = AssetTypeList::getCount();
        for ( uint32 typeIndex = 0; typeIndex < typeCount; ++typeIndex )
        {
            const EditorAssetTypeRegistration& registration = AssetTypeList::getAt( typeIndex );
            if ( registration._bImportable == false )
                continue;
            for ( uint32 ruleIndex = 0; ruleIndex < registration._ruleCount; ++ruleIndex )
            {
                EditorAssetTypeInternal::appendUniqueRuleSuffixes( registration._pRule[ruleIndex], outListExtension );
            }
        }
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".json" ) == false )
            outListExtension.push_back( ".json" );
        if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".txt" ) == false )
            outListExtension.push_back( ".txt" );
    }
} // namespace sw::editor
