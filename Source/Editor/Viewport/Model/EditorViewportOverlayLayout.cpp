#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportOverlayLayout.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportOverlayLayoutInternal
        {
            /** @brief 바가 놓일 수 있는 남는 공간(뷰포트 − 바, 음수면 0)입니다. */
            static float2 computeFreeSpace( const float2& barSize, const float2& viewSize )
            {
                return float2{ MathUtil::max( viewSize._x - barSize._x, 0.0f ), MathUtil::max( viewSize._y - barSize._y, 0.0f ) };
            }

            static float32 toFraction( float32 position, float32 freeSpace ) { return freeSpace > 0.0f ? MathUtil::saturate( position / freeSpace ) : 0.0f; }
        };
    } // namespace

    void EditorViewportOverlayLayout::registerBar( const EditorOverlayBarState& defaultState )
    {
        bool bDefaultKnown = false;
        for ( EditorOverlayBarState& known : _listDefault )
        {
            if ( known._id == defaultState._id )
            {
                known         = defaultState;
                bDefaultKnown = true;
            }
        }
        if ( bDefaultKnown == false )
            _listDefault.push_back( defaultState );
        if ( findBar( defaultState._id ) == nullptr )
            _listBar.push_back( defaultState );
    }

    EditorOverlayBarState* EditorViewportOverlayLayout::findBar( string_view barID )
    {
        return VectorUtil::findBy<&EditorOverlayBarState::_id>( _listBar, barID );
    }

    const EditorOverlayBarState* EditorViewportOverlayLayout::findBar( string_view barID ) const
    {
        return VectorUtil::findBy<&EditorOverlayBarState::_id>( _listBar, barID );
    }

    void EditorViewportOverlayLayout::resetToDefault()
    {
        _listBar = _listDefault;
    }

    float2 EditorViewportOverlayLayout::computePosition( const EditorOverlayBarState& state, const float2& barSize, const float2& viewSize )
    {
        const float2 freeSpace = EditorViewportOverlayLayoutInternal::computeFreeSpace( barSize, viewSize );
        float2       fraction  = float2{ MathUtil::saturate( state._fraction._x ), MathUtil::saturate( state._fraction._y ) };
        switch ( state._dock )
        {
            case EditorOverlayDock::Top:
            {
                fraction._y = 0.0f;
                break;
            }
            case EditorOverlayDock::Bottom:
            {
                fraction._y = 1.0f;
                break;
            }
            case EditorOverlayDock::Left:
            {
                fraction._x = 0.0f;
                break;
            }
            case EditorOverlayDock::Right:
            {
                fraction._x = 1.0f;
                break;
            }
            case EditorOverlayDock::Free:
            {
                break;
            }
        }
        return float2{ freeSpace._x * fraction._x, freeSpace._y * fraction._y };
    }

    void EditorViewportOverlayLayout::applyDrop( EditorOverlayBarState& state, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance )
    {
        const float2  freeSpace = EditorViewportOverlayLayoutInternal::computeFreeSpace( barSize, viewSize );
        const float32 x         = MathUtil::clamp( position._x, 0.0f, freeSpace._x );
        const float32 y         = MathUtil::clamp( position._y, 0.0f, freeSpace._y );
        state._fraction         = float2{ EditorViewportOverlayLayoutInternal::toFraction( x, freeSpace._x ), EditorViewportOverlayLayoutInternal::toFraction( y, freeSpace._y ) };
        // 가장 가까운 가장자리 하나에 붙인다(거리가 같으면 위 · 아래가 먼저 — 가로 바가 흔하다).
        const float32 arrDistance[] = { y, freeSpace._y - y, x, freeSpace._x - x };
        uint32        nearest       = 0;
        for ( uint32 index = 1; index < 4; ++index )
        {
            if ( arrDistance[index] < arrDistance[nearest] )
                nearest = index;
        }
        state._dock = arrDistance[nearest] <= snapDistance ? static_cast<EditorOverlayDock>( nearest + 1 ) : EditorOverlayDock::Free;
    }

    bool EditorViewportOverlayLayout::isVertical( const EditorOverlayBarState& state )
    {
        return state._dock == EditorOverlayDock::Left || state._dock == EditorOverlayDock::Right;
    }

    bool EditorViewportOverlayLayout::saveFile( string_view absPath ) const
    {
        EditorViewportOverlayFile file;
        file._listBar = _listBar;
        return JSONSerializer::saveFile( absPath, &file, *EditorViewportOverlayFile::StaticType() );
    }

    ConfigReadResult EditorViewportOverlayLayout::loadFile( string_view absPath )
    {
        EditorViewportOverlayFile file;
        const ConfigReadResult    result = ConfigManager::readConfigFile( file, absPath );
        if ( result != ConfigReadResult::Loaded )
            return result;
        for ( EditorOverlayBarState& loaded : file._listBar )
        {
            loaded._fraction            = float2{ MathUtil::saturate( loaded._fraction._x ), MathUtil::saturate( loaded._fraction._y ) };
            EditorOverlayBarState* pBar = findBar( loaded._id );
            if ( pBar != nullptr )
                *pBar = loaded;
            else
                _listBar.push_back( loaded );
        }
        return result;
    }
} // namespace sw::editor
