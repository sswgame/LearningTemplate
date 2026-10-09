#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntentHistory.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/BitStream.h"

namespace sw
{
    SW_LOG_CALLER( "ControlIntentHistory" );
} // namespace sw

namespace sw
{
    ControlIntentHistory::ControlIntentHistory()
        : _listTrack{}
        , _mapComponentIdToTrack{}
        , _capacityTicks{ kDefaultCapacityTicks }
    {
    }

    void ControlIntentHistory::initialize( int32 capacityTicks )
    {
        _capacityTicks = capacityTicks > 0 ? capacityTicks : 1;
        reset();
    }

    void ControlIntentHistory::reset()
    {
        _listTrack.clear();
        _mapComponentIdToTrack.clear();
    }

    void ControlIntentHistory::record( uint32 tick, const ComponentHandle& pawn, const hashed_string& pawnName, const ControlIntent& intent )
    {
        int32                                              trackIndex = -1;
        const unordered_map<uint64, int32>::const_iterator found      = _mapComponentIdToTrack.find( pawn.componentId() );
        if ( found != _mapComponentIdToTrack.end() )
        {
            trackIndex = found->second;
        }
        else
        {
            trackIndex = static_cast<int32>( _listTrack.size() );
            _listTrack.emplace_back();
            ControlIntentTrack& created = _listTrack.back();
            created._ring.initialize( _capacityTicks );
            created._pawnName = pawnName;
            created._pawn     = pawn;
            _mapComponentIdToTrack.emplace( pawn.componentId(), trackIndex );
        }
        ControlIntentTrack& track       = _listTrack[static_cast<size_t>( trackIndex )];
        const bool          bContinuing = track._ring.getCount() > 0 && tick == track._lastTick + 1;
        if ( bContinuing == false )
        {
            // 처음이거나 틱이 건너뛰었다 — 이어 붙일 수 없으니 그 틱부터 다시 쌓는다.
            track._ring.reset();
            track._firstTick = tick;
        }
        track._ring.acquire( tick ) = intent;
        track._lastTick             = tick;
    }

    void ControlIntentHistory::write( BitWriter& writer ) const
    {
        uint32 baseTick = 0;
        bool   bHasBase = false;
        for ( const ControlIntentTrack& track : _listTrack )
        {
            const uint32 oldestTick = computeOldestTick( track );
            if ( track._ring.getCount() > 0 && ( bHasBase == false || oldestTick < baseTick ) )
            {
                baseTick = oldestTick;
                bHasBase = true;
            }
        }
        writer.writeUint32( kMagic );
        writer.writeVarUint( kVersion );
        writer.writeVarUint( _listTrack.size() );
        for ( const ControlIntentTrack& track : _listTrack )
        {
            const uint32 oldestTick = track._ring.getCount() > 0 ? computeOldestTick( track ) : baseTick;
            const uint32 tickCount  = track._ring.getCount() > 0 ? track._lastTick - oldestTick + 1 : 0;
            if ( oldestTick != track._firstTick )
                SW_LOG_WARNING( "Intent track '%#' lost its first %# tick(s) to the ring - a replay from the scene start will not match", track._pawnName.c_str(),
                                oldestTick - track._firstTick );
            const string_view name = track._pawnName.c_str();
            writer.writeBlob( reinterpret_cast<const uint8*>( name.data() ), static_cast<int32>( name.size() ) );
            writer.writeVarUint( oldestTick - baseTick );
            writer.writeVarUint( tickCount );
            for ( uint32 tickOffset = 0; tickOffset < tickCount; ++tickOffset )
            {
                const ControlIntent* pIntent = track._ring.find( oldestTick + tickOffset );
                ( pIntent != nullptr ? *pIntent : ControlIntent{} ).write( writer );
            }
        }
    }

