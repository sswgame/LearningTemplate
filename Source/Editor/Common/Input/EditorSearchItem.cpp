#include "pch.h"

#include "Editor/Common/Input/EditorSearchItem.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/TypeInfo.h"

namespace sw::editor
{
    bool EditorSearchItem::matchesScattered( string_view text, string_view pattern )
    {
        if ( pattern.empty() )
            return true;
        size_t patternIndex = 0;
        for ( const utf8 ch : text )
        {
            if ( StringUtil::toLowerChar( ch ) != StringUtil::toLowerChar( pattern[patternIndex] ) )
                continue;
            ++patternIndex;
            if ( patternIndex == pattern.size() )
                return true;
        }
        return false;
    }

    bool EditorSearchItem::containsIgnoreCase( string_view text, string_view pattern )
    {
        for ( size_t start = 0; start + pattern.size() <= text.size(); ++start )
        {
            if ( StringUtil::startsWith( text.substr( start ), pattern, true ) )
                return true;
        }
        return false;
    }

    void EditorSearchItem::appendCommands( vector<EditorSearchItem>& inoutListItem )
    {
        for ( const EditorCommandDesc& desc : editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>().getCommands() )
        {
            if ( desc._bPaletteVisible == false )
                continue;
            EditorSearchItem item{};
            item._source   = EditorSearchSource::Command;
            item._category = desc._category;
            item._title    = desc._label;
            item._detail   = desc._detail;
            item._key      = desc._id;
            inoutListItem.push_back( std::move( item ) );
        }
    }

    void EditorSearchItem::appendPanels( vector<EditorSearchItem>& inoutListItem )
    {
        for ( const EditorPanelEntry& panel : editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>().getPanels() )
        {
            EditorSearchItem item{};
            item._source   = EditorSearchSource::Panel;
            item._category = "Panel";
            item._title    = string( "Open Panel: " ) + panel._title;
            item._detail   = "Editor Panel";
            item._key      = panel._id;
            inoutListItem.push_back( std::move( item ) );
        }
    }

    void EditorSearchItem::appendSceneObjects( vector<EditorSearchItem>& inoutListItem )
    {
        GameObjectManager* pObjects = editor::getActiveObjectManager();
        if ( pObjects == nullptr )
            return;
        // 값으로 반환하는 `getAllGameObjects()` 는 씬 전체를 복사한다. 순회만 하므로 복사하지 않는 쪽을 쓴다.
        pObjects->forEachGameObject( [&inoutListItem]( GameObject* pObj )
        {
            EditorSearchItem item{};
            item._source   = EditorSearchSource::SceneObject;
            item._category = "GameObject";
            item._title    = string{ pObj->getName().c_str() };
            StringBuilder<constant::buffer::kMax256> detailBuilder;
            detailBuilder.appendFormat( "Scene GameObject (ID: %#)", pObj->getObjectID() );
            item._detail   = string( detailBuilder.view() );
            item._objectID = pObj->getObjectID();
            // 칸 필터(type: · tag: · is:)가 볼 값 — 이미 있는 데이터(컴포넌트 유형 · 태그 · 활성)만 싣는다.
            for ( const Component* pComponent : pObj->getComponents() )
            {
                const TypeInfo* pType = pComponent != nullptr ? pComponent->getTypeInfo() : nullptr;
                if ( pType != nullptr )
                    item._listFacet.push_back( "type:" + string( pType->_name.c_str() ) );
            }
            for ( const TagID& tag : pObj->getTags().getTags() )
            {
                const utf8* pTagName = tag.getString();
                if ( pTagName != nullptr )
                    item._listFacet.push_back( string( "tag:" ) + pTagName );
            }
            item._listFacet.push_back( pObj->isActiveInHierarchy() ? "is:active" : "is:inactive" );
            inoutListItem.push_back( std::move( item ) );
        } );
    }

    EditorSearchQuery EditorSearchQuery::parse( string_view pattern )
    {
        EditorSearchQuery query;
        size_t            start = 0;
        while ( start < pattern.size() )
        {
            size_t end = pattern.find( ' ', start );
            if ( end == string_view::npos )
                end = pattern.size();
            const string_view word  = pattern.substr( start, end - start );
            const size_t      colon = word.find( ':' );
            if ( colon != string_view::npos && colon > 0 && colon + 1 < word.size() )
                query._listFacet.emplace_back( StringUtil::toLower( string( word.substr( 0, colon ) ).c_str() ), string( word.substr( colon + 1 ) ) );
            else if ( word.empty() == false )
                query._text += ( query._text.empty() ? "" : " " ) + string( word );
            start = end + 1;
        }
        return query;
    }

