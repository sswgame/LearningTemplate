#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Content/AssetIO.h"

namespace sw
{
} // namespace sw

namespace sw
{
    ControlIntentHistory::ControlIntentHistory()
        : _listTrack{}
        , _mapComponentIDToTrack{}
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
        _mapComponentIDToTrack.clear();
    }

    void ControlIntentHistory::record( uint32 tick, const TComponentHandle<PawnComponent>& pawn, const hashed_string& pawnName, const ControlIntent& intent )
    {
        const unordered_map<uint64, int32>::const_iterator found      = _mapComponentIDToTrack.find( pawn.componentID() );
        const int32                                        trackIndex = found != _mapComponentIDToTrack.end() ? found->second : static_cast<int32>( _listTrack.size() );
        if ( found == _mapComponentIDToTrack.end() )
        {
            _listTrack.emplace_back();
            ControlIntentTrack& created = _listTrack.back();
            created._ring.initialize( _capacityTicks );
            created._pawnName = pawnName;
            created._pawn     = pawn;
            _mapComponentIDToTrack.emplace( pawn.componentID(), trackIndex );
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

    bool ControlIntentHistory::write( BitWriter& writer ) const
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
                SW_LOG_WARNING_ID( "ControlIntentHistory.IntentTrackLostFirst", "Intent track '%#' lost its first %# tick(s) to the ring - a replay from the scene start will not match", track._pawnName.c_str(),
                                   oldestTick - track._firstTick );
            const string_view name = track._pawnName.c_str();
            writer.writeBlob( reinterpret_cast<const uint8*>( name.data() ), static_cast<int32>( name.size() ) );
            writer.writeVarUint( oldestTick - baseTick );
            writer.writeVarUint( tickCount );
            for ( uint32 tickOffset = 0; tickOffset < tickCount; ++tickOffset )
            {
                const ControlIntent* pIntent = track._ring.find( oldestTick + tickOffset );
                if ( ( pIntent != nullptr ? *pIntent : ControlIntent{} ).write( writer ) == false )
                    return false;
            }
        }
        return true;
    }

    expected<void> ControlIntentHistory::read( BitReader& reader )
    {
        reset();
        const uint32 magic   = reader.readUint32();
        const uint64 version = reader.readVarUint();
        if ( reader.hasOverflowed() || magic != kMagic )
            return unexpected( Error{ ErrorKind::InvalidArgument, "not an intent recording (magic)" } );
        if ( version != kVersion )
        {
            StringBuilder<constant::buffer::kMax128> errorBuilder;
            errorBuilder.appendFormat( "%#, %#", version, kVersion );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the intent recording version is not supported (found, expected)", errorBuilder.view() } );
        }
        const uint64 trackCount = reader.readVarUint();
        if ( reader.hasOverflowed() || trackCount > static_cast<uint64>( kMaxTrackCount ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "intent recording has a broken track count" } );
        vector<uint8> nameBytes;
        for ( uint64 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            if ( reader.readBlob( nameBytes, kMaxNameBytes ) == false )
            {
                reset();
                return unexpected( Error{ ErrorKind::InvalidArgument, "intent recording has a broken pawn name" } );
            }
            const uint64 firstTick = reader.readVarUint();
            const uint64 tickCount = reader.readVarUint();
            // 의도 하나는 적어도 한 바이트 — 남은 바이트보다 많은 틱 수는 깨진 머리다(큰 고리를 잡기 전에 거른다).
            const bool bCountFits = tickCount <= static_cast<uint64>( reader.getBitsRemaining() / 8 ) && firstTick <= constant::invalid_index::kUint32 - tickCount;
            if ( reader.hasOverflowed() || bCountFits == false )
            {
                reset();
                return unexpected( Error{ ErrorKind::InvalidArgument, "intent recording has a broken tick count" } );
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
                    reset();
                    return unexpected( Error{ ErrorKind::InvalidArgument, "intent recording ends inside track", string( track._pawnName.c_str() ) } );
                }
            }
        }
        return {};
    }

    bool ControlIntentHistory::saveToFile( const AbsolutePath& absPath ) const
    {
        BitWriter writer;
        if ( write( writer ) == false )
            return false;
        return FileUtil::writeFile( absPath.str(), writer.getBytes().data(), static_cast<uint64>( writer.getByteCount() ) );
    }

    expected<void> ControlIntentHistory::loadFromFile( string_view path )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( path, bytes ) == false )
        {
            reset();
            return unexpected( Error{ ErrorKind::NotFound, "the intent recording could not be read", path } );
        }
        return loadFromBytes( bytes, path );
    }

    expected<void> ControlIntentHistory::loadFromAsset( const AssetID& assetID )
    {
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
        {
            reset();
            return unexpected( Error{ ErrorKind::NotFound, "the intent recording could not be read", assetID.view() } );
        }
        return loadFromBytes( bytes, assetID.view() );
    }

    expected<void> ControlIntentHistory::loadFromBytes( const vector<uint8>& bytes, string_view label )
    {
        BitReader      reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        expected<void> result = read( reader );
        if ( result.has_value() )
            return {};
        Error error{ result.error().kind(), result.error().message().data(), label };
        if ( result.error().detail().empty() == false )
            error.appendDetail( ": " ).appendDetail( result.error().detail() );
        return unexpected( error );
    }

    optional<int32> ControlIntentHistory::findTrack( const hashed_string& pawnName ) const
    {
        for ( size_t trackIndex = 0; trackIndex < _listTrack.size(); ++trackIndex )
        {
            if ( _listTrack[trackIndex]._pawnName == pawnName )
                return static_cast<int32>( trackIndex );
        }
        return nullopt;
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
