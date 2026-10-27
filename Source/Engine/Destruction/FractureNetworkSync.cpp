#include "pch.h"

#include "Engine/Destruction/FractureNetworkSync.h"

#include "Core/Network/BitStream.h"

namespace sw
{
    FractureNetworkSync::FractureNetworkSync()
        : _driveMutex{}
        , _listPendingDrive{}
        , _listRecentHash{}
    {
    }

    void FractureNetworkSync::recordStateHash( uint32 eventCount, uint64 hash )
    {
        // 받는 쪽 컴포넌트만 처음 기록할 때 슬롯을 잡는다. 권한 쪽이나 파괴를 받지 않는 컴포넌트는 슬롯을 갖지 않는다.
        if ( _listRecentHash.getCapacity() != static_cast<int32>( kRecentHashCount ) )
            _listRecentHash.initialize( static_cast<int32>( kRecentHashCount ) );
        _listRecentHash.acquire( eventCount ) = hash;
    }

    bool FractureNetworkSync::findStateHash( uint32 eventCount, uint64& outHash ) const
    {
        const uint64* pHash = _listRecentHash.find( eventCount );
        if ( pHash == nullptr )
            return false;
        outHash = *pHash;
        return true;
    }

    void FractureNetworkSync::queueDriveTarget( const FractureGroupPose& target )
    {
        ScopedLock<mutex> lock{ _driveMutex };
        for ( FractureGroupPose& existing : _listPendingDrive )
        {
            if ( existing._groupID == target._groupID )
            {
                existing = target;
                return;
            }
        }
        _listPendingDrive.push_back( target );
    }

    void FractureNetworkSync::takeDriveTargets( vector<FractureGroupPose>& outListTarget )
    {
        outListTarget.clear();
        ScopedLock<mutex> lock{ _driveMutex };
        outListTarget.swap( _listPendingDrive );
    }

    void FractureNetworkSync::writeSnapshot( const vector<uint8>& stateBytes, const vector<FractureGroupPose>& listPose, vector<uint8>& outBytes )
    {
        BitWriter writer;
        writer.writeUint32( kMagic );
        writer.writeBlob( stateBytes.data(), static_cast<int32>( stateBytes.size() ) );
        writer.writeVarUint( listPose.size() );
        for ( const FractureGroupPose& pose : listPose )
        {
            writer.writeVarUint( pose._groupID );
            writer.writeBits( pose._bGone == SW_TRUE ? 1u : 0u, 1 );
            if ( pose._bGone == SW_TRUE )
                continue;
            writer.writeFloat( pose._position._x );
            writer.writeFloat( pose._position._y );
            writer.writeFloat( pose._position._z );
            writer.writeFloat( pose._rotation._x );
            writer.writeFloat( pose._rotation._y );
            writer.writeFloat( pose._rotation._z );
            writer.writeFloat( pose._rotation._w );
        }
        outBytes = writer.releaseBytes();
    }

    FractureNetworkSync::SnapshotReadResult FractureNetworkSync::readSnapshot( const vector<uint8>& bytes, vector<uint8>& outStateBytes, vector<FractureGroupPose>& outListPose )
    {
        outStateBytes.clear();
        outListPose.clear();
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readUint32() != kMagic )
            return SnapshotReadResult::WrongMagic;
        // 상태 바이트의 상한은 남은 바이트다. 받은 스냅샷 자체가 길이를 정한다(조각은 이미 모아 왔다).
        if ( reader.readBlob( outStateBytes, reader.getBitsRemaining() / 8 ) == false || outStateBytes.empty() )
            return SnapshotReadResult::StateTruncated;
        const uint64 poseCount = reader.readVarUint();
        for ( uint64 index = 0; index < poseCount && reader.hasOverflowed() == false; ++index )
        {
            FractureGroupPose pose;
            pose._groupID = static_cast<uint32>( reader.readVarUint() );
            pose._bGone   = reader.readBits( 1 ) != 0 ? SW_TRUE : SW_FALSE;
            if ( pose._bGone == SW_FALSE )
            {
                pose._position._x = reader.readFloat();
                pose._position._y = reader.readFloat();
                pose._position._z = reader.readFloat();
                pose._rotation._x = reader.readFloat();
                pose._rotation._y = reader.readFloat();
                pose._rotation._z = reader.readFloat();
                pose._rotation._w = reader.readFloat();
            }
            outListPose.push_back( pose );
        }
        if ( reader.hasOverflowed() )
            return SnapshotReadResult::PoseTruncated;
        return SnapshotReadResult::Success;
    }
} // namespace sw
