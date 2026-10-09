#include "pch.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameData" );

    namespace
    {
        struct FrameDataInternal
        {
            static HitboxShape parseShape( string_view text )
            {
                return StringUtil::equals( text, "Capsule", true ) ? HitboxShape::Capsule : HitboxShape::Box;
            }

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
    // ------------------------------------------------------------------------------
    // MoveFrameData
    // ------------------------------------------------------------------------------
    int32 MoveFrameData::computeFrameAdvantage( bool bOnBlock ) const { return FrameDataInternal::computeAdvantage( *this, _startup, bOnBlock ); }

    // ------------------------------------------------------------------------------
    // MoveCatalog
    // ------------------------------------------------------------------------------
    MoveCatalog::MoveCatalog()
        : _catalog{}
    {
    }

    void MoveCatalog::addMove( const MoveFrameData& move )
    {
        (void)_catalog.add( move ); // 빈 id 는 카탈로그가 거른다
    }

    AttackHeight MoveCatalog::parseAttackHeight( string_view text, AttackHeight fallback )
    {
        if ( StringUtil::equals( text, "High", true ) )
            return AttackHeight::High;
        if ( StringUtil::equals( text, "Mid", true ) )
            return AttackHeight::Mid;
        if ( StringUtil::equals( text, "Low", true ) )
            return AttackHeight::Low;
        if ( StringUtil::equals( text, "Throw", true ) )
            return AttackHeight::Throw;
        return fallback;
    }

    uint32 MoveCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Move" ); node; node = node.findNextSibling( "Move" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            MoveFrameData move;
            move._id              = hashed_string( pId );
            const utf8* pName     = node.findAttribute( "name" );
            move._name            = pName != nullptr ? pName : pId;
            move._startup         = MathUtil::max( 1, node.getAttributeInt( "startup", move._startup ) );
            move._active          = MathUtil::max( 1, node.getAttributeInt( "active", move._active ) );
            move._recovery        = MathUtil::max( 0, node.getAttributeInt( "recovery", move._recovery ) );
            move._damage          = node.getAttributeFloat( "damage", move._damage );
            move._chipDamage      = MathUtil::max( 0.0f, node.getAttributeFloat( "chip", move._chipDamage ) );
            move._hitstun         = MathUtil::max( 0, node.getAttributeInt( "hitstun", move._hitstun ) );
            move._blockstun       = MathUtil::max( 0, node.getAttributeInt( "blockstun", move._blockstun ) );
            move._hitstop         = MathUtil::max( 0, node.getAttributeInt( "hitstop", move._hitstop ) );
            move._height          = parseAttackHeight( node.getAttributeText( "height" ), move._height );
            move._bLauncher       = node.getAttributeBool( "launcher", false ) ? SW_TRUE : SW_FALSE;
            move._bKnockdown      = node.getAttributeBool( "knockdown", false ) ? SW_TRUE : SW_FALSE;
            move._bWallSplat      = node.getAttributeBool( "wallSplat", false ) ? SW_TRUE : SW_FALSE;
            move._bUnblockable    = node.getAttributeBool( "unblockable", false ) ? SW_TRUE : SW_FALSE;
            const int32 lastFrame = move.getTotalFrames();

            for ( XmlNode child = node.findChild( "Hitbox" ); child; child = child.findNextSibling( "Hitbox" ) )
            {
                MoveHitbox hitbox;
                hitbox._fromFrame = child.getAttributeInt( "from", move._startup );
                hitbox._toFrame   = child.getAttributeInt( "to", move.getLastActiveFrame() );
                hitbox._x         = child.getAttributeFloat( "x", 0.0f );
                hitbox._y         = child.getAttributeFloat( "y", 0.0f );
                hitbox._width     = MathUtil::max( 0.0f, child.getAttributeFloat( "w", 0.0f ) );
                hitbox._height    = MathUtil::max( 0.0f, child.getAttributeFloat( "h", 0.0f ) );
                hitbox._shape     = FrameDataInternal::parseShape( child.getAttributeText( "shape" ) );
                if ( hitbox._toFrame < hitbox._fromFrame || hitbox._fromFrame > lastFrame )
                {
                    SW_LOG_WARNING( "%#: move '%#' has a hitbox outside its frames - skipped", sourceName, pId );
                    continue;
                }
                move._listHitbox.push_back( hitbox );
            }

            for ( XmlNode child = node.findChild( "Cancel" ); child; child = child.findNextSibling( "Cancel" ) )
            {
                MoveCancelWindow window;
                window._fromFrame  = child.getAttributeInt( "from", move.getLastActiveFrame() + 1 );
                window._toFrame    = child.getAttributeInt( "to", lastFrame );
                window._bOnHitOnly = child.getAttributeBool( "onHit", false ) ? SW_TRUE : SW_FALSE;
                GameDataXml::forEachToken( child.getAttributeText( "moves" ), ",; \t", [&]( string_view token )
                { window._listMoveId.push_back( hashed_string( token ) ); } );
                if ( window._listMoveId.empty() || window._toFrame < window._fromFrame )
                {
                    SW_LOG_WARNING( "%#: move '%#' has an empty cancel window - skipped", sourceName, pId );
                    continue;
                }
                move._listCancel.push_back( window );
            }

            (void)_catalog.add( move );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Move> entries", sourceName );
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

    void MoveTimeline::start( const MoveFrameData& move )
    {
        _move             = move;
        _move._startup    = MathUtil::max( 1, _move._startup );
        _move._active     = MathUtil::max( 1, _move._active );
        _move._recovery   = MathUtil::max( 0, _move._recovery );
        _frame            = 1;
        _hitstopRemaining = 0;
        _bPlaying         = SW_TRUE;
        _bContact         = SW_FALSE;
        _bBlocked         = SW_FALSE;
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
        restored.start( move );
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

    void MoveTimeline::applyHitstop( int32 frames ) { _hitstopRemaining = MathUtil::max( _hitstopRemaining, frames ); }

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

    bool MoveTimeline::canCancelInto( const hashed_string& moveId ) const
    {
        if ( _bPlaying == SW_FALSE )
            return false;
        for ( const MoveCancelWindow& window : _move._listCancel )
        {
            if ( _frame < window._fromFrame || _frame > window._toFrame )
                continue;
            if ( window._bOnHitOnly == SW_TRUE && _bContact == SW_FALSE )
                continue;
            for ( const hashed_string& candidate : window._listMoveId )
            {
                if ( candidate == moveId )
                    return true;
            }
        }
        return false;
    }

    int32 MoveTimeline::computeFrameAdvantage( bool bOnBlock ) const { return FrameDataInternal::computeAdvantage( _move, _frame, bOnBlock ); }

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
        hashed_string moveId;
        int32         frame            = 0;
        int32         hitstopRemaining = 0;
        uint8         bContact         = SW_FALSE;
        uint8         bBlocked         = SW_FALSE;
        if ( StateArchiveUtil::readName( archive, moveId ) == false )
            return false;
        archive >> frame;
        archive >> hitstopRemaining;
        archive >> bContact;
        archive >> bBlocked;
        const MoveFrameData* pMove = catalog.findMove( moveId );
        if ( archive.isError() || pMove == nullptr )
            return false;
        return restoreState( *pMove, frame, hitstopRemaining, bContact == SW_TRUE, bBlocked == SW_TRUE );
    }
} // namespace sw
