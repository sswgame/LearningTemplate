#include "pch.h"

#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/Registry/ScenePrimitive.h"

#include <algorithm>

namespace sw
{
    void PrimitiveRegistry::add( ScenePrimitive* pComp )
    {
        if ( pComp == nullptr )
            return;

        ScopedLock<mutex> lock{ _mutex };
        // 슬롯이 정말 이 레지스트리에서 자기를 가리키고 있을 때만 "이미 등록됨"이다. 인덱스는 컴포넌트가 들고 있어서, 목록이 다른 것을
        // 가리키는데 그대로 믿으면 조용히 미등록으로 남는다(컴포넌트 이동은 막혀 있지만 인덱스를 그대로 믿지 않는다).
        const uint32 existing           = pComp->getPrimitiveIndex();
        const bool   bAlreadyRegistered = existing != ScenePrimitive::kInvalidPrimitiveIndex && existing < _listPrimitive.size() &&
                                        _listPrimitive[existing] == pComp;
        if ( bAlreadyRegistered )
            return;

        const uint32 slot = static_cast<uint32>( _listPrimitive.size() );
        // 인스턴스 항목이 메시 컴포넌트 뒤에 이어지므로 깃발은 전체 번호 공간만큼 있어야 한다.
        growDirtyFlags( slot + 1 + static_cast<uint32>( _listInstanceEntry.size() ) );
        _listPrimitive.push_back( pComp );
        _listPrimitiveTransformSlot.push_back( pComp->getTransformSlot() );
        pComp->setPrimitiveIndex( slot );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::growDirtyFlags( uint32 count )
    {
        if ( count <= _dirtyFlagCapacity )
            return;
        // 두 배씩 늘린다. 프리미티브가 하나씩 늘 때마다 배열을 다시 만들지 않는다. 용량은 워드 단위(64 칸)다.
        uint32 capacity = ( _dirtyFlagCapacity == 0 ) ? 64u : _dirtyFlagCapacity;
        while ( capacity < count )
        {
            capacity *= 2u;
        }
        const uint32                 oldWordCount    = _dirtyFlagCapacity / 64u;
        const uint32                 wordCount       = capacity / 64u;
        unique_ptr<atomic<uint64>[]> arrNew          = make_unique<atomic<uint64>[]>( wordCount );
        unique_ptr<atomic<uint64>[]> arrNewTransform = make_unique<atomic<uint64>[]>( wordCount );
        for ( uint32 wordIndex = 0; wordIndex < wordCount; ++wordIndex )
        {
            const bool   bOld              = wordIndex < oldWordCount;
            const uint64 previous          = bOld ? _arrDirtyWord[wordIndex].load( std::memory_order_relaxed ) : 0u;
            const uint64 previousTransform = bOld ? _arrTransformDirtyWord[wordIndex].load( std::memory_order_relaxed ) : 0u;
            arrNew[wordIndex].store( previous, std::memory_order_relaxed );
            arrNewTransform[wordIndex].store( previousTransform, std::memory_order_relaxed );
        }
        _arrDirtyWord          = std::move( arrNew );
        _arrTransformDirtyWord = std::move( arrNewTransform );
        _dirtyFlagCapacity     = capacity;
    }

    void PrimitiveRegistry::remove( ScenePrimitive* pComp )
    {
        if ( pComp == nullptr )
            return;

        ScopedLock<mutex> lock{ _mutex };
        // 에셋을 기다리던 것도 뺀다 — 사라진 컴포넌트의 함수를 부르지 않는다.
        for ( size_t waiterIndex = 0; waiterIndex < _listAssetWaiter.size(); ++waiterIndex )
        {
            if ( _listAssetWaiter[waiterIndex]._pPrimitive != pComp )
                continue;
            VectorUtil::removeAtSwap( _listAssetWaiter, waiterIndex );
            break;
        }
        (void)_listLODPrimitive.remove( pComp ); // LOD 메시가 아니었으면 목록에 없다
        const uint32 slot = pComp->getPrimitiveIndex();
        // 슬롯이 정말 이 레지스트리의 것인지 확인한다. 인덱스는 컴포넌트가 들고 있어서, 다른 레지스트리가
        // 실수로 불리면 남의 목록을 그럴듯한 인덱스로 훼손할 수 있다.
        const bool bSlotNotOwned = slot == ScenePrimitive::kInvalidPrimitiveIndex || slot >= _listPrimitive.size() ||
                                   _listPrimitive[slot] != pComp;
        if ( bSlotNotOwned )
            return;

        // swap-and-pop. 마지막 원소가 이 자리로 오므로 그쪽 인덱스와 **깃발**을 같이 옮긴다(지울 때마다 깃발 전부를 훑지 않는다).
        // 집합 세대가 오르므로 빌더는 어차피 전부 다시 모은다. 옮기지 않은 인스턴스 항목 깃발이 한 칸 어긋나도
        // (`markInstanceDirty` 주석) 답은 틀리지 않는다.
        ScenePrimitive* pMoved   = _listPrimitive.back();
        const uint32    lastSlot = static_cast<uint32>( _listPrimitive.size() - 1 );
        VectorUtil::removeAtSwap( _listPrimitive, slot );
        VectorUtil::removeAtSwap( _listPrimitiveTransformSlot, slot );
        if ( pMoved != pComp )
            pMoved->setPrimitiveIndex( slot );
        if ( lastSlot < _dirtyFlagCapacity )
        {
            // 깃발 둘을 다 옮긴다. 트랜스폼 깃발만 옮기지 않으면 움직인 물체가 한 프레임 멈춰 보인다.
            const bool bMovedDirty          = takeWordBit( _arrDirtyWord.get(), lastSlot );
            const bool bMovedTransformDirty = takeWordBit( _arrTransformDirtyWord.get(), lastSlot );
            if ( slot != lastSlot )
            {
                storeWordBit( _arrDirtyWord.get(), slot, bMovedDirty );
                storeWordBit( _arrTransformDirtyWord.get(), slot, bMovedTransformDirty );
            }
        }
        pComp->setPrimitiveIndex( ScenePrimitive::kInvalidPrimitiveIndex );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::addAssetWaiter( ScenePrimitive* pPrimitive, AssetWaitFunction pFunction )
    {
        SW_ASSERT( pPrimitive != nullptr && pFunction != nullptr );
        ScopedLock<mutex> lock{ _mutex };
        for ( AssetWaiter& waiter : _listAssetWaiter )
        {
            if ( waiter._pPrimitive != pPrimitive )
                continue;
            waiter._pFunction = pFunction;
            return;
        }
        AssetWaiter& added = _listAssetWaiter.emplace_back();
        added._pPrimitive  = pPrimitive;
        added._pFunction   = pFunction;
    }

    void PrimitiveRegistry::addLODPrimitive( ScenePrimitive* pPrimitive )
    {
        SW_ASSERT( pPrimitive != nullptr );
        ScopedLock<mutex> lock{ _mutex };
        (void)_listLODPrimitive.add( pPrimitive ); // 이미 걸려 있으면 무시한다
    }

    void PrimitiveRegistry::removeLODPrimitive( ScenePrimitive* pPrimitive )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_listLODPrimitive.remove( pPrimitive ); // 걸려 있지 않았으면 할 일이 없다
    }

    void PrimitiveRegistry::updateAssetWaiters( float32 deltaSeconds )
    {
        // 번호로 돈다 — 함수가 다시 걸면(addAssetWaiter) 같은 자리를 고치거나 뒤에 붙인다. 함수 안에서 프리미티브를 지우는 일(remove)은 없다:
        // 컴포넌트 제거는 틱 밖의 파괴 단계에서만 일어난다. 그래서 함수를 부르는 동안 잠금을 쥐지 않는다(함수가 다시 걸 때 같은 잠금을 잡는다).
        size_t waiterIndex = 0;
        while ( waiterIndex < _listAssetWaiter.size() )
        {
            const AssetWaiter waiter      = _listAssetWaiter[waiterIndex];
            const bool        bStillWaits = waiter._pFunction( *waiter._pPrimitive, deltaSeconds );
            if ( bStillWaits )
            {
                ++waiterIndex;
                continue;
            }
            ScopedLock<mutex> lock{ _mutex };
            VectorUtil::removeAtSwap( _listAssetWaiter, waiterIndex );
        }
    }

    void PrimitiveRegistry::markDirty( ScenePrimitive* pComp )
    {
        if ( pComp == nullptr )
            return;
        const vector<ScenePrimitive*>& listPrimitive     = std::as_const( _listPrimitive );
        const uint32                   slot              = pComp->getPrimitiveIndex();
        const bool                     bInvalidDirtySlot = slot == ScenePrimitive::kInvalidPrimitiveIndex || slot >= listPrimitive.size() || listPrimitive[slot] != pComp;
        if ( bInvalidDirtySlot )
            return;
        markSlotDirty( slot );
    }

    void PrimitiveRegistry::markSlotDirty( uint32 slot )
    {
        if ( slot >= _dirtyFlagCapacity )
            return;
        markWordBit( _arrDirtyWord.get(), slot );
    }

    void PrimitiveRegistry::markTransformDirty( uint32 primitiveIndex )
    {
        if ( primitiveIndex >= _dirtyFlagCapacity )
            return;
        markWordBit( _arrTransformDirtyWord.get(), primitiveIndex );
    }

    void PrimitiveRegistry::markWordBit( atomic<uint64>* pArrWord, uint32 slot )
    {
        atomic<uint64>& word = pArrWord[slot >> 6];
        const uint64    bit  = static_cast<uint64>( 1 ) << ( slot & 63u );
        // 이미 서 있으면 읽기만 한다. 같은 워드의 이웃 칸을 찍는 워커와 캐시 라인을 다투지 않는다.
        if ( ( word.load( std::memory_order_relaxed ) & bit ) != 0u )
            return;
        if ( ( word.fetch_or( bit, std::memory_order_acq_rel ) & bit ) != 0u )
            return;
        if ( _bAnyDirty.load( std::memory_order_relaxed ) == 0u )
            _bAnyDirty.store( 1u, std::memory_order_release );
    }

    bool PrimitiveRegistry::takeWordBit( atomic<uint64>* pArrWord, uint32 slot )
    {
        const uint64 bit = static_cast<uint64>( 1 ) << ( slot & 63u );
        return ( pArrWord[slot >> 6].fetch_and( ~bit, std::memory_order_acq_rel ) & bit ) != 0u;
    }

    void PrimitiveRegistry::storeWordBit( atomic<uint64>* pArrWord, uint32 slot, bool bDirty )
    {
        const uint64 bit = static_cast<uint64>( 1 ) << ( slot & 63u );
        if ( bDirty )
            pArrWord[slot >> 6].fetch_or( bit, std::memory_order_acq_rel );
        else
            pArrWord[slot >> 6].fetch_and( ~bit, std::memory_order_acq_rel );
    }

    PrimitiveRegistry::~PrimitiveRegistry()
    {
        for ( ScenePrimitiveBatch* pBatch : _listInstanceBatch )
        {
            if ( pBatch != nullptr && pBatch->_pRegistry == this )
                pBatch->_pRegistry = nullptr;
        }
    }

    void PrimitiveRegistry::addInstanceBatch( ScenePrimitiveBatch* pBatch )
    {
        if ( pBatch == nullptr )
            return;
        ScopedLock<mutex> lock{ _mutex };
        if ( pBatch->_pRegistry == this )
            return;
        SW_LOG_ASSERT( pBatch->_pRegistry == nullptr, "ScenePrimitiveBatch is already registered to another registry" );
        pBatch->_pRegistry  = this;
        pBatch->_firstEntry = static_cast<uint32>( _listInstanceEntry.size() );
        const uint32 count  = pBatch->getCount();
        _listInstanceEntry.reserve( _listInstanceEntry.size() + count );
        for ( uint32 index = 0; index < count; ++index )
        {
            _listInstanceEntry.push_back( PrimitiveInstanceEntry{ pBatch, index } );
        }
        _listInstanceBatch.push_back( pBatch );
        growDirtyFlags( getSlotCount() );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::removeInstanceBatch( ScenePrimitiveBatch* pBatch )
    {
        removeInstanceBatches( vector_reference<ScenePrimitiveBatch* const>( &pBatch, 1 ) );
    }

    void PrimitiveRegistry::removeInstanceBatches( vector_reference<ScenePrimitiveBatch* const> listBatch )
    {
        if ( listBatch.empty() )
            return;
        ScopedLock<mutex> lock{ _mutex };
        // 이 레지스트리에 든 배치만 모은다(널 · 다른 레지스트리 · 미등록 · 두 번 넘긴 배치는 건너뛴다). 모으면서 `_pRegistry` 를 비워 "빠질 배치" 표시로 쓴다.
        vector<ScenePrimitiveBatch*> listRemoved;
        listRemoved.reserve( listBatch.size() );
        for ( ScenePrimitiveBatch* pBatch : listBatch )
        {
            if ( pBatch == nullptr || pBatch->_pRegistry != this )
                continue;
            pBatch->_pRegistry = nullptr;
            listRemoved.push_back( pBatch );
        }
        if ( listRemoved.empty() )
            return;
        clearDirtyLocked();

        // 항목 구간을 자리 순으로 놓고 목록을 한 번만 접는다. 구간은 서로 겹치지 않는다(배치마다 자기 구간).
        // 정렬 키는 (첫 항목 자리 << 32 | listRemoved 안의 번호)다 — 자리가 같은(항목 0 개) 배치는 번호 순이다.
        vector<uint64> listOrderKey;
        listOrderKey.reserve( listRemoved.size() );
        for ( size_t removedIndex = 0; removedIndex < listRemoved.size(); ++removedIndex )
        {
            listOrderKey.push_back( ( static_cast<uint64>( listRemoved[removedIndex]->_firstEntry ) << 32 ) | static_cast<uint64>( removedIndex ) );
        }
        std::sort( listOrderKey.begin(), listOrderKey.end() );
        const uint32   entryTotal = static_cast<uint32>( _listInstanceEntry.size() );
        uint32         readIndex  = 0;
        uint32         writeIndex = 0;
        vector<uint32> listRemovedFirst;
        vector<uint32> listRemovedPrefix; // 앞 구간들이 빠진 항목 수의 누적(자기 구간 포함)
        listRemovedFirst.reserve( listRemoved.size() );
        listRemovedPrefix.reserve( listRemoved.size() );
        for ( const uint64 orderKey : listOrderKey )
        {
            const ScenePrimitiveBatch* pBatch          = listRemoved[static_cast<uint32>( orderKey )];
            const uint32               first           = pBatch->_firstEntry;
            const uint32               count           = pBatch->getCount();
            const bool                 bOutsideEntries = first > entryTotal || count > entryTotal - first;
            if ( bOutsideEntries || first < readIndex )
                continue; // 항목 목록과 어긋난 배치 — 항목은 건드리지 않는다(옛 단건 제거와 같다)
            if ( writeIndex != readIndex )
                std::copy( _listInstanceEntry.begin() + readIndex, _listInstanceEntry.begin() + first, _listInstanceEntry.begin() + writeIndex );
            writeIndex += first - readIndex;
            readIndex = first + count;
            listRemovedFirst.push_back( first );
            listRemovedPrefix.push_back( ( listRemovedPrefix.empty() ? 0u : listRemovedPrefix.back() ) + count );
        }
        if ( writeIndex != readIndex )
            std::copy( _listInstanceEntry.begin() + readIndex, _listInstanceEntry.end(), _listInstanceEntry.begin() + writeIndex );
        _listInstanceEntry.resize( writeIndex + ( entryTotal - readIndex ) );

        // 남은 배치는 자기보다 앞에서 빠진 항목 수만큼 앞으로 당겨진다. 빠진 배치는 목록에서 뺀다(남은 배치의 순서는 그대로).
        size_t keepCount = 0;
        for ( ScenePrimitiveBatch* pOther : _listInstanceBatch )
        {
            if ( pOther == nullptr || pOther->_pRegistry != this )
                continue;
            const size_t removedBefore = static_cast<size_t>( std::lower_bound( listRemovedFirst.begin(), listRemovedFirst.end(), pOther->_firstEntry ) - listRemovedFirst.begin() );
            if ( removedBefore > 0 )
                pOther->_firstEntry -= listRemovedPrefix[removedBefore - 1];
            _listInstanceBatch[keepCount++] = pOther;
        }
        _listInstanceBatch.resize( keepCount );
        for ( ScenePrimitiveBatch* pBatch : listRemoved )
        {
            pBatch->_firstEntry = 0;
        }
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::markInstanceDirty( ScenePrimitiveBatch* pBatch, uint32 index )
    {
        const bool bInvalidInstanceDirty = pBatch == nullptr || pBatch->_pRegistry != this || index >= pBatch->getCount();
        if ( bInvalidInstanceDirty )
            return;
        // 잠금 없이 메시 컴포넌트 수를 읽는다. 그 수가 그 사이 바뀌면 번호가 어긋나지만, 그 변경은 집합 세대를 올려
        // 다음 빌드가 전체 수집으로 가므로 깃발 하나가 엉뚱한 자리에 서도 답은 틀리지 않는다.
        const uint32 slot = static_cast<uint32>( std::as_const( _listPrimitive ).size() ) + pBatch->_firstEntry + index;
        markSlotDirty( slot );
    }

    bool PrimitiveRegistry::hasDirty() const
    {
        return _bAnyDirty.load( std::memory_order_acquire ) != 0u;
    }

    void PrimitiveRegistry::clearDirty()
    {
        ScopedLock<mutex> lock{ _mutex };
        clearDirtyLocked();
    }

    void PrimitiveRegistry::consumeDirty( vector<uint32>& outListStateSlot, vector<uint32>& outListTransformSlot )
    {
        outListStateSlot.clear();
        outListTransformSlot.clear();
        ScopedLock<mutex> lock{ _mutex };
        if ( hasDirty() == false )
            return;
        // "하나라도" 를 훑기 **전에** 내린다. 훑는 동안 워커가 새로 찍으면 다시 서서 다음 프레임에 잡힌다.
        // (이미 지난 칸이면 그 프레임에는 헛훑기 한 번, 아직 안 지난 칸이면 이번에 잡힌다. 어느 쪽도 잃지 않는다.)
        _bAnyDirty.store( 0u, std::memory_order_release );
        // 워드를 훑는다. 8000 칸이 워드 125 개다. 빈 워드는 읽기만, 선 워드만 exchange 한 번 하고 켜진 비트를 골라 돈다.
        // 번호 공간 밖의 비트(지워진 칸)는 여기서 같이 내려 버린다. 그 칸을 다시 쓰는 추가는 집합 세대를 올려 전체 수집이 된다.
        const uint32 count     = MathUtil::min( getSlotCount(), _dirtyFlagCapacity );
        const uint32 wordCount = ( count + 63u ) / 64u;
        for ( uint32 wordIndex = 0; wordIndex < wordCount; ++wordIndex )
        {
            atomic<uint64>& stateWord     = _arrDirtyWord[wordIndex];
            atomic<uint64>& transformWord = _arrTransformDirtyWord[wordIndex];
            if ( stateWord.load( std::memory_order_relaxed ) == 0u && transformWord.load( std::memory_order_relaxed ) == 0u )
                continue;
            const uint64 stateBits = stateWord.exchange( 0u, std::memory_order_acq_rel );
            // 렌더 상태까지 바뀐 칸은 어차피 다시 모으므로 트랜스폼 목록에서 뺀다.
            const uint64 transformBits = transformWord.exchange( 0u, std::memory_order_acq_rel ) & ~stateBits;
            for ( uint64 bits = stateBits; bits != 0u; bits &= bits - 1u )
            {
                const uint32 slot = wordIndex * 64u + MathUtil::countTrailingZeros( bits );
                if ( slot < count )
                    outListStateSlot.push_back( slot );
            }
            for ( uint64 bits = transformBits; bits != 0u; bits &= bits - 1u )
            {
                const uint32 slot = wordIndex * 64u + MathUtil::countTrailingZeros( bits );
                if ( slot < count )
                    outListTransformSlot.push_back( slot );
            }
        }
    }

    void PrimitiveRegistry::clearDirtyLocked()
    {
        _bAnyDirty.store( 0u, std::memory_order_release );
        const uint32 wordCount = _dirtyFlagCapacity / 64u;
        for ( uint32 wordIndex = 0; wordIndex < wordCount; ++wordIndex )
        {
            if ( _arrDirtyWord[wordIndex].load( std::memory_order_relaxed ) != 0u )
                _arrDirtyWord[wordIndex].store( 0u, std::memory_order_release );
            if ( _arrTransformDirtyWord[wordIndex].load( std::memory_order_relaxed ) != 0u )
                _arrTransformDirtyWord[wordIndex].store( 0u, std::memory_order_release );
        }
    }
} // namespace sw
