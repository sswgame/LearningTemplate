#include "pch.h"

#include "Editor/Viewport/Model/SceneViewOverlayLayoutStore.h"

#include "Editor/Common/Config/EditorPaths.h"

namespace sw::editor
{
    SubsystemInitResult SceneViewOverlayLayoutStore::initializeSubsystem()
    {
        // 저장된 값을 먼저 읽고 등록한다 — 등록은 저장된 바를 덮지 않고 기본값만 적는다.
        const AbsolutePath path = EditorPaths::resolveEditorStateFile( kFileName );
        // 처음엔 파일이 없다(Missing). 깨진 파일은 키와 함께 오류를 남기고 기본 배치로 둔다(저장하면 다시 쓴다).
        const bool bInvalidOverlayFile = path.empty() == false && _layout.loadFromFile( path.str() ) == ConfigReadResult::Invalid;
        if ( bInvalidOverlayFile )
            SW_LOG_WARNING_ID( "SceneViewOverlayLayoutStore.ViewportOverlayLayoutNot", "Viewport overlay layout is not valid - the default layout is used until it is saved again: %#", path.c_str() );
        for ( const SceneViewOverlayBarDesc& desc : kArrBar )
        {
            EditorOverlayBarState state{};
            state._id       = desc._pID;
            state._dock     = desc._defaultDock;
            state._fraction = desc._defaultFraction;
            _layout.registerBar( state );
        }
        return SubsystemInitResult::Succeeded;
    }

    const SceneViewOverlayBarDesc* SceneViewOverlayLayoutStore::findBarDesc( string_view barID )
    {
        for ( const SceneViewOverlayBarDesc& desc : kArrBar )
        {
            if ( barID == desc._pID )
                return &desc;
        }
        return nullptr;
    }

    void SceneViewOverlayLayoutStore::save() const
    {
        const AbsolutePath path = EditorPaths::resolveEditorStateFile( kFileName );
        if ( path.empty() )
            return;
        if ( _layout.saveToFile( path ) == false )
            SW_LOG_WARNING_ID( "SceneViewOverlayLayoutStore.CouldNotSaveViewport", "Could not save the viewport overlay layout to '%#'", path.c_str() );
    }

    void SceneViewOverlayLayoutStore::resetToDefault()
    {
        _layout.resetToDefault();
        save();
    }

    bool SceneViewOverlayLayoutStore::setBarVisible( string_view barID, bool bVisible )
    {
        EditorOverlayBarState* pState = _layout.findBar( barID );
        if ( pState == nullptr )
            return false;
        pState->_bVisible = bVisible;
        save();
        return true;
    }

    bool SceneViewOverlayLayoutStore::setBarCollapsed( string_view barID, bool bCollapsed )
    {
        EditorOverlayBarState* pState = _layout.findBar( barID );
        if ( pState == nullptr )
            return false;
        pState->_bCollapsed = bCollapsed;
        save();
        return true;
    }

    bool SceneViewOverlayLayoutStore::dropBar( string_view barID, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance )
    {
        EditorOverlayBarState* pState = _layout.findBar( barID );
        if ( pState == nullptr )
            return false;
        EditorViewportOverlayLayout::applyDrop( *pState, position, barSize, viewSize, snapDistance );
        save();
        return true;
    }
} // namespace sw::editor