    bool EditorSearchQuery::matchesFacets( const EditorSearchItem& item ) const
    {
        for ( const auto& [field, value] : _listFacet )
        {
            bool bMatched = false;
            for ( const string& facet : item._listFacet )
            {
                const size_t colon = facet.find( ':' );
                if ( colon == string::npos || StringUtil::toLower( facet.substr( 0, colon ).c_str() ) != field )
                    continue;
                // 상태(`is:`)는 낱말 전체로 견준다 — `active` 가 `inactive` 안에도 들어 있다. 나머지 칸은 이어진 일부로 견준다(`type:mesh`).
                const string_view facetValue = string_view( facet ).substr( colon + 1 );
                const bool        bWhole     = field == "is";
                const bool        bHit       = bWhole ? StringUtil::toLower( string( facetValue ).c_str() ) == StringUtil::toLower( value.c_str() ) : EditorSearchItem::containsIgnoreCase( facetValue, value );
                if ( bHit )
                {
                    bMatched = true;
                    break;
                }
            }
            if ( bMatched == false )
                return false;
        }
        return true;
    }

    void EditorSearchItem::appendAssets( const vector<EditorAssetIndexEntry>& listEntry, vector<EditorSearchItem>& inoutListItem )
    {
        for ( const EditorAssetIndexEntry& entry : listEntry )
        {
            EditorSearchItem item{};
            item._source   = EditorSearchSource::Asset;
            item._kind     = entry._kind;
            item._category = entry._category;
            item._title    = entry._title;
            item._detail   = entry._detail;
            item._key      = entry._path;
            inoutListItem.push_back( std::move( item ) );
        }
    }

    void EditorSearchItem::filter( std::initializer_list<const vector<EditorSearchItem>*> listSource, string_view pattern, vector<const EditorSearchItem*>& outListFiltered )
    {
        outListFiltered.clear();
        vector<const EditorSearchItem*> listScattered;
        // 칸 필터(`type:` · `tag:` · `is:`)를 먼저 걸고, 남은 글로 기존 규칙(이어진 일치 먼저, 흩어진 일치 뒤)을 건다.
        const EditorSearchQuery query = EditorSearchQuery::parse( pattern );
        const string_view       text  = query._text;
        for ( const vector<EditorSearchItem>* pListItem : listSource )
            for ( const EditorSearchItem& item : *pListItem )
            {
                if ( query.matchesFacets( item ) == false )
                    continue;
                if ( text.empty() || containsIgnoreCase( item._title, text ) || containsIgnoreCase( item._category, text ) || containsIgnoreCase( item._detail, text ) )
                    outListFiltered.push_back( &item );
                else if ( matchesScattered( item._title, text ) || matchesScattered( item._category, text ) || matchesScattered( item._detail, text ) )
                    listScattered.push_back( &item );
            }
        outListFiltered.insert( outListFiltered.end(), listScattered.begin(), listScattered.end() );
    }

    void EditorSearchItem::execute( const EditorSearchItem& item )
    {
        switch ( item._source )
        {
            case EditorSearchSource::Command:
            {
                // 팔레트는 대상 없이 실행한다 — 꺼진 명령이면 아무 일도 없다
                (void)editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>().execute( item._key, EditorCommandTarget::none() );
                return;
            }
            case EditorSearchSource::Panel:
            {
                editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>().setPanelOpen( item._key.c_str(), true );
                return;
            }
            case EditorSearchSource::SceneObject:
            {
                GameObjectManager* pObjects = editor::getActiveObjectManager();
                GameObject*        pFound   = pObjects != nullptr ? pObjects->findGameObjectByID( item._objectID ) : nullptr;
                if ( pFound != nullptr )
                    EditorSceneCommands::select( pFound, SelectionMode::Replace );
                return;
            }
            case EditorSearchSource::Asset:
            {
                // 콘텐츠 브라우저 더블클릭과 같이 연다 — 도구 패널이 있는 종류는 그 패널을, 여는 동작이 있는 종류(씬)는 그 동작을.
                // 둘 다 없는 종류는 포커스만 옮긴다(인스펙터 · 콘텐츠 브라우저가 따라간다).
                if ( EditorAssetCommands::openPath( item._key ) == false )
                    EditorAssetCommands::focusPath( item._key );
                return;
            }
        }
    }
} // namespace sw::editor
