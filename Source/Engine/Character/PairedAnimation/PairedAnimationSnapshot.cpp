#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"

#include "Core/Serialization/BitStream.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"

namespace sw
{
    namespace
    {
        /** @brief 단계는 두 비트로 씁니다(`PairedAnimationRoleStage` 의 값이 넷입니다). */
        constexpr int32 kPairedSnapshotStageBits = 2;
        /** @brief 역할 하나가 쓰는 비트 수입니다(단계 + 진입 시각). 읽기가 역할 수를 남은 비트와 맞춰 볼 때 씁니다. */
        constexpr int32 kPairedSnapshotRoleBits = kPairedSnapshotStageBits + 32;
    } // namespace
} // namespace sw

namespace sw
{
    PairedAnimationSessionSnapshot PairedAnimationSessionSnapshot::makeAtElapsedTime( const PairedAnimationDef& def, uint32 sessionID, float32 elapsedSeconds )
    {
        PairedAnimationSessionSnapshot snapshot{};
        snapshot._sessionID  = sessionID;
        snapshot._anchorTime = elapsedSeconds;
        snapshot._listRole.resize( def.getRoles().size() );
        for ( size_t roleIndex = 0; roleIndex < def.getRoles().size(); ++roleIndex )
        {
            const PairedAnimationRoleDef& roleDef = def.getRoles()[roleIndex];
            PairedAnimationRoleSnapshot&  role    = snapshot._listRole[roleIndex];
            if ( roleDef._bAnchor || roleDef._startSeconds <= elapsedSeconds )
            {
                role._stage           = PairedAnimationRoleStage::Playing;
                role._entryAnchorTime = roleDef._startSeconds;
            }
            else
            {
                role._stage = PairedAnimationRoleStage::Waiting;
            }
        }
        for ( const PairedAnimationNotifyDef& notify : def.getNotifies() )
        {
            if ( notify._time <= elapsedSeconds )
                ++snapshot._nextNotifyIndex;
        }
        return snapshot;
    }

    void PairedAnimationSessionSnapshot::writeBytes( vector<uint8>& outBytes ) const
    {
        BitWriter writer;
        writer.writeUint32( kMagic );
        writer.writeVarUint( _sessionID );
        writer.writeFloat( _anchorTime );
        writer.writeVarUint( _nextNotifyIndex );
        writer.writeVarUint( _listRole.size() );
        for ( const PairedAnimationRoleSnapshot& role : _listRole )
        {
            writer.writeBits( static_cast<uint32>( role._stage ), kPairedSnapshotStageBits );
            writer.writeFloat( role._entryAnchorTime );
        }
        outBytes = writer.releaseBytes();
    }

    PairedAnimationSessionSnapshot::ReadResult PairedAnimationSessionSnapshot::readBytes( const vector<uint8>& bytes, PairedAnimationSessionSnapshot& outSnapshot )
    {
        outSnapshot = PairedAnimationSessionSnapshot{};
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readUint32() != kMagic )
            return ReadResult::WrongMagic;
        PairedAnimationSessionSnapshot snapshot{};
        snapshot._sessionID = reader.readVarUint32();
        if ( reader.readFiniteFloat( snapshot._anchorTime ) == false )
            return ReadResult::Truncated;
        snapshot._nextNotifyIndex = reader.readVarUint32();
        const uint64 roleCount    = reader.readVarUint();
        // 역할 수가 남은 비트로 채울 수 있는 만큼이어야 한다 - 깨진 바이트가 큰 수를 말해도 그만큼 잡지 않는다.
        const bool bRoleCountFits = reader.hasOverflowed() == false && roleCount <= static_cast<uint64>( reader.getBitsRemaining() ) / static_cast<uint64>( kPairedSnapshotRoleBits );
        if ( bRoleCountFits == false )
            return ReadResult::Truncated;
        snapshot._listRole.resize( static_cast<size_t>( roleCount ) );
        for ( PairedAnimationRoleSnapshot& role : snapshot._listRole )
        {
            role._stage = static_cast<PairedAnimationRoleStage>( reader.readBits( kPairedSnapshotStageBits ) );
            if ( reader.readFiniteFloat( role._entryAnchorTime ) == false )
                return ReadResult::Truncated;
        }
        if ( reader.hasOverflowed() )
            return ReadResult::Truncated;
        outSnapshot = std::move( snapshot );
        return ReadResult::Success;
    }
} // namespace sw
