#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickStateBundle.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/LittleEndianUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"

namespace sw
{
    namespace
    {
        struct GimmickStateBundleInternal
        {
            static constexpr size_t kHeaderBytes      = 12; ///< magic, version, count
            static constexpr size_t kEntryHeaderBytes = 12; ///< objectID, length
        };
    } // namespace
} // namespace sw

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
            entry._bytes    = pCircuit->getStateBytes();
            _listEntry.push_back( std::move( entry ) );
        }
        // 등록 순서에 기대지 않는다 — 키 순이라 같은 씬이면 늘 같은 묶음이다.
        std::sort( _listEntry.begin(), _listEntry.end(), []( const GimmickStateEntry& lhs, const GimmickStateEntry& rhs )
        { return lhs._ownerKey < rhs._ownerKey; } );
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
        std::stable_sort( listLive.begin(), listLive.end(), []( const Live& lhs, const Live& rhs )
        { return lhs._ownerKey < rhs._ownerKey; } );

        uint32 applied = 0;
        uint32 skipped = 0;
        for ( const GimmickStateEntry& entry : _listEntry )
        {
            const auto liveIt = std::lower_bound( listLive.begin(), listLive.end(), entry._ownerKey, []( const Live& live, uint64 key )
            { return live._ownerKey < key; } );
            const bool bFound = liveIt != listLive.end() && liveIt->_ownerKey == entry._ownerKey;
            if ( bFound && liveIt->_pCircuit->restoreStateBytes( entry._bytes ) )
                ++applied;
            else
                ++skipped;
        }
        if ( pOutSkippedCount != nullptr )
            *pOutSkippedCount = skipped;
        return applied;
    }

    void GimmickStateBundle::write( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        LittleEndianUtil::appendUint32( outBytes, kMagic );
        LittleEndianUtil::appendUint32( outBytes, kVersion );
        LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( _listEntry.size() ) );
        for ( const GimmickStateEntry& entry : _listEntry )
        {
            LittleEndianUtil::appendUint64( outBytes, entry._ownerKey );
            LittleEndianUtil::appendUint32( outBytes, static_cast<uint32>( entry._bytes.size() ) );
            outBytes.insert( outBytes.end(), entry._bytes.begin(), entry._bytes.end() );
        }
    }

    bool GimmickStateBundle::read( const uint8* pData, size_t size )
    {
        using Internal            = GimmickStateBundleInternal;
        const bool bInvalidBundle = pData == nullptr || size < Internal::kHeaderBytes || LittleEndianUtil::readUint32( pData ) != kMagic || LittleEndianUtil::readUint32( pData + 4 ) != kVersion;
        if ( bInvalidBundle )
            return false;
        const uint32 count = LittleEndianUtil::readUint32( pData + 8 );
        if ( count > kMaxEntryCount )
            return false;

        vector<GimmickStateEntry> listEntry;
        size_t                    position = Internal::kHeaderBytes;
        for ( uint32 entryIndex = 0; entryIndex < count; ++entryIndex )
        {
            if ( size - position < Internal::kEntryHeaderBytes )
                return false;
            GimmickStateEntry entry;
            entry._ownerKey     = LittleEndianUtil::readUint64( pData + position );
            const uint32 length = LittleEndianUtil::readUint32( pData + position + 8 );
            position += Internal::kEntryHeaderBytes;
            if ( length > kMaxEntryBytes || length > size - position )
                return false;
            entry._bytes.assign( pData + position, pData + position + length );
            position += length;
            listEntry.push_back( std::move( entry ) );
        }
        if ( position != size )
            return false; // 끝 뒤에 남는 바이트
        _listEntry = std::move( listEntry );
        return true;
    }

    uint64 GimmickStateBundle::computeHash() const
    {
        uint64 hash = HashUtil::kFnvOffset64;
        for ( const GimmickStateEntry& entry : _listEntry )
        {
            hash = HashUtil::combine( hash, entry._ownerKey );
            for ( const uint8 value : entry._bytes )
            {
                hash = ( hash ^ value ) * HashUtil::kFnvPrime64;
            }
        }
        return hash;
    }

    const GimmickStateEntry* GimmickStateBundle::findEntry( uint64 ownerKey ) const
    {
        for ( const GimmickStateEntry& entry : _listEntry )
        {
            if ( entry._ownerKey == ownerKey )
                return &entry;
        }
        return nullptr;
    }
} // namespace sw
