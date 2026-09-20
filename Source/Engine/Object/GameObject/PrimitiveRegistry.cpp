#include "pch.h"

#include "Engine/Object/GameObject/PrimitiveRegistry.h"

#include "Engine/Object/Component/3D/MeshComponent.h"

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
        growDirtyFlags( slot + 1 );
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

        // **const 로 읽는다.** 워커 여럿이 동시에 들어오는 자리라, 비-const operator[] 는 컨테이너 레이스
        // 탐지기에 "쓰기" 로 잡힌다(Debug 에서 셋이 동시에 찍자 바로 울렸다).
        const vector<MeshComponent*>& listPrimitive = std::as_const( _listPrimitive );
        const uint32                  slot          = pComp->getPrimitiveIndex();
        if ( slot == MeshComponent::kInvalidPrimitiveIndex || slot >= _dirtyFlagCapacity || slot >= listPrimitive.size() ||
             listPrimitive[slot] != pComp )
            return;

        // 먼저 읽고, 서 있지 않을 때만 쓴다. 워커 여럿이 같은 라인의 이웃 칸을 찍는 프레임에 무조건 exchange 하면
        // 그 라인이 코어 사이를 오간다 — 이미 선 칸은 읽기 한 번으로 끝낸다.
        atomic<uint8>& flag = _arrDirtyFlag[slot];
        if ( flag.load( std::memory_order_relaxed ) != 0u )
            return;
        if ( flag.exchange( 1u, std::memory_order_acq_rel ) != 0u )
            return;
        // 프레임에 한 번만 쓰인다 — 두 번째부터는 읽기다.
        if ( _bAnyDirty.load( std::memory_order_relaxed ) == 0u )
            _bAnyDirty.store( 1u, std::memory_order_release );
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
        const uint32 count = static_cast<uint32>( _listPrimitive.size() );
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
        const uint32 count = static_cast<uint32>( _listPrimitive.size() );
        for ( uint32 slot = 0; slot < count && slot < _dirtyFlagCapacity; ++slot )
        {
            if ( _arrDirtyFlag[slot].load( std::memory_order_relaxed ) != 0u )
                _arrDirtyFlag[slot].store( 0u, std::memory_order_release );
        }
    }
} // namespace sw
