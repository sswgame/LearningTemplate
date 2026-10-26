#include "pch.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameData" );

    namespace
    {
        struct FrameDataInternal
        {
            /** @brief `shape` 를 읽습니다. 없으면 @p inoutShape 를 둔다(초기값 Box). 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool readShape( const XMLNode& node, HitboxShape& inoutShape )
            {
                const utf8* pText = node.findAttribute( "shape" );
                if ( pText == nullptr )
                    return true;
                if ( StringUtil::equals( pText, "Capsule" ) )
                    inoutShape = HitboxShape::Capsule;
                else if ( StringUtil::equals( pText, "Box" ) )
                    inoutShape = HitboxShape::Box;
                else
                    return false;
                return true;
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

    bool MoveCatalog::parseAttackHeight( string_view text, AttackHeight& outHeight )
    {
        if ( StringUtil::equals( text, "High" ) )
            outHeight = AttackHeight::High;
        else if ( StringUtil::equals( text, "Mid" ) )
            outHeight = AttackHeight::Mid;
        else if ( StringUtil::equals( text, "Low" ) )
            outHeight = AttackHeight::Low;
        else if ( StringUtil::equals( text, "Throw" ) )
            outHeight = AttackHeight::Throw;
        else
            return false;
        return true;
    }

    uint32 MoveCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Move" ); node; node = node.findNextSibling( "Move" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            MoveFrameData move;
            move._id          = hashed_string( pID );
            const utf8* pName = node.findAttribute( "name" );
            move._name        = pName != nullptr ? pName : pID;
            // 적지 않은 칸은 MoveFrameData 의 초기값이다. 깨진 값 · 범위 밖 값은 그 기술을 넣지 않는 오류다(자르지 않는다).
            constexpr int32   kAnyInt   = MathUtil::kMaxInt32;
            constexpr float32 kAnyFloat = MathUtil::kMaxFloat;
            bool              bValid    = GameDataXML::readOptionalNumber( node, "startup", sourceName, 1, kAnyInt, move._startup );
            bValid                      = GameDataXML::readOptionalNumber( node, "active", sourceName, 1, kAnyInt, move._active ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "recovery", sourceName, 0, kAnyInt, move._recovery ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "damage", sourceName, -kAnyFloat, kAnyFloat, move._damage ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "chip", sourceName, 0.0f, kAnyFloat, move._chipDamage ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "hitstun", sourceName, 0, kAnyInt, move._hitstun ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "blockstun", sourceName, 0, kAnyInt, move._blockstun ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "hitstop", sourceName, 0, kAnyInt, move._hitstop ) && bValid;
            bValid                      = GameDataXML::readOptionalFlag( node, "launcher", move._bLauncher ) && bValid;
            bValid                      = GameDataXML::readOptionalFlag( node, "knockdown", move._bKnockdown ) && bValid;
            bValid                      = GameDataXML::readOptionalFlag( node, "wallSplat", move._bWallSplat ) && bValid;
            bValid                      = GameDataXML::readOptionalFlag( node, "unblockable", move._bUnblockable ) && bValid;
            const utf8* pHeight         = node.findAttribute( "height" );
            if ( pHeight != nullptr && parseAttackHeight( pHeight, move._height ) == false )
            {
                SW_LOG_ERROR( "%#: move '%#' has an unknown height '%#' (High, Mid, Low, Throw)", sourceName, pID, pHeight );
                bValid = false;
            }
            const int32 lastFrame = move.getTotalFrames();

            for ( XMLNode child = node.findChild( "Hitbox" ); child; child = child.findNextSibling( "Hitbox" ) )
            {
                // 프레임 구간을 적지 않으면 기술의 활성 구간이다(이 기술의 데이터에서 온 값).
                MoveHitbox hitbox;
                hitbox._fromFrame = move._startup;
                hitbox._toFrame   = move.getLastActiveFrame();
                bool bHitboxValid = GameDataXML::readOptionalNumber( child, "from", sourceName, 0, kAnyInt, hitbox._fromFrame );
                bHitboxValid      = GameDataXML::readOptionalNumber( child, "to", sourceName, 0, kAnyInt, hitbox._toFrame ) && bHitboxValid;
                bHitboxValid      = GameDataXML::readOptionalNumber( child, "x", sourceName, -kAnyFloat, kAnyFloat, hitbox._x ) && bHitboxValid;
                bHitboxValid      = GameDataXML::readOptionalNumber( child, "y", sourceName, -kAnyFloat, kAnyFloat, hitbox._y ) && bHitboxValid;
                bHitboxValid      = GameDataXML::readOptionalNumber( child, "w", sourceName, 0.0f, kAnyFloat, hitbox._width ) && bHitboxValid;
                bHitboxValid      = GameDataXML::readOptionalNumber( child, "h", sourceName, 0.0f, kAnyFloat, hitbox._height ) && bHitboxValid;
                if ( FrameDataInternal::readShape( child, hitbox._shape ) == false )
                {
                    SW_LOG_ERROR( "%#: move '%#' has a hitbox with an unknown shape '%#' (Box, Capsule)", sourceName, pID, child.getAttributeText( "shape" ) );
                    bHitboxValid = false;
                }
                if ( bHitboxValid && ( hitbox._toFrame < hitbox._fromFrame || hitbox._fromFrame > lastFrame ) )
                {
                    SW_LOG_ERROR( "%#: move '%#' has a hitbox outside its frames", sourceName, pID );
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
                bool bWindowValid = GameDataXML::readOptionalNumber( child, "from", sourceName, 0, kAnyInt, window._fromFrame );
                bWindowValid      = GameDataXML::readOptionalNumber( child, "to", sourceName, 0, kAnyInt, window._toFrame ) && bWindowValid;
                bWindowValid      = GameDataXML::readOptionalFlag( child, "onHit", window._bOnHitOnly ) && bWindowValid;
                StringUtil::forEachToken( child.getAttributeText( "moves" ), ",; \t", [&]( string_view token )
                { window._listMoveID.push_back( hashed_string( token ) ); } );
                if ( bWindowValid && ( window._listMoveID.empty() || window._toFrame < window._fromFrame ) )
                {
                    SW_LOG_ERROR( "%#: move '%#' has an empty cancel window", sourceName, pID );
                    bWindowValid = false;
                }
                if ( bWindowValid )
                    move._listCancel.push_back( window );
                bValid = bWindowValid && bValid;
            }

            if ( bValid == false )
            {
                SW_LOG_ERROR( "%#: move '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            (void)add( move );
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
