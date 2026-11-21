#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"

#include "Core/Serialization/BitStream.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

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

    bool PairedAnimationSessionSnapshot::writeBytes( vector<uint8>& outBytes ) const
    {
        BitWriter writer( std::move( outBytes ) );
        writer.writeUint32( kMagic );
        const bool bWritten = ReplicatedLayout::writeFull( *this, writer );
        outBytes            = writer.releaseBytes();
        if ( bWritten == false )
            outBytes.clear();
        return bWritten;
    }

    PairedAnimationSessionSnapshot::ReadResult PairedAnimationSessionSnapshot::readBytes( const vector<uint8>& bytes, PairedAnimationSessionSnapshot& outSnapshot )
    {
        outSnapshot = PairedAnimationSessionSnapshot{};
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        if ( reader.readUint32() != kMagic )
            return ReadResult::WrongMagic;
        // 역할 수 상한 · 등록 밖 단계 · 유한하지 않은 시각은 칸 선언이 거절한다.
        PairedAnimationSessionSnapshot snapshot{};
        if ( ReplicatedLayout::readFull( reader, snapshot, nullptr ) != FixedReadResult::Ok || reader.hasOverflowed() )
            return ReadResult::Truncated;
        outSnapshot = std::move( snapshot );
        return ReadResult::Success;
    }
} // namespace sw
