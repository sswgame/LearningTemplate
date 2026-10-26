#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickStateBundle.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/CompareUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"

namespace sw
{
    void GimmickStateBundle::collect( const GameObjectManager& manager )
    {
        _listEntry.clear();
        for ( GimmickCircuitComponent* pCircuit : manager.getComponentRegistry().getAll<GimmickCircuitComponent>() )
        {
            const GameObject* pOwner       = pCircuit->getOwner();
            const bool        bSkipCircuit = pOwner == nullptr || pCircuit->isPendingDestroy() || pCircuit->getCircuit().isBuilt() == false;
            if ( bSkipCircuit )
                continue;
            GimmickStateEntry entry;
            entry._ownerKey = makeOwnerKey( pOwner->getName() );
            entry._state    = pCircuit->getState();
            _listEntry.push_back( std::move( entry ) );
        }
        // 등록 순서에 기대지 않는다 — 키 순이라 같은 씬이면 늘 같은 묶음이다.
        std::sort( _listEntry.begin(), _listEntry.end(), Ascending<&GimmickStateEntry::_ownerKey>{} );
    }

    uint32 GimmickStateBundle::apply( GameObjectManager& manager, uint32* pOutSkippedCount ) const
    {
        // 씬의 회로를 키로 한 번 정렬해 두고 항목마다 이분 탐색한다(항목 수만큼 씬을 훑지 않는다). 같은 키가 둘이면 앞의 것이다.
        struct Live
        {
            uint64                   _ownerKey;
            GimmickCircuitComponent* _pCircuit;
        };
        vector<Live> listLive;
        for ( GimmickCircuitComponent* pCircuit : manager.getComponentRegistry().getAll<GimmickCircuitComponent>() )
        {
            const GameObject* pOwner = pCircuit->getOwner();
            if ( pOwner != nullptr && pCircuit->isPendingDestroy() == false )
                listLive.push_back( Live{ makeOwnerKey( pOwner->getName() ), pCircuit } );
        }
        std::stable_sort( listLive.begin(), listLive.end(), Ascending<&Live::_ownerKey>{} );

        uint32 applied = 0;
        uint32 skipped = 0;
        for ( const GimmickStateEntry& entry : _listEntry )
        {
            const auto liveIt = std::lower_bound( listLive.begin(), listLive.end(), entry._ownerKey, []( const Live& live, uint64 key )
            { return live._ownerKey < key; } );
            const bool bFound = liveIt != listLive.end() && liveIt->_ownerKey == entry._ownerKey;
            if ( bFound && liveIt->_pCircuit->restoreState( entry._state ) )
                ++applied;
            else
                ++skipped;
        }
        if ( pOutSkippedCount != nullptr )
            *pOutSkippedCount = skipped;
        return applied;
    }

    bool GimmickStateBundle::write( vector<uint8>& outBytes ) const
    {
        BitWriter                   writer{ std::move( outBytes ) };
        [[maybe_unused]] const bool bWritten = FixedLayout::write( *this, writer );
        outBytes                             = writer.releaseBytes();
        if ( bWritten == false )
            outBytes.clear();
        return bWritten;
    }

    FixedReadResult GimmickStateBundle::read( const uint8* pData, size_t size )
    {
        // BitReader 는 int32 바이트 수를 받는다. 그보다 큰 입력은 항목 상한으로도 나올 수 없는 크기다.
        if ( pData == nullptr || size > static_cast<size_t>( std::numeric_limits<int32>::max() ) )
            return FixedReadResult::Truncated;
        return FixedLayout::read( pData, static_cast<int32>( size ), *this );
    }

    uint64 GimmickStateBundle::computeHash() const
    {
        BitWriter                   writer;
        [[maybe_unused]] const bool bWritten = FixedLayout::write( *this, writer );
        SW_ASSERT( bWritten ); // 항목 수는 kMaxEntryCount, 회로 상태는 populate · registerKind 가 상한 안으로 막았다
        return HashUtil::appendBytes64( HashUtil::kFnvOffset64, writer.getBytes().data(), static_cast<size_t>( writer.getByteCount() ) );
    }

    const GimmickStateEntry* GimmickStateBundle::findEntry( uint64 ownerKey ) const
    {
        return VectorUtil::findBy<&GimmickStateEntry::_ownerKey>( _listEntry, ownerKey );
    }
} // namespace sw
