#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFavorite.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorDefaultAssets.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Localization/EditorDataText.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserViewModel.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 읽은 표와 지금 언어로 푼 이름입니다. 에디터 범위의 상태라 에디터 컨텍스트와 함께 처음으로 돌아간다. */
        struct ContentBrowserFavoriteState
        {
            vector<ContentBrowserFavorite> _listFavorite{};
            uint32                         _revision{ 0 };       ///< 푼 때의 현지화 글 판
            uint8                          _bLoaded{ SW_FALSE }; ///< 표를 읽었다(실패해도 다시 읽지 않는다 — 오류는 한 번)
        };
    } // namespace

    AbsolutePath ContentBrowserFavorite::findFolder() const
    {
        const string packRoot = _pack == ContentBrowserFavoritePack::Engine ? string( constant::path::kEnginePack ) : ContentBrowserViewModel::getGamePackRoot();
        return ContentBrowserViewModel::findAssetFile( FileUtil::joinPath( packRoot, _folder ) );
    }

    const vector<ContentBrowserFavorite>& ContentBrowserFavorite::getFavorites()
    {
        ContentBrowserFavoriteState& state    = getEditorState<ContentBrowserFavoriteState>();
        const uint32                 revision = EditorDataText::getRevision();
        if ( state._bLoaded == SW_TRUE && state._revision == revision )
            return state._listFavorite;
        ContentBrowserFavoriteTable table;
        const string&               tablePath = editor::getSubsystem<const EditorDefaultAssets>()._contentBrowserFavorites.get();
        if ( XMLSerializer::loadFromAsset( AssetID{ tablePath }, &table, *ContentBrowserFavoriteTable::StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "ContentBrowser.FavoritesCannotRead", "Content browser favorites '%#' could not be read strictly - the list is empty", tablePath );
            table._listFavorite.clear();
        }
        for ( ContentBrowserFavorite& favorite : table._listFavorite )
        {
            favorite._label = EditorDataText::localize( favorite._label );
        }
        state._listFavorite = std::move( table._listFavorite );
        state._revision     = revision;
        state._bLoaded      = SW_TRUE;
        return state._listFavorite;
    }
} // namespace sw::editor
