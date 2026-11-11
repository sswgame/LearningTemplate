#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/FrameData.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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
        using Element                          = XMLCatalogElement<MoveCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&MoveCatalog::loadMove>( "Move" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool MoveCatalog::loadMove( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 MoveFrameData 의 초기값이다. 깨진 값 · 범위 밖 값 · 모르는 높이는 그 기술을 넣지 않는 오류다(자르지 않는다).
        MoveFrameData move;
        bool          bValid = XMLFields::readProperties( node, move, sourceName );
        if ( move._id.empty() )
            return false;
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
            bool bHitboxValid = XMLFields::readProperties( child, hitbox, sourceName ); // 크기 >= 0 · to >= from · 모양은 ENUM() 이름
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
            bool bWindowValid = XMLFields::readProperties( child, window, sourceName ); // to >= from 은 PROPERTY 가 본다
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
            return false;
        }
        (void)add( move );
        return true;
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

    GuardResult MoveTimeline::computeGuardResult( AttackHeight height, GuardStance stance, bool bUnblockable )
    {
        switch ( stance )
        {
            case GuardStance::None:
            {
                return GuardResult::Hit;
            }
            case GuardStance::Standing:
            {
                if ( height == AttackHeight::High || height == AttackHeight::Mid )
                    return bUnblockable ? GuardResult::Hit : GuardResult::Blocked;
                return GuardResult::Hit; // 하단 · 잡기
            }
            case GuardStance::Crouching:
            {
                if ( height == AttackHeight::High || height == AttackHeight::Throw )
                    return GuardResult::Evaded;
                if ( height == AttackHeight::Low )
                    return bUnblockable ? GuardResult::Hit : GuardResult::Blocked;
                return GuardResult::Hit; // 중단
            }
        }
        return GuardResult::Hit;
    }

    void MoveTimeline::serializeState( StateIO& io, const MoveCatalog* pCatalog )
    {
        uint8 bPlaying = _bPlaying;
        io.flag( bPlaying );
        if ( io.isOk() == false )
            return;
        if ( bPlaying == SW_FALSE )
        {
            if ( io.isReading() )
            {
                cancel();
            }
            return;
        }
        hashed_string moveID           = _move._id;
        int32         frame            = _frame;
        int32         hitstopRemaining = _hitstopRemaining;
        uint8         bContact         = _bContact;
        uint8         bBlocked         = _bBlocked;
        io.value( moveID );
        io.value( frame );
        io.value( hitstopRemaining );
        io.value( bContact );
        io.value( bBlocked );
        if ( io.isReading() && io.isOk() )
        {
            const MoveFrameData* pMove = pCatalog->find( moveID );
            io.check( pMove != nullptr && restoreState( *pMove, frame, hitstopRemaining, bContact == SW_TRUE, bBlocked == SW_TRUE ) );
        }
    }

} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MoveCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( MoveCatalog, MoveCatalog, SW_GF_API, MoveCatalog::kAssetType.withLoader( AssetTypeLoader::of<MoveCatalog>( MemoryTag::Game ) ) );
} // namespace sw
