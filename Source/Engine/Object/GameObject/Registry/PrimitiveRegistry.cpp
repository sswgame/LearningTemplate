#include "pch.h"

#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/Registry/PrimitiveComponent.h"

#include <algorithm>

namespace sw
{
    void PrimitiveRegistry::add( PrimitiveComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        ScopedLock<mutex> lock{ _mutex };
        // 슬롯이 정말 이 레지스트리에서 자기를 가리키고 있을 때만 "이미 등록됨"이다. 인덱스는 컴포넌트가 들고 있어서, 목록이 다른 것을
        // 가리키는데 그대로 믿으면 조용히 미등록으로 남는다(컴포넌트 이동은 막혀 있지만 인덱스를 그대로 믿지 않는다).
        const uint32 existing           = pComp->getPrimitiveIndex();
        const bool   bAlreadyRegistered = existing != PrimitiveComponent::kInvalidPrimitiveIndex && existing < _listPrimitive.size() &&
                                        _listPrimitive[existing] == pComp;
        if ( bAlreadyRegistered )
            return;

        // 칸은 고정이다 — 프리리스트에서 꺼내고, 비었으면 영역을 두 배로 늘린다(늘릴 때만 인스턴스 항목 번호가 밀린다).
        if ( _listFreePrimitiveSlot.empty() )
            growMeshRegion();
        const uint32 slot = _listFreePrimitiveSlot.back();
        _listFreePrimitiveSlot.pop_back();
        _listPrimitive[slot]              = pComp;
        _listPrimitiveTransformSlot[slot] = pComp->getTransformSlot();
        ++_listPrimitiveSerial[slot]; // 다시 쓰는 칸이면 지난 물체의 칸 상태(LOD 전환 · 칸 순 인스턴스)를 이어받지 않게 차례가 바뀐다
        pComp->setPrimitiveIndex( slot );
        // 집합 세대만으로 알리면 빌더가 모든 칸을 다시 모은다. 새 칸 하나를 더티로 올려 그 칸만 모으게 한다.
        markSlotDirty( slot );
        ++_liveSlotCount;
        recordMeshSetChange( slot );
    }

    uint64 PrimitiveRegistry::issueGenerationBase()
    {
        static atomic<uint64> s_nextRange{ 1 };
        return s_nextRange.fetch_add( 1, std::memory_order_relaxed ) << 32u;
    }

    void PrimitiveRegistry::recordMeshSetChange( uint32 slot )
    {
        const uint64 generation = _setGeneration.fetch_add( 1, std::memory_order_relaxed ) + 1u;
        const size_t ringIndex  = static_cast<size_t>( _meshSetChangeTotal % kMeshSetChangeLogCapacity );
        if ( _listMeshSetChange.size() < kMeshSetChangeLogCapacity )
            _listMeshSetChange.push_back( MeshSetChange{ generation, slot } );
        else
        {
            // 가장 오랜 기록을 덮는다 — 그 세대까지는 더 이상 거슬러 갈 수 없다.
            _meshSetChangeLogStart        = _listMeshSetChange[ringIndex]._generation;
            _listMeshSetChange[ringIndex] = MeshSetChange{ generation, slot };
        }
        ++_meshSetChangeTotal;
    }

    bool PrimitiveRegistry::collectMeshSetChanges( uint64 sinceGeneration, vector<uint32>& outListSlot ) const
    {
        outListSlot.clear();
        ScopedLock<mutex> lock{ _mutex };
        // 다른 레지스트리의 세대(앞 구간이든 뒤 구간이든)는 이 기록으로 거슬러 갈 수 없다 — 다시 만든다.
        if ( sinceGeneration < _meshSetChangeLogStart || sinceGeneration > _setGeneration.load( std::memory_order_relaxed ) )
            return false;
        for ( const MeshSetChange& change : _listMeshSetChange )
        {
            if ( change._generation > sinceGeneration )
                outListSlot.push_back( change._slot );
        }
        return true;
    }

