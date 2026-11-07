#include "pch.h"

#include "Editor/Common/Input/EditorSearchSources.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    bool EditorSearchSources::matchesScattered( string_view text, string_view pattern )
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

    bool EditorSearchSources::containsIgnoreCase( string_view text, string_view pattern )
    {
        for ( size_t start = 0; start + pattern.size() <= text.size(); ++start )
        {
            if ( StringUtil::startsWith( text.substr( start ), pattern, true ) )
                return true;
        }
        return false;
    }

    void EditorSearchSources::appendCommands( vector<EditorSearchItem>& inoutListItem )
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

    void EditorSearchSources::appendPanels( vector<EditorSearchItem>& inoutListItem )
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

    void EditorSearchSources::appendSceneObjects( vector<EditorSearchItem>& inoutListItem )
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
            item._detail   = formatToString( "Scene GameObject (ID: %#)", pObj->getObjectID() );
            item._objectID = pObj->getObjectID();
            inoutListItem.push_back( std::move( item ) );
        } );
    }

    void EditorSearchSources::appendAssets( const vector<EditorAssetIndexEntry>& listEntry, vector<EditorSearchItem>& inoutListItem )
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

    void EditorSearchSources::filter( std::initializer_list<const vector<EditorSearchItem>*> listSource, string_view pattern, vector<const EditorSearchItem*>& outListFiltered )
    {
        outListFiltered.clear();
        vector<const EditorSearchItem*> listScattered;
        for ( const vector<EditorSearchItem>* pListItem : listSource )
            for ( const EditorSearchItem& item : *pListItem )
            {
                if ( pattern.empty() || containsIgnoreCase( item._title, pattern ) || containsIgnoreCase( item._category, pattern ) || containsIgnoreCase( item._detail, pattern ) )
                    outListFiltered.push_back( &item );
                else if ( matchesScattered( item._title, pattern ) || matchesScattered( item._category, pattern ) || matchesScattered( item._detail, pattern ) )
                    listScattered.push_back( &item );
            }
        outListFiltered.insert( outListFiltered.end(), listScattered.begin(), listScattered.end() );
    }

    void EditorSearchSources::execute( const EditorSearchItem& item )
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
