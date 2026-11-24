#include "pch.h"

#include "Engine/Object/Component/SceneMoveDispatch.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformPage.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SceneMoveDispatch::SceneMoveDispatch()
        : _sharedLock{}
        , _arrQueue{}
    {
    }

    void SceneMoveDispatch::queueMoved( SceneComponent& component, uint8 consumerMask )
    {
        if ( component._pManager == nullptr )
            return;
        uint8& queued = component._pTransformPage->_arrMoveQueued[component.getPageIndex()];
        queued        = static_cast<uint8>( queued | consumerMask );
        component._pManager->getMoveDispatch().push( component._transformSlot, consumerMask );
    }

    void SceneMoveDispatch::push( uint32 slot, uint8 consumerMask )
    {
        const uint32 scratch = engine::getParallelScratchSlot();
        for ( uint32 index = 0; index < kMaxConsumerCount; ++index )
        {
            if ( ( consumerMask & ( 1u << index ) ) == 0 )
                continue;
            ConsumerQueue& queue = _arrQueue[index];
            if ( scratch < queue._listScratch.size() )
            {
                queue._listScratch.data()[scratch].push_back( slot ); // 스레드마다 제 칸만 — 바깥 목록은 구독 때 잡고 바뀌지 않는다
                continue;
            }
            ScopedLock<SpinLock> lock{ _sharedLock };
            queue._listShared.push_back( slot );
        }
    }

    uint32 SceneMoveDispatch::addConsumer()
    {
        for ( uint32 index = 0; index < kMaxConsumerCount; ++index )
        {
            ConsumerQueue& queue = _arrQueue[index];
            if ( queue._bUsed )
                continue;
            queue._bUsed = true;
            queue._listScratch.clear();
            queue._listScratch.resize( engine::getParallelScratchSlotCount() ); // 워커마다 자기 칸만 쓴다 — 여기서 한 번 잡고 도는 동안 바뀌지 않는다
            return index;
        }
        reportFatalError( "SceneMoveDispatch is out of consumer slots (%# per manager)", kMaxConsumerCount );
        return 0;
    }

    void SceneMoveDispatch::removeConsumer( uint32 consumerIndex )
    {
        _arrQueue[consumerIndex] = ConsumerQueue{};
    }

    SceneMoveConsumer::SceneMoveConsumer( GameObjectManager& manager )
        : _pDispatch{ &manager.getMoveDispatch() }
        , _pManager{ &manager }
        , _index{ manager.getMoveDispatch().addConsumer() }
    {
    }

    SceneMoveConsumer::~SceneMoveConsumer()
    {
        _pDispatch->removeConsumer( _index );
    }

    void SceneMoveConsumer::track( const SceneComponent& component )
    {
        SW_ASSERT( component._pManager == _pManager );
        const uint8         bit      = static_cast<uint8>( 1u << _index );
        const uint32        index    = component.getPageIndex();
        SceneTransformPage& page     = *component._pTransformPage;
        page._arrMoveInterest[index] = static_cast<uint8>( page._arrMoveInterest[index] | bit );
        if ( ( page._arrMoveQueued[index] & bit ) != 0 )
            return;
        page._arrMoveQueued[index] = static_cast<uint8>( page._arrMoveQueued[index] | bit );
        _pDispatch->push( component._transformSlot, bit );
    }

    void SceneMoveConsumer::untrack( uint32 slot, const SceneComponent* pComponent )
    {
        if ( isAlive( slot, pComponent ) == false )
            return; // 칸이 비었거나 다른 컴포넌트의 것이다 — 비트는 칸을 놓을 때 이미 내려갔다
        const uint8         keep     = static_cast<uint8>( ~( 1u << _index ) );
        const uint32        index    = slot & SceneTransformPage::kSlotMask;
        SceneTransformPage& page     = *engine::getSubsystem<SceneTransformStorage>().findPage( slot );
        page._arrMoveInterest[index] = static_cast<uint8>( page._arrMoveInterest[index] & keep );
        page._arrMoveQueued[index]   = static_cast<uint8>( page._arrMoveQueued[index] & keep ); // 줄에 남은 칸 번호는 꺼낼 때 건너뛴다
    }

    bool SceneMoveConsumer::isAlive( uint32 slot, const SceneComponent* pComponent )
    {
        const SceneTransformPage* pPage = engine::getSubsystem<SceneTransformStorage>().findPage( slot );
        return pPage != nullptr && pComponent != nullptr && pPage->_arrOwner[slot & SceneTransformPage::kSlotMask] == pComponent;
    }

    void SceneMoveConsumer::takeMoved( vector<SceneComponent*>& outListMoved )
    {
        outListMoved.clear();
        const uint8                  bit       = static_cast<uint8>( 1u << _index );
        const SceneTransformStorage& storage   = engine::getSubsystem<SceneTransformStorage>();
        const uint8                  keep      = static_cast<uint8>( ~bit );
        const auto                   takeSlots = [&]( vector<uint32>& listSlot )
        {
            for ( const uint32 slot : listSlot )
            {
                SceneTransformPage* pPage  = storage.findPage( slot );
                const uint32        index  = slot & SceneTransformPage::kSlotMask;
                SceneComponent*     pOwner = pPage != nullptr ? pPage->_arrOwner[index] : nullptr;
                // 끊었거나 사라졌거나(비트가 없다) 다른 매니저의 컴포넌트가 그 칸을 다시 받았다 — 그 매니저의 줄을 건드리지 않는다.
                const bool bMine = pOwner != nullptr && pOwner->_pManager == _pManager && ( pPage->_arrMoveQueued[index] & bit ) != 0;
                if ( bMine == false )
                    continue;
                pPage->_arrMoveQueued[index] = static_cast<uint8>( pPage->_arrMoveQueued[index] & keep );
                if ( ( pPage->_arrMoveInterest[index] & bit ) != 0 )
                    outListMoved.push_back( pOwner );
            }
            listSlot.clear();
        };
        SceneMoveDispatch::ConsumerQueue& queue = _pDispatch->_arrQueue[_index];
        for ( vector<uint32>& listSlot : queue._listScratch )
        {
            takeSlots( listSlot );
        }
        ScopedLock<SpinLock> lock{ _pDispatch->_sharedLock };
        takeSlots( queue._listShared );
    }
} // namespace sw
