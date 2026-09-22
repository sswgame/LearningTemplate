#include "pch.h"

#include "Engine/Object/GameObject/PrimitiveRegistry.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"

namespace sw
{
    void PrimitiveRegistry::add( MeshComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        // 슬롯이 정말 이 등록부에서 자기를 가리키고 있을 때만 "이미 등록됨"이다. 컴포넌트가 이동된
        // 사본이면 인덱스만 따라오고 목록은 원본을 가리키고 있어, 그대로 믿으면 조용히 미등록으로 남는다.
        const uint32 existing = pComp->getPrimitiveIndex();
        if ( existing != MeshComponent::kInvalidPrimitiveIndex && existing < _listPrimitive.size() &&
             _listPrimitive[existing] == pComp )
            return;

        const uint32 slot = static_cast<uint32>( _listPrimitive.size() );
        // 인스턴스 항목이 메시 컴포넌트 뒤에 이어지므로 깃발은 전체 번호 공간만큼 있어야 한다.
        growDirtyFlags( slot + 1 + static_cast<uint32>( _listInstanceEntry.size() ) );
        _listPrimitive.push_back( pComp );
        pComp->setPrimitiveIndex( slot );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::growDirtyFlags( uint32 count )
    {
        if ( count <= _dirtyFlagCapacity )
            return;
        // 두 배씩 — 프리미티브가 하나씩 늘 때마다 배열을 다시 만들지 않는다.
        uint32 capacity = ( _dirtyFlagCapacity == 0 ) ? 64u : _dirtyFlagCapacity;
        while ( capacity < count )
            capacity *= 2u;
        std::unique_ptr<atomic<uint8>[]> arrNew{ new atomic<uint8>[capacity] };
        for ( uint32 index = 0; index < capacity; ++index )
        {
            const uint8 previous = ( index < _dirtyFlagCapacity ) ? _arrDirtyFlag[index].load( std::memory_order_relaxed ) : 0u;
            arrNew[index].store( previous, std::memory_order_relaxed );
        }
        _arrDirtyFlag      = std::move( arrNew );
        _dirtyFlagCapacity = capacity;
    }

    void PrimitiveRegistry::remove( MeshComponent* pComp )
    {
        if ( pComp == nullptr )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        const uint32            slot = pComp->getPrimitiveIndex();
        // 슬롯이 정말 이 등록부의 것인지 확인한다. 인덱스는 컴포넌트가 들고 있어서, 다른 등록부가
        // 실수로 불리면 남의 목록을 그럴듯한 인덱스로 훼손할 수 있다.
        if ( slot == MeshComponent::kInvalidPrimitiveIndex || slot >= _listPrimitive.size() ||
             _listPrimitive[slot] != pComp )
            return;

        // swap-and-pop. 마지막 원소가 이 자리로 오므로 그쪽 인덱스를 고쳐준다.
        // 집합이 바뀌면 빌더는 어차피 전부 다시 모은다 — 더티 표시는 통째로 지운다(예전과 같다).
        clearDirtyLocked();

        MeshComponent* pMoved = _listPrimitive.back();
        _listPrimitive[slot]  = pMoved;
        _listPrimitive.pop_back();
        if ( pMoved != pComp )
            pMoved->setPrimitiveIndex( slot );
        pComp->setPrimitiveIndex( MeshComponent::kInvalidPrimitiveIndex );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::markDirty( MeshComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        const vector<MeshComponent*>& listPrimitive = std::as_const( _listPrimitive );
        const uint32                  slot          = pComp->getPrimitiveIndex();
        if ( slot == MeshComponent::kInvalidPrimitiveIndex || slot >= listPrimitive.size() || listPrimitive[slot] != pComp )
            return;
        markSlotDirty( slot );
    }

    void PrimitiveRegistry::markSlotDirty( uint32 slot )
    {
        if ( slot >= _dirtyFlagCapacity )
            return;
        atomic<uint8>& flag = _arrDirtyFlag[slot];
        if ( flag.load( std::memory_order_relaxed ) != 0u )
            return;
        if ( flag.exchange( 1u, std::memory_order_acq_rel ) != 0u )
            return;
        if ( _bAnyDirty.load( std::memory_order_relaxed ) == 0u )
            _bAnyDirty.store( 1u, std::memory_order_release );
    }

    PrimitiveRegistry::~PrimitiveRegistry()
    {
        for ( MeshInstanceBatch* pBatch : _listInstanceBatch )
        {
            if ( pBatch != nullptr && pBatch->_pRegistry == this )
                pBatch->_pRegistry = nullptr;
        }
    }

    void PrimitiveRegistry::addInstanceBatch( MeshInstanceBatch* pBatch )
    {
        if ( pBatch == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( pBatch->_pRegistry == this )
            return;
        SW_LOG_ASSERT( pBatch->_pRegistry == nullptr, "MeshInstanceBatch is already registered to another registry" );
        pBatch->_pRegistry  = this;
        pBatch->_firstEntry = static_cast<uint32>( _listInstanceEntry.size() );
        const uint32 count  = pBatch->getCount();
        _listInstanceEntry.reserve( _listInstanceEntry.size() + count );
        for ( uint32 index = 0; index < count; ++index )
            _listInstanceEntry.push_back( PrimitiveInstanceEntry{ pBatch, index } );
        _listInstanceBatch.push_back( pBatch );
        growDirtyFlags( getSlotCount() );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::removeInstanceBatch( MeshInstanceBatch* pBatch )
    {
        if ( pBatch == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        if ( pBatch->_pRegistry != this )
            return;
        clearDirtyLocked();
        const uint32 first = pBatch->_firstEntry;
        const uint32 count = pBatch->getCount();
        if ( first <= _listInstanceEntry.size() && first + count <= _listInstanceEntry.size() )
            _listInstanceEntry.erase( _listInstanceEntry.begin() + first, _listInstanceEntry.begin() + first + count );
        for ( size_t batchIndex = 0; batchIndex < _listInstanceBatch.size(); ++batchIndex )
        {
            MeshInstanceBatch* pOther = _listInstanceBatch[batchIndex];
            if ( pOther == pBatch )
            {
                _listInstanceBatch[batchIndex] = _listInstanceBatch.back();
                _listInstanceBatch.pop_back();
                --batchIndex;
                continue;
            }
            // 뒤에 있던 배치의 항목이 앞으로 당겨졌다.
            if ( pOther != nullptr && pOther->_firstEntry > first )
                pOther->_firstEntry -= count;
        }
        pBatch->_pRegistry  = nullptr;
        pBatch->_firstEntry = 0;
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::markInstanceDirty( MeshInstanceBatch* pBatch, uint32 index )
    {
        if ( pBatch == nullptr || pBatch->_pRegistry != this || index >= pBatch->getCount() )
            return;
        // 잠금 없이 메시 컴포넌트 수를 읽는다 — 그 수가 그 사이 바뀌면 번호가 어긋나지만, 그 변경은 집합 세대를 올려
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
        std::scoped_lock<mutex> lock{ _mutex };
        clearDirtyLocked();
    }

    void PrimitiveRegistry::consumeDirty( vector<uint32>& outListSlot )
    {
        outListSlot.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        if ( hasDirty() == false )
            return;
        // "하나라도" 를 훑기 **전에** 내린다 — 훑는 동안 워커가 새로 찍으면 다시 서서 다음 프레임에 잡힌다.
        // (이미 지난 칸이면 그 프레임엔 헛훑기 한 번, 아직 안 지난 칸이면 이번에 잡힌다 — 어느 쪽도 잃지 않는다.)
        _bAnyDirty.store( 0u, std::memory_order_release );
        // 플래그 배열을 훑는다 — 프리미티브 수만큼의 바이트 읽기라 8000 개에 몇 us 다. 선 칸만 exchange 한다.
        const uint32 count = MathUtil::min( getSlotCount(), _dirtyFlagCapacity );
        for ( uint32 slot = 0; slot < count; ++slot )
        {
            if ( _arrDirtyFlag[slot].load( std::memory_order_relaxed ) == 0u )
                continue;
            if ( _arrDirtyFlag[slot].exchange( 0u, std::memory_order_acq_rel ) == 0u )
                continue;
            outListSlot.push_back( slot );
        }
    }

    void PrimitiveRegistry::clearDirtyLocked()
    {
        _bAnyDirty.store( 0u, std::memory_order_release );
        const uint32 count = getSlotCount();
        for ( uint32 slot = 0; slot < count && slot < _dirtyFlagCapacity; ++slot )
        {
            if ( _arrDirtyFlag[slot].load( std::memory_order_relaxed ) != 0u )
                _arrDirtyFlag[slot].store( 0u, std::memory_order_release );
        }
    }
} // namespace sw
