#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/ContentFolderWatch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ParkLayout.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"

namespace sw::editor
{
    ParkLayoutPreview::ParkLayoutPreview()
        : _model{}
        , _layoutWatch{}
        , _coasterWatch{}
        , _layoutPath{}
        , _watchedLayoutPath{}
        , _watchedCoasterPath{}
        , _bFolderChanged{ SW_FALSE }
        , _bWarned{ SW_FALSE }
    {
    }

    void ParkLayoutPreview::syncWatch( const string& coasterPath )
    {
        if ( _watchedLayoutPath == _layoutPath && _watchedCoasterPath == coasterPath )
            return;
        _watchedLayoutPath  = _layoutPath;
        _watchedCoasterPath = coasterPath;
        _layoutWatch.reset();
        _coasterWatch.reset();
        IContentFolderWatch* pFolderWatch = findSubsystem<IContentFolderWatch>(); // 개발 도구가 건다 — 없으면 refresh 가 내용을 비교한다
        if ( pFolderWatch == nullptr )
            return;
        const IContentFolderWatch::ChangedDelegate onChanged  = IContentFolderWatch::ChangedDelegate::create<&ParkLayoutPreview::onDataFolderChanged>( this );
        const string                               layoutDir  = FileUtil::getDirectoryPart( _layoutPath );
        const string                               coasterDir = FileUtil::getDirectoryPart( coasterPath );
        _layoutWatch                                          = pFolderWatch->watchAssetFolder( layoutDir, { ".xml" }, onChanged );
        if ( coasterDir != layoutDir )
            _coasterWatch = pFolderWatch->watchAssetFolder( coasterDir, { ".xml" }, onChanged );
    }

    bool ParkLayoutPreview::refresh( bool bForce )
    {
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        string        coasterPath;
        string        error;
        if ( GamePackDataUtil::tryMakeKindPath( packRoot, ParkLayout::kDataKind, _layoutPath, error ) == false ||
             GamePackDataUtil::tryMakeKindPath( packRoot, CoasterLayoutCatalog::kDataKind, coasterPath, error ) == false )
        {
            if ( _bWarned == false )
                SW_LOG_ERROR_ID( "ParkLayoutPreview.ParkLayoutPreviewNo", "The park layout preview has no path in pack '%#': %#", packRoot.c_str(), error.c_str() );
            _bWarned    = SW_TRUE;
            _layoutPath = error;
            _model.setFailed();
            return false;
        }
        if ( AssetIO::hasAsset( AssetID{ _layoutPath } ) == false )
        {
            _model.setMissing(); // 이 게임 팩에는 공원 배치가 없다 — 패널이 그 상태를 보인다
            return false;
        }
        syncWatch( coasterPath );
        const bool bWatching   = _layoutWatch.isActive();
        const bool bNothingNew = bForce == false && bWatching && _bFolderChanged == false && _model.getState() != ParkLayoutState::Unread && _model.getState() != ParkLayoutState::Missing;
        if ( bNothingNew )
            return false;
        _bFolderChanged = SW_FALSE;

        string layoutText;
        string coasterText;
        if ( AssetIO::readTextAsset( AssetID{ _layoutPath }, layoutText ) == false || AssetIO::readTextAsset( AssetID{ coasterPath }, coasterText ) == false )
        {
            if ( _bWarned == false )
                SW_LOG_WARNING_ID( "ParkLayoutPreview.CouldNotReadPark", "%# or %# could not be read - the park layout preview is empty", _layoutPath.c_str(), coasterPath.c_str() );
            _bWarned = SW_TRUE;
            _model.setFailed();
            return false;
        }
        const bool bReread = _model.applyTexts( layoutText, coasterText, _layoutPath, coasterPath, bForce );
        if ( _model.getState() == ParkLayoutState::Loaded )
            _bWarned = SW_FALSE;
        return bReread;
    }

    void ParkLayoutPreview::appendSegments( vector<EditorWorldSegment>& outListSegment ) const
    {
        ParkLayoutColors colors{};
        colors._gate     = SW_THEME_COLOR( "Viewport", "ParkGate" ).toFloat4();
        colors._track    = SW_THEME_COLOR( "Viewport", "ParkTrack" ).toFloat4();
        colors._selected = SW_THEME_COLOR( "Base", "Highlight" ).toFloat4();
        _model.appendSegments( colors, outListSegment );
    }

    SW_EDITOR_SUBSYSTEM( ParkLayoutPreview, 9300 );
} // namespace sw::editor
