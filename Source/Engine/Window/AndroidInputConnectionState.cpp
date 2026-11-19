#include "pch.h"

#include "Engine/Window/AndroidInputConnectionState.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    void AndroidInputConnectionState::setComposingText( string_view text, vector<AndroidTextAction>& outListAction )
    {
        if ( text.empty() && _composingText.empty() )
            return; // 조합이 없는데 빈 조합 — 낼 것이 없다
        _composingText.assign( text.data(), text.size() );
        outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Composition, _composingText } );
    }

    void AndroidInputConnectionState::finishComposingText( vector<AndroidTextAction>& outListAction )
    {
        if ( _composingText.empty() )
            return;
        outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Commit, _composingText } );
        outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Composition, string{} } );
        _composingText.clear();
    }

    void AndroidInputConnectionState::commitText( string_view text, vector<AndroidTextAction>& outListAction )
    {
        if ( _composingText.empty() == false )
        {
            outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Composition, string{} } );
            _composingText.clear();
        }
        if ( text.empty() == false )
            outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Commit, string( text ) } );
    }

    void AndroidInputConnectionState::deleteSurroundingText( const uint32 beforeLength, const uint32 afterLength, vector<AndroidTextAction>& outListAction )
    {
        for ( uint32 index = 0; index < MathUtil::min( beforeLength, kMaxDeleteKeys ); ++index )
        {
            outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::BackspaceKey, string{} } );
        }
        for ( uint32 index = 0; index < MathUtil::min( afterLength, kMaxDeleteKeys ); ++index )
        {
            outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::DeleteKey, string{} } );
        }
    }

    void AndroidInputConnectionState::cancelComposition( vector<AndroidTextAction>& outListAction )
    {
        if ( _composingText.empty() )
            return;
        outListAction.push_back( AndroidTextAction{ AndroidTextActionKind::Composition, string{} } );
        _composingText.clear();
    }
} // namespace sw