    bool ControlIntentHistory::read( BitReader& reader, string& outError )
    {
        reset();
        const uint32 magic   = reader.readUint32();
        const uint64 version = reader.readVarUint();
        if ( reader.hasOverflowed() || magic != kMagic )
        {
            outError = "not an intent recording (magic)";
            return false;
        }
        if ( version != kVersion )
        {
            outError = "intent recording version " + to_string( version ) + " is not " + to_string( kVersion );
            return false;
        }
        const uint64 trackCount = reader.readVarUint();
        if ( reader.hasOverflowed() || trackCount > static_cast<uint64>( kMaxTrackCount ) )
        {
            outError = "intent recording has a broken track count";
            return false;
        }
        vector<uint8> nameBytes;
        for ( uint64 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            if ( reader.readBlob( nameBytes, kMaxNameBytes ) == false )
            {
                outError = "intent recording has a broken pawn name";
                reset();
                return false;
            }
            const uint64 firstTick = reader.readVarUint();
            const uint64 tickCount = reader.readVarUint();
            // 의도 하나는 적어도 한 바이트 — 남은 바이트보다 많은 틱 수는 깨진 머리다(큰 고리를 잡기 전에 거른다).
            const bool bCountFits = tickCount <= static_cast<uint64>( reader.getBitsRemaining() / 8 ) && firstTick <= invalid_index::kUint32 - tickCount;
            if ( reader.hasOverflowed() || bCountFits == false )
            {
                outError = "intent recording has a broken tick count";
                reset();
                return false;
            }
            ControlIntentTrack& track = _listTrack.emplace_back();
            track._pawnName           = hashed_string( string_view( reinterpret_cast<const utf8*>( nameBytes.data() ), nameBytes.size() ) );
            track._firstTick          = static_cast<uint32>( firstTick );
            track._lastTick           = tickCount > 0 ? static_cast<uint32>( firstTick + tickCount - 1 ) : track._firstTick;
            track._ring.initialize( static_cast<int32>( tickCount > 0 ? tickCount : 1 ) );
            for ( uint64 tickOffset = 0; tickOffset < tickCount; ++tickOffset )
            {
                if ( track._ring.acquire( static_cast<uint32>( firstTick + tickOffset ) ).read( reader ) == false )
                {
                    outError = "intent recording ends inside track '" + string( track._pawnName.c_str() ) + "'";
                    reset();
                    return false;
                }
            }
        }
        return true;
    }

    bool ControlIntentHistory::saveToFile( string_view path ) const
    {
        BitWriter writer;
        write( writer );
        return FileUtil::writeFile( path, writer.getBytes().data(), static_cast<uint64>( writer.getByteCount() ) );
    }

    bool ControlIntentHistory::loadFromFile( string_view path, string& outError )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( path, bytes ) == false )
        {
            outError = string( path ) + ": could not read";
            reset();
            return false;
        }
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( read( reader, outError ) )
            return true;
        outError = string( path ) + ": " + outError;
        return false;
    }

    int32 ControlIntentHistory::findTrack( const hashed_string& pawnName ) const
    {
        for ( size_t trackIndex = 0; trackIndex < _listTrack.size(); ++trackIndex )
        {
            if ( _listTrack[trackIndex]._pawnName == pawnName )
                return static_cast<int32>( trackIndex );
        }
        return -1;
    }

    const ControlIntent* ControlIntentHistory::findIntent( int32 trackIndex, uint32 tick ) const
    {
        if ( trackIndex < 0 || getTrackCount() <= trackIndex )
            return nullptr;
        return _listTrack[static_cast<size_t>( trackIndex )]._ring.find( tick );
    }

    bool ControlIntentHistory::copyTrack( int32 trackIndex, vector<ControlIntent>& outListIntent ) const
    {
        outListIntent.clear();
        if ( trackIndex < 0 || getTrackCount() <= trackIndex )
            return false;
        const ControlIntentTrack& track = _listTrack[static_cast<size_t>( trackIndex )];
        if ( track._ring.getCount() == 0 )
            return true;
        const uint32 oldestTick = computeOldestTick( track );
        outListIntent.reserve( static_cast<size_t>( track._lastTick - oldestTick + 1 ) );
        for ( uint32 tick = oldestTick; tick <= track._lastTick; ++tick )
        {
            const ControlIntent* pIntent = track._ring.find( tick );
            if ( pIntent == nullptr )
            {
                outListIntent.clear();
                return false;
            }
            outListIntent.push_back( *pIntent );
        }
        return true;
    }

    uint32 ControlIntentHistory::computeOldestTick( const ControlIntentTrack& track )
    {
        const uint32 ringOldestTick = track._ring.computeOldestTick();
        return ringOldestTick > track._firstTick ? ringOldestTick : track._firstTick;
    }
} // namespace sw
