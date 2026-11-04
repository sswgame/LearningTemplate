#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/FrameData.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct FrameDataInternal
        {
            static int32 computeAdvantage( const MoveFrameData& move, int32 contactFrame, bool bOnBlock )
            {
                const int32 stun = bOnBlock ? move._blockstun : move._hitstun;
                return stun - ( move.getTotalFrames() - contactFrame );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MoveFrameData::MoveFrameData()
        : _id{}
        , _name{}
        , _listHitbox{}
        , _listCancel{}
        , _damage{ 0.0f }
        , _chipDamage{ 0.0f }
        , _startup{ 1 }
        , _active{ 1 }
        , _recovery{ 0 }
        , _hitstun{ 0 }
        , _blockstun{ 0 }
        , _hitstop{ 0 }
        , _height{ AttackHeight::Mid }
        , _bLauncher{ SW_FALSE }
        , _bKnockdown{ SW_FALSE }
        , _bWallSplat{ SW_FALSE }
        , _bUnblockable{ SW_FALSE }
    {
    }

    MoveCancelWindow::MoveCancelWindow()
        : _listMoveID{}
        , _fromFrame{ 0 }
        , _toFrame{ 0 }
        , _bOnHitOnly{ SW_FALSE }
    {
    }

    // ------------------------------------------------------------------------------
    // MoveFrameData
    // ------------------------------------------------------------------------------
    int32 MoveFrameData::computeFrameAdvantage( bool bOnBlock ) const
    {
        return FrameDataInternal::computeAdvantage( *this, _startup, bOnBlock );
    }

    // ------------------------------------------------------------------------------
    // MoveCatalog
    // ------------------------------------------------------------------------------
    MoveCatalog::MoveCatalog()
    {
    }

    uint32 MoveCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Move" ); node; node = node.findNextSibling( "Move" ) )
        {
            // 적지 않은 칸은 MoveFrameData 의 초기값이다. 깨진 값 · 범위 밖 값 · 모르는 높이는 그 기술을 넣지 않는 오류다(자르지 않는다).
            MoveFrameData move;
            bool          bValid = XMLFields::readFields( node, move, sourceName );
            if ( move._id.empty() )
                continue;
            const utf8* const pID = move._id.c_str();
            if ( move._name.empty() )
                move._name = pID;
            const int32 lastFrame = move.getTotalFrames();

            for ( XMLNode child = node.findChild( "Hitbox" ); child; child = child.findNextSibling( "Hitbox" ) )
            {
                // 프레임 구간을 적지 않으면 기술의 활성 구간이다(이 기술의 데이터에서 온 값).
                MoveHitbox hitbox;
                hitbox._fromFrame = move._startup;
                hitbox._toFrame   = move.getLastActiveFrame();
                bool bHitboxValid = XMLFields::readFields( child, hitbox, sourceName ); // 크기 >= 0 · to >= from · 모양은 ENUM() 이름
                if ( bHitboxValid && hitbox._fromFrame > lastFrame )
                {
                    SW_LOG_ERROR_ID( "FrameData.MoveHitboxOutsideFrames", "%#: move '%#' has a hitbox outside its frames", sourceName, pID );
                    bHitboxValid = false;
                }
                if ( bHitboxValid )
                    move._listHitbox.push_back( hitbox );
                bValid = bHitboxValid && bValid;
            }

            for ( XMLNode child = node.findChild( "Cancel" ); child; child = child.findNextSibling( "Cancel" ) )
            {
                // 구간을 적지 않으면 활성 구간 다음부터 기술 끝까지다.
                MoveCancelWindow window;
                window._fromFrame = move.getLastActiveFrame() + 1;
                window._toFrame   = lastFrame;
                bool bWindowValid = XMLFields::readFields( child, window, sourceName, { "moves" } ); // to >= from 은 PROPERTY 가 본다
                StringUtil::forEachToken( child.getAttributeText( "moves" ), ",; \t", [&]( string_view token )
                { window._listMoveID.push_back( hashed_string( token ) ); } );
                if ( bWindowValid && ( window._listMoveID.empty() || window._toFrame < window._fromFrame ) )
                {
                    SW_LOG_ERROR_ID( "FrameData.MoveEmptyCancelWindow", "%#: move '%#' has an empty cancel window", sourceName, pID );
                    bWindowValid = false;
                }
                if ( bWindowValid )
                    move._listCancel.push_back( window );
                bValid = bWindowValid && bValid;
            }

            if ( bValid == false )
            {
                SW_LOG_ERROR_ID( "FrameData.MoveInvalidFieldsSkipped", "%#: move '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            (void)add( move );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING_ID( "FrameData.NoMoveEntries", "%#: no <Move> entries", sourceName );
        return loadedCount;
    }

    // ------------------------------------------------------------------------------
    // MoveTimeline
    // ------------------------------------------------------------------------------
    MoveTimeline::MoveTimeline()
        : _move{}
        , _frame{ 0 }
        , _hitstopRemaining{ 0 }
        , _bPlaying{ SW_FALSE }
        , _bContact{ SW_FALSE }
        , _bBlocked{ SW_FALSE }
    {
    }

    bool MoveTimeline::start( const MoveFrameData& move )
    {
        const bool bInvalid = move._startup < 1 || move._active < 1 || move._recovery < 0;
        if ( bInvalid )
        {
            SW_LOG_ERROR_ID( "FrameData.MoveTimelineRefusedMoveStartup", "MoveTimeline: refused move '%#' with startup %# active %# recovery %# - startup and active must be at least 1 and recovery must not be negative", move._id.c_str(),
                             move._startup, move._active, move._recovery );
            return false;
        }
        _move             = move;
        _frame            = 1;
        _hitstopRemaining = 0;
        _bPlaying         = SW_TRUE;
        _bContact         = SW_FALSE;
        _bBlocked         = SW_FALSE;
        return true;
    }

    void MoveTimeline::cancel()
    {
        _frame            = 0;
        _hitstopRemaining = 0;
        _bPlaying         = SW_FALSE;
        _bContact         = SW_FALSE;
        _bBlocked         = SW_FALSE;
    }

    bool MoveTimeline::restoreState( const MoveFrameData& move, int32 frame, int32 hitstopRemaining, bool bContact, bool bBlocked )
    {
        if ( hitstopRemaining < 0 || frame < 1 )
            return false;
        MoveTimeline restored;
        if ( restored.start( move ) == false )
            return false;
        if ( frame > restored._move.getTotalFrames() )
            return false;
        restored._frame            = frame;
        restored._hitstopRemaining = hitstopRemaining;
        restored._bContact         = bContact ? SW_TRUE : SW_FALSE;
        restored._bBlocked         = bContact && bBlocked ? SW_TRUE : SW_FALSE;
        *this                      = restored;
        return true;
    }

    bool MoveTimeline::advanceFrame()
    {
        if ( _bPlaying == SW_FALSE )
            return false;
        if ( _hitstopRemaining > 0 )
        {
            --_hitstopRemaining;
            return false;
        }
        ++_frame;
        if ( _frame > _move.getTotalFrames() )
            _bPlaying = SW_FALSE; // Finished — 프레임 번호는 남겨 둔다
        return true;
    }

    void MoveTimeline::registerContact( bool bBlocked )
    {
        if ( _bPlaying == SW_FALSE )
            return;
        _bContact = SW_TRUE;
        _bBlocked = bBlocked ? SW_TRUE : SW_FALSE;
        applyHitstop( _move._hitstop );
    }

    void MoveTimeline::applyHitstop( int32 frames )
    {
        _hitstopRemaining = MathUtil::max( _hitstopRemaining, frames );
    }

    MovePhase MoveTimeline::getPhase() const
    {
        if ( _frame <= 0 )
            return MovePhase::Idle;
        if ( _frame > _move.getTotalFrames() )
            return MovePhase::Finished;
        if ( _frame < _move._startup )
            return MovePhase::Startup;
        if ( _frame <= _move.getLastActiveFrame() )
            return MovePhase::Active;
        return MovePhase::Recovery;
    }

    uint32 MoveTimeline::collectActiveHitboxes( vector<MoveHitbox>& outListHitbox ) const
    {
        outListHitbox.clear();
        if ( _bPlaying == SW_FALSE )
            return 0;
        for ( const MoveHitbox& hitbox : _move._listHitbox )
        {
            if ( _frame >= hitbox._fromFrame && _frame <= hitbox._toFrame )
                outListHitbox.push_back( hitbox );
        }
        return static_cast<uint32>( outListHitbox.size() );
    }

    bool MoveTimeline::canCancelInto( const hashed_string& moveID ) const
    {
        if ( _bPlaying == SW_FALSE )
            return false;
        for ( const MoveCancelWindow& window : _move._listCancel )
        {
            if ( _frame < window._fromFrame || _frame > window._toFrame )
                continue;
            if ( window._bOnHitOnly == SW_TRUE && _bContact == SW_FALSE )
                continue;
            for ( const hashed_string& candidate : window._listMoveID )
            {
                if ( candidate == moveID )
                    return true;
            }
        }
        return false;
    }

    int32 MoveTimeline::computeFrameAdvantage( bool bOnBlock ) const
    {
        return FrameDataInternal::computeAdvantage( _move, _frame, bOnBlock );
    }

    GuardOutcome MoveTimeline::computeGuardOutcome( AttackHeight height, GuardStance stance, bool bUnblockable )
    {
        switch ( stance )
        {
            case GuardStance::None:
            {
                return GuardOutcome::Hit;
            }
            case GuardStance::Standing:
            {
                if ( height == AttackHeight::High || height == AttackHeight::Mid )
                    return bUnblockable ? GuardOutcome::Hit : GuardOutcome::Blocked;
                return GuardOutcome::Hit; // 하단 · 잡기
            }
            case GuardStance::Crouching:
            {
                if ( height == AttackHeight::High || height == AttackHeight::Throw )
                    return GuardOutcome::Evaded;
                if ( height == AttackHeight::Low )
                    return bUnblockable ? GuardOutcome::Hit : GuardOutcome::Blocked;
                return GuardOutcome::Hit; // 중단
            }
        }
        return GuardOutcome::Hit;
    }

    void MoveTimeline::writeState( Archive& outArchive ) const
    {
        outArchive << _bPlaying;
        if ( _bPlaying == SW_FALSE )
            return;
        StateArchiveUtil::writeName( outArchive, _move._id );
        outArchive << _frame;
        outArchive << _hitstopRemaining;
        outArchive << _bContact;
        outArchive << _bBlocked;
    }

    bool MoveTimeline::readState( Archive& archive, const MoveCatalog& catalog )
    {
        uint8 bPlaying = SW_FALSE;
        archive >> bPlaying;
        if ( archive.isError() || bPlaying > SW_TRUE )
            return false;
        if ( bPlaying == SW_FALSE )
        {
            cancel();
            return true;
        }
        hashed_string moveID;
        int32         frame            = 0;
        int32         hitstopRemaining = 0;
        uint8         bContact         = SW_FALSE;
        uint8         bBlocked         = SW_FALSE;
        if ( StateArchiveUtil::readName( archive, moveID ) == false )
            return false;
        archive >> frame;
        archive >> hitstopRemaining;
        archive >> bContact;
        archive >> bBlocked;
        const MoveFrameData* pMove = catalog.find( moveID );
        if ( archive.isError() || pMove == nullptr )
            return false;
        return restoreState( *pMove, frame, hitstopRemaining, bContact == SW_TRUE, bBlocked == SW_TRUE );
    }
} // namespace sw
