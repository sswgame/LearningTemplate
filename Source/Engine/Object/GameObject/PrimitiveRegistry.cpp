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
        _listPrimitive.push_back( pComp );
        pComp->setPrimitiveIndex( slot );
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
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
        MeshComponent* pMoved = _listPrimitive.back();
        _listPrimitive[slot]  = pMoved;
        _listPrimitive.pop_back();
        if ( pMoved != pComp )
            pMoved->setPrimitiveIndex( slot );
        pComp->setPrimitiveIndex( MeshComponent::kInvalidPrimitiveIndex );

        // 인덱스가 섞였으므로 더티 목록은 더 이상 믿을 수 없다. 집합 세대를 올려 전체 재구축시킨다.
        for ( uint32 dirtySlot : _listDirty )
        {
            if ( dirtySlot < _listPrimitive.size() && _listPrimitive[dirtySlot] != nullptr )
                _listPrimitive[dirtySlot]->setRenderStateDirty( false );
        }
        _listDirty.clear();
        _setGeneration.fetch_add( 1, std::memory_order_relaxed );
    }

    void PrimitiveRegistry::markDirty( MeshComponent* pComp )
    {
        if ( pComp == nullptr || pComp->isRenderStateDirty() )
            return;

        std::scoped_lock<mutex> lock{ _mutex };
        const uint32            slot = pComp->getPrimitiveIndex();
        if ( slot == MeshComponent::kInvalidPrimitiveIndex || slot >= _listPrimitive.size() ||
             _listPrimitive[slot] != pComp )
            return;
        if ( pComp->isRenderStateDirty() )
            return;

        pComp->setRenderStateDirty( true );
        _listDirty.push_back( slot );
    }

    bool PrimitiveRegistry::hasDirty() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _listDirty.empty() == false;
    }

    void PrimitiveRegistry::clearDirty()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        for ( uint32 slot : _listDirty )
        {
            if ( slot < _listPrimitive.size() && _listPrimitive[slot] != nullptr )
                _listPrimitive[slot]->setRenderStateDirty( false );
        }
        _listDirty.clear();
    }
} // namespace sw