    void PrimitiveRegistry::growMeshRegion()
    {
        const uint32 oldCount = static_cast<uint32>( _listPrimitive.size() );
        const uint32 newCount = ( oldCount == 0 ) ? kInitialMeshSlotCount : oldCount * 2u;
        _listPrimitive.resize( newCount, nullptr );
        _listPrimitiveTransformSlot.resize( newCount, SceneTransformStorage::kInvalidSlot );
        _listPrimitiveSerial.resize( newCount, 0u );
        // 아래에서 더티 비트를 버린다 — 버린 표시를 잃지 않게 모든 칸이 바뀐 것으로 친다(늘리기는 두 배씩이라 드물다).
        _listMeshChangeGeneration.assign( newCount, ++_meshChangeGeneration );
        // 스택이라 끝에서 꺼낸다 — 작은 번호부터 쓰이게 큰 번호를 먼저 넣는다.
        _listFreePrimitiveSlot.reserve( _listFreePrimitiveSlot.size() + ( newCount - oldCount ) );
        for ( uint32 slot = newCount; slot > oldCount; --slot )
        {
            _listFreePrimitiveSlot.push_back( slot - 1 );
        }
        // 인스턴스 항목의 번호(영역 크기 + 자리)가 밀렸다. 비트는 옛 번호라 버리고, 배치 세대를 올려 빌더가 모든 칸을 다시 모으게 한다.
        clearDirtyLocked();
        growDirtyFlags( getSlotCount() );
        _layoutGeneration.fetch_add( 1, std::memory_order_relaxed );
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

    void PrimitiveRegistry::remove( PrimitiveComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        ScopedLock<mutex> lock{ _mutex };
        // 에셋을 기다리던 것도 뺀다 — 사라진 컴포넌트의 함수를 부르지 않는다.
        removeAssetWaiterLocked( pComp );
        (void)_setLODPrimitive.erase( pComp ); // LOD 메시가 아니었으면 목록에 없다
        const uint32 slot = pComp->getPrimitiveIndex();
        // 슬롯이 정말 이 레지스트리의 것인지 확인한다. 인덱스는 컴포넌트가 들고 있어서, 다른 레지스트리가
        // 실수로 불리면 남의 목록을 그럴듯한 인덱스로 훼손할 수 있다.
        const bool bSlotNotOwned = slot == PrimitiveComponent::kInvalidPrimitiveIndex || slot >= _listPrimitive.size() ||
                                   _listPrimitive[slot] != pComp;
        if ( bSlotNotOwned )
            return;

        // 칸을 비우고 프리리스트에 돌려준다 — 다른 프리미티브는 옮기지 않으므로 번호가 그대로다(옮기면 옮긴 칸과 그 뒤 인스턴스 항목이 모두 바뀐다).
        // 빈 칸을 더티로 올려 빌더가 그 칸의 후보를 빼게 한다. 트랜스폼 비트는 뜻이 없어 내린다.
        _listPrimitive[slot]              = nullptr;
        _listPrimitiveTransformSlot[slot] = SceneTransformStorage::kInvalidSlot;
        _listFreePrimitiveSlot.push_back( slot );
        if ( slot < _dirtyFlagCapacity )
        {
            (void)takeWordBit( _arrTransformDirtyWord.get(), slot );
            markWordBit( _arrDirtyWord.get(), slot );
        }
        pComp->setPrimitiveIndex( PrimitiveComponent::kInvalidPrimitiveIndex );
        --_liveSlotCount;
        recordMeshSetChange( slot );
    }

    void PrimitiveRegistry::addAssetWaiter( Component* pComponent, AssetWaitFunction pFunction )
    {
        SW_ASSERT( pComponent != nullptr && pFunction != nullptr );
        ScopedLock<mutex> lock{ _mutex };
        const auto        inserted = _mapAssetWaiterToIndex.try_emplace( pComponent, static_cast<uint32>( _listAssetWaiter.size() ) );
        if ( inserted.second == false )
        {
            _listAssetWaiter[inserted.first->second]._pFunction = pFunction;
            return;
        }
        AssetWaiter& added = _listAssetWaiter.emplace_back();
        added._pComponent  = pComponent;
        added._pFunction   = pFunction;
    }

    void PrimitiveRegistry::removeAssetWaiter( const Component* pComponent )
    {
        ScopedLock<mutex> lock{ _mutex };
        removeAssetWaiterLocked( pComponent );
    }

    void PrimitiveRegistry::removeAssetWaiterLocked( const Component* pComponent )
    {
        const auto found = _mapAssetWaiterToIndex.find( pComponent );
        if ( found == _mapAssetWaiterToIndex.end() )
            return;
        const size_t waiterIndex = found->second;
        _mapAssetWaiterToIndex.erase( found );
        // 도는 중(`updateAssetWaiters`)에는 자리를 당기지 않고 비워 둔다 — 당기면 도는 쪽의 번호가 다른 대기자를 가리켜, 그 대기자를 부르지 않고 빼 버린다.
        if ( _bWalkingAssetWaiters != SW_FALSE )
            _listAssetWaiter[waiterIndex] = AssetWaiter{};
        else
            removeAssetWaiterSlotLocked( waiterIndex );
    }

    void PrimitiveRegistry::removeAssetWaiterSlotLocked( size_t waiterIndex )
    {
        VectorUtil::removeAtSwap( _listAssetWaiter, waiterIndex );
        if ( waiterIndex < _listAssetWaiter.size() && _listAssetWaiter[waiterIndex]._pComponent != nullptr )
            _mapAssetWaiterToIndex.insert_or_assign( _listAssetWaiter[waiterIndex]._pComponent, static_cast<uint32>( waiterIndex ) );
    }

    void PrimitiveRegistry::addLODPrimitive( PrimitiveComponent* pPrimitive )
    {
        SW_ASSERT( pPrimitive != nullptr );
        ScopedLock<mutex> lock{ _mutex };
        (void)_setLODPrimitive.insert( pPrimitive ); // 이미 걸려 있으면 무시한다
    }

    void PrimitiveRegistry::removeLODPrimitive( PrimitiveComponent* pPrimitive )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_setLODPrimitive.erase( pPrimitive ); // 걸려 있지 않았으면 할 일이 없다
    }

    void PrimitiveRegistry::updateAssetWaiters( float32 deltaSeconds )
    {
        // 번호로 돈다 — 함수가 다시 걸면(addAssetWaiter) 같은 자리를 고치거나 뒤에 붙인다. 함수 안에서 프리미티브를 지우는 일(remove)은 없다:
        // 컴포넌트 제거는 틱 밖의 파괴 단계에서만 일어난다. 그래서 함수를 부르는 동안 잠금을 쥐지 않는다(함수가 다시 걸 때 같은 잠금을 잡는다).
        // 도는 동안 빼기(`removeAssetWaiter` — 함수 안에서 자기나 다른 컴포넌트를 내릴 때)는 자리를 비우기만 한다. 빈 자리는 여기서 거둔다.
        {
            ScopedLock<mutex> lock{ _mutex };
            _bWalkingAssetWaiters = SW_TRUE;
        }
        size_t waiterIndex = 0;
        while ( waiterIndex < _listAssetWaiter.size() )
        {
            const AssetWaiter waiter      = _listAssetWaiter[waiterIndex];
            const bool        bStillWaits = waiter._pComponent != nullptr && waiter._pFunction( *waiter._pComponent, deltaSeconds );
            ScopedLock<mutex> lock{ _mutex };
            // 함수가 이 자리를 비웠으면(자기를 뺐다) 남기지 않는다. 다시 걸었으면(같은 컴포넌트, 다른 함수) 그 자리는 그대로 산다.
            const bool bKeep = bStillWaits && _listAssetWaiter[waiterIndex]._pComponent != nullptr;
            if ( bKeep )
            {
                ++waiterIndex;
                continue;
            }
            // 이 자리의 대기자가 아직 걸려 있으면(더 기다리지 않는다) 번호도 지운다. 비운 자리는 빼기가 이미 지웠다.
            if ( _listAssetWaiter[waiterIndex]._pComponent != nullptr )
                (void)_mapAssetWaiterToIndex.erase( _listAssetWaiter[waiterIndex]._pComponent );
            removeAssetWaiterSlotLocked( waiterIndex );
        }
        ScopedLock<mutex> lock{ _mutex };
        _bWalkingAssetWaiters = SW_FALSE;
    }

    void PrimitiveRegistry::markDirty( PrimitiveComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        const vector<PrimitiveComponent*>& listPrimitive     = std::as_const( _listPrimitive );
        const uint32                       slot              = pComp->getPrimitiveIndex();
        const bool                         bInvalidDirtySlot = slot == PrimitiveComponent::kInvalidPrimitiveIndex || slot >= listPrimitive.size() || listPrimitive[slot] != pComp;
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
        for ( PrimitiveBatch* pBatch : _listInstanceBatch )
        {
            if ( pBatch != nullptr && pBatch->_pRegistry == this )
                pBatch->_pRegistry = nullptr;
        }
    }

    void PrimitiveRegistry::addInstanceBatch( PrimitiveBatch* pBatch )
    {
        if ( pBatch == nullptr )
            return;
        ScopedLock<mutex> lock{ _mutex };
        if ( pBatch->_pRegistry == this )
            return;
        SW_LOG_ASSERT( pBatch->_pRegistry == nullptr, "PrimitiveBatch is already registered to another registry" );
        pBatch->_pRegistry  = this;
        const uint32 count  = pBatch->getCount();
        const uint32 first  = allocateEntrySpan( count );
        pBatch->_firstEntry = first;
        for ( uint32 index = 0; index < count; ++index )
        {
            // 차례는 칸에 남아 있던 값에서 잇는다(지운 배치의 항목도 차례를 남겨 둔다).
            PrimitiveInstanceEntry& entry = _listInstanceEntry[first + index];
            entry._pBatch                 = pBatch;
            entry._index                  = index;
            ++entry._serial;
        }
        _listInstanceBatch.push_back( pBatch );
        growDirtyFlags( getSlotCount() );
        // 새 항목 칸만 더티로 올린다 — 다른 칸의 번호는 그대로이므로 빌더가 이 칸들만 모은다.
        const uint32 firstSlot = static_cast<uint32>( _listPrimitive.size() ) + first;
        for ( uint32 index = 0; index < count; ++index )
        {
            markSlotDirty( firstSlot + index );
        }
        _liveSlotCount += count;
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    uint32 PrimitiveRegistry::allocateEntrySpan( uint32 count )
    {
        const uint32 entryTotal = static_cast<uint32>( _listInstanceEntry.size() );
        if ( count == 0 )
            return entryTotal;
        // 첫 맞춤 — 지운 배치의 자리를 다시 쓴다(언리얼 GPUScene 의 `FSpanAllocator` 와 같은 모양). 맞는 구간이 없으면 끝에 붙인다(다른 칸은 그대로다).
        for ( size_t spanIndex = 0; spanIndex < _listFreeEntrySpan.size(); ++spanIndex )
        {
            EntrySpan& span = _listFreeEntrySpan[spanIndex];
            if ( span._count < count )
                continue;
            const uint32 start = span._start;
            span._start += count;
            span._count -= count;
            if ( span._count == 0 )
                _listFreeEntrySpan.erase( _listFreeEntrySpan.begin() + static_cast<ptrdiff_t>( spanIndex ) );
            return start;
        }
        _listInstanceEntry.resize( static_cast<size_t>( entryTotal ) + count );
        return entryTotal;
    }

    void PrimitiveRegistry::freeEntrySpan( uint32 start, uint32 count )
    {
        if ( count == 0 )
            return;
        // 자리 순으로 넣고 앞뒤 이웃과 합친다 — 잘게 쪼개지면 큰 배치가 빈 자리를 못 쓰고 목록 끝만 늘어난다.
        const auto   it        = std::lower_bound( _listFreeEntrySpan.begin(), _listFreeEntrySpan.end(), start, []( const EntrySpan& span, uint32 value )
                 { return span._start < value; } );
        const size_t spanIndex = static_cast<size_t>( it - _listFreeEntrySpan.begin() );
        _listFreeEntrySpan.insert( it, EntrySpan{ start, count } );
        const bool bMergeNext = spanIndex + 1 < _listFreeEntrySpan.size() && start + count == _listFreeEntrySpan[spanIndex + 1]._start;
        if ( bMergeNext )
        {
            _listFreeEntrySpan[spanIndex]._count += _listFreeEntrySpan[spanIndex + 1]._count;
            _listFreeEntrySpan.erase( _listFreeEntrySpan.begin() + static_cast<ptrdiff_t>( spanIndex + 1 ) );
        }
        const bool bMergePrevious = spanIndex > 0 && _listFreeEntrySpan[spanIndex - 1]._start + _listFreeEntrySpan[spanIndex - 1]._count == start;
        if ( bMergePrevious )
        {
            _listFreeEntrySpan[spanIndex - 1]._count += _listFreeEntrySpan[spanIndex]._count;
            _listFreeEntrySpan.erase( _listFreeEntrySpan.begin() + static_cast<ptrdiff_t>( spanIndex ) );
        }
    }

    void PrimitiveRegistry::removeInstanceBatch( PrimitiveBatch* pBatch )
    {
        removeInstanceBatches( vector_reference<PrimitiveBatch* const>( &pBatch, 1 ) );
    }

    void PrimitiveRegistry::removeInstanceBatches( vector_reference<PrimitiveBatch* const> listBatch )
    {
        if ( listBatch.empty() )
            return;
        ScopedLock<mutex> lock{ _mutex };
        // 이 레지스트리에 든 배치만 모은다(널 · 다른 레지스트리 · 미등록 · 두 번 넘긴 배치는 건너뛴다). 모으면서 `_pRegistry` 를 비워 "빠질 배치" 표시로 쓴다.
        vector<PrimitiveBatch*> listRemoved;
        listRemoved.reserve( listBatch.size() );
        for ( PrimitiveBatch* pBatch : listBatch )
        {
            if ( pBatch == nullptr || pBatch->_pRegistry != this )
                continue;
            pBatch->_pRegistry = nullptr;
            listRemoved.push_back( pBatch );
        }
        if ( listRemoved.empty() )
            return;

        // 배치마다 항목 구간을 비우고 그 칸들을 더티로 올린 뒤 빈 구간 목록에 돌려준다. 남은 배치는 옮기지 않는다 — 옮기면 그 뒤 항목 전부의 칸 번호가 바뀐다.
        const uint32 entryTotal = static_cast<uint32>( _listInstanceEntry.size() );
        const uint32 meshRegion = static_cast<uint32>( _listPrimitive.size() );
        for ( PrimitiveBatch* pBatch : listRemoved )
        {
            const uint32 first           = pBatch->_firstEntry;
            const uint32 count           = pBatch->getCount();
            const bool   bOutsideEntries = first > entryTotal || count > entryTotal - first || ( count > 0 && _listInstanceEntry[first]._pBatch != pBatch );
            pBatch->_firstEntry          = 0;
            if ( bOutsideEntries )
                continue; // 항목 목록과 어긋난 배치 — 항목은 건드리지 않는다
            for ( uint32 index = 0; index < count; ++index )
            {
                // 차례는 남긴다 — 다음에 이 칸을 받는 항목이 다른 차례가 되게.
                _listInstanceEntry[first + index]._pBatch = nullptr;
                _listInstanceEntry[first + index]._index  = 0;
                markSlotDirty( meshRegion + first + index );
            }
            _liveSlotCount -= count;
            freeEntrySpan( first, count );
        }

        // 빠진 배치를 목록에서 뺀다(남은 배치의 순서는 그대로). 빠질 배치는 위에서 `_pRegistry` 를 비워 표시했다.
        size_t keepCount = 0;
        for ( PrimitiveBatch* pOther : _listInstanceBatch )
        {
            if ( pOther == nullptr || pOther->_pRegistry != this )
                continue;
            _listInstanceBatch[keepCount++] = pOther;
        }
        _listInstanceBatch.resize( keepCount );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::markInstanceDirty( PrimitiveBatch* pBatch, uint32 index )
    {
        const bool bInvalidInstanceDirty = pBatch == nullptr || pBatch->_pRegistry != this || index >= pBatch->getCount();
        if ( bInvalidInstanceDirty )
            return;
        // 잠금 없이 메시 칸 영역의 크기를 읽는다. 영역이 그 사이 늘면 번호가 어긋나지만, 늘리기는 배치 세대를 올려
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
        // "하나라도" 를 훑기 전에 내린다. 훑는 동안 워커가 새로 찍으면 다시 서서 다음 프레임에 잡힌다.
        // (이미 지난 칸이면 그 프레임에는 헛훑기 한 번, 아직 안 지난 칸이면 이번에 잡힌다. 어느 쪽도 잃지 않는다.)
        _bAnyDirty.store( 0u, std::memory_order_release );
        // 워드를 훑는다. 8000 칸이 워드 125 개다. 빈 워드는 읽기만, 선 워드만 exchange 한 번 하고 켜진 비트를 골라 돈다.
        // 번호 공간은 줄지 않는다(지운 칸은 비워 둔다). 공간 밖 비트는 없지만 경계는 그대로 본다.
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
        ++_meshChangeGeneration;
        recordMeshChanges( outListStateSlot );
        recordMeshChanges( outListTransformSlot );
    }

    void PrimitiveRegistry::markMeshChanged( const vector<uint32>& listSlot )
    {
        if ( listSlot.empty() )
            return;
        ScopedLock<mutex> lock{ _mutex };
        ++_meshChangeGeneration;
        for ( const uint32 slot : listSlot )
        {
            if ( slot < _listMeshChangeGeneration.size() )
                _listMeshChangeGeneration[slot] = _meshChangeGeneration;
        }
    }

    void PrimitiveRegistry::recordMeshChanges( const vector<uint32>& listSlot )
    {
        // 목록은 오름차순이고 메시 칸이 인스턴스 항목보다 앞이다.
        for ( const uint32 slot : listSlot )
        {
            if ( slot >= _listMeshChangeGeneration.size() )
                break;
            _listMeshChangeGeneration[slot] = _meshChangeGeneration;
        }
    }

    void PrimitiveRegistry::appendMeshChanges( uint64 sinceGeneration, vector<uint32>& inoutListSlot ) const
    {
        if ( sinceGeneration == _meshChangeGeneration )
            return;
        for ( uint32 slot = 0; slot < static_cast<uint32>( _listMeshChangeGeneration.size() ); ++slot )
        {
            if ( _listMeshChangeGeneration[slot] > sinceGeneration )
                inoutListSlot.push_back( slot );
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
