/**
 * @file GameObjectManagerTick.cpp
 * @brief GameObjectManager 의 프레임 경로입니다(tick 의 단계 · 병렬 틱 디스패치 · 트랜스폼 배치/큐 적용 · 지연 큐).
 * @details 수명(생성 · 이름 · 파괴 · 팩토리)은 `GameObjectManager.cpp` 에 있습니다. 이 파일은 매 프레임 도는 것만 담습니다.
 */
#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct GameObjectManagerTickInternal
        {
            /** @brief 항목(오브젝트)이 이 수보다 적으면 나누지 않고 이 스레드가 돕니다. 디스패치 바닥보다 작은 일입니다. */
            static constexpr uint32 kParallelTickThreshold = 16;

            /** @brief 핸들을 씬 컴포넌트로 풉니다(리플렉션 캐스트 없이 플래그 비트로). 씬 컴포넌트가 아니거나 죽었으면 nullptr 입니다. */
            static SceneComponent* resolveSceneComponent( GameObjectManager* pManager, ComponentHandle handle )
            {
                Component* pComp = pManager->resolveComponent( handle );
                if ( pComp == nullptr || pComp->isSceneComponent() == false || pComp->isPendingKill() )
                    return nullptr;
                return static_cast<SceneComponent*>( pComp );
            }

            /**
             * @brief 쓰기 [start, end) 를 순서대로 적용합니다. 워커에서 불립니다. 죽었거나 씬 컴포넌트가 아닌 건은 건너뜁니다.
             * @param bTrustTarget 건이 든 `_pTarget` 을 믿고 핸들을 풀지 않을지 여부. 같은 `tick()` 안에서 쌓이고 적용되는 틱 큐만 true 입니다.
             */
            static uint32 applyTransformWriteRange( GameObjectManager* pManager, const SceneTransformWrite* pWrite, uint32 start, uint32 end, bool bTrustTarget )
            {
                uint32 changedCount = 0;
                for ( uint32 index = start; index < end; ++index )
                {
                    SceneComponent* pScene = nullptr;
                    if ( bTrustTarget && pWrite[index]._pTarget != nullptr )
                    {
                        pScene = pWrite[index]._pTarget;
                        if ( pScene->isPendingKill() )
                            continue;
                    }
                    else
                        pScene = resolveSceneComponent( pManager, pWrite[index]._handle );
                    if ( pScene != nullptr && pScene->applyTransformWrite( pWrite[index] ) )
                        ++changedCount;
                }
                return changedCount;
            }

            /** @brief 항목 하나를 돌립니다. 주 틱이면 `onTick`, 서브틱이면 `onSubTick` 입니다. 살아 있고 켜져 있는지는 부르는 쪽이 이미 봤습니다. */
            static void runTickItem( float32 deltaTime, const TickItem& item )
            {
                Component* pComp = item._pComponent;
                if ( item._subTickId == 0 )
                {
                    if ( pComp->canEverTick() )
                        pComp->onTick( deltaTime );
                }
                else
                {
                    if ( pComp->isSubTickActive( item._subTickId ) )
                        pComp->onSubTick( item._subTickId, deltaTime );
                }
            }

            /**
             * @brief 오브젝트 하나의 그룹 `group` 항목을 순서대로 틱합니다. 워커에서 불립니다.
             * @details 항목은 등록부가 지은 것이라 살아 있는 컴포넌트만 가리킵니다(지워진 컴포넌트는 소유 오브젝트가 표시되어 틱 전에
             *          다시 지어집니다). 삭제 대기 · 비활성은 여기서 건너뜁니다.
             */
            static void tickObjectGroup( float32 deltaTime, GameObject* pObj, uint32 group )
            {
                if ( pObj == nullptr || pObj->isPendingKill() || pObj->isActiveInHierarchy() == false )
                    return;
                const TickItemList& listItem = pObj->getTickItems();
                const uint32        end      = pObj->getTickGroupBegin( group + 1 );
                for ( uint32 index = pObj->getTickGroupBegin( group ); index < end; ++index )
                {
                    const TickItem& item  = listItem[index];
                    Component*      pComp = item._pComponent;
                    // 소유자의 활성은 위에서 봤다. 여기서는 컴포넌트 자기 비트만 본다.
                    if ( pComp == nullptr || pComp->isPendingKill() || pComp->isSelfActive() == false )
                        continue;
                    runTickItem( deltaTime, item );
                }
            }

            /** @brief 한 그룹의 오브젝트 목록을 [start, end) 로 나눠 도는 잡 본문입니다. 워커는 포인터 배열만 받습니다. */
            struct ObjectGroupTick
            {
                GameObject* const* _ppObject{ nullptr };
                float32            _deltaTime{ 0.0f };
                uint32             _group{ 0 };

                void tickRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                        tickObjectGroup( _deltaTime, _ppObject[index], _group );
                }
            };

            /**
             * @brief 선행 조건 웨이브 하나의 항목 [start, end) 를 도는 잡 본문입니다. 항목마다 오브젝트가 다르므로 소유자도 봅니다.
             */
            struct WaveTick
            {
                const TickItem* _pItem{ nullptr };
                float32         _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const TickItem& item  = _pItem[index];
                        Component*      pComp = item._pComponent;
                        if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
                            continue;
                        GameObject* pOwner = pComp->getOwner();
                        if ( pOwner == nullptr || pOwner->isPendingKill() )
                            continue;
                        runTickItem( _deltaTime, item );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    void GameObjectManager::tick( float32 deltaTime )
    {
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().dispatchMainThreadTasks();
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.destroy" );
            processDeferredDestruction();
        }
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.merge" );
            mergePendingAdds();
        }

        if ( _listGameObject.empty() )
            return;

        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransforms" );
            flushSceneTransforms();
        }

        // 틱 중의 세터가 쌓을 쓰기 큐를 슬롯 수만큼 미리 잡아 둔다(워커는 자기 칸만 만진다).
        _transformHierarchy.beginQueuedWrites();
        _bParallelTransformReadOnly.store( true, std::memory_order_relaxed );
        _bTicking.store( true, std::memory_order_release );

        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.components" );
            tickComponents( deltaTime );

            if ( engine::areEngineServicesBound() )
                engine::getTaskManager().waitAll();
        }

        _bParallelTransformReadOnly.store( false, std::memory_order_relaxed );
        _bTicking.store( false, std::memory_order_release );

        // 지연된 계층 변경(attach · detach)을 인스턴스가 살아 있는 동안 먼저 적용한다. 지연 큐 · 파괴보다 앞이다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.deferredTransforms" );
            {
                std::scoped_lock<mutex> lock{ _deferredTransformMutex };
                if ( _listDeferredTransformUpdate.empty() == false )
                    _listProcessingTransform.swap( _listDeferredTransformUpdate );
            }
            for ( TransformUpdateDelegate& func : _listProcessingTransform )
            {
                if ( func.isBound() )
                    func();
            }
            _listProcessingTransform.clear();
        }

        // 틱 중의 세터가 슬롯 큐에 쌓은 쓰기를 적용한다. 구조 변경(위의 지연 attach · detach)이 끝난 뒤라 부모 사슬이 안정됐다.
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.queuedTransforms" );
            applyQueuedTransformWrites();
        }

        // 병렬 onTick 이 미룬 스폰 · 데미지 · 태그.
        {
            std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
            if ( _listDeferredPostTickUpdate.empty() == false )
                _listProcessingPostTick.swap( _listDeferredPostTickUpdate );
        }
        for ( PostTickDelegate& func : _listProcessingPostTick )
        {
            if ( func.isBound() )
                func();
        }
        _listProcessingPostTick.clear();
        mergePendingAdds();

        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }

        processDeferredDestruction();
    }

    void GameObjectManager::tickComponents( float32 deltaTime )
    {
        {
            // 멤버십이 바뀐 오브젝트만 항목을 다시 짓는다. 씬 전체를 훑지 않는다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.registry" );
            if ( _tickRegistry.refresh( *this ) )
                _tickWaveBuildCount.fetch_add( 1, std::memory_order_relaxed );
        }

        if ( _tickRegistry.hasPrerequisites() == false )
        {
            // 보통 경로다. 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눈다. 한 오브젝트의 항목은 한 워커가 (순서 키 순으로)
            // 돌므로 같은 오브젝트의 컴포넌트 둘이 동시에 돌지 않는다.
            for ( uint32 group = 0; group < TickRegistry::kGroupCount; ++group )
            {
                const vector<GameObject*>& listObject = _tickRegistry.getObjects( group );
                if ( listObject.empty() )
                    continue;
                GameObjectManagerTickInternal::ObjectGroupTick job{};
                job._ppObject  = listObject.data();
                job._deltaTime = deltaTime;
                job._group     = group;
                engine::runParallel( static_cast<uint32>( listObject.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                     SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::ObjectGroupTick::tickRange, &job ) );
            }
            return;
        }

        // 선행 조건이 있다. 계층을 넘는 순서는 오브젝트 단위로 표현할 수 없으므로 등록부가 지은 DAG 웨이브로 간다(드물다).
        // 웨이브 캐시는 등록부 세대로 무효화한다. 항목은 등록부의 것이라 세대가 같은 동안 살아 있다.
        if ( _lastWaveGeneration != _tickRegistry.getGeneration() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.waves" );
            _lastWaveGeneration = _tickRegistry.getGeneration();
            _tickRegistry.buildPrerequisiteWaves( _listCachedTickWave );
        }

        for ( const TickWave& wave : _listCachedTickWave )
        {
            if ( wave.empty() )
                continue;
            GameObjectManagerTickInternal::WaveTick job{};
            job._pItem     = wave.data();
            job._deltaTime = deltaTime;
            engine::runParallel( static_cast<uint32>( wave.size() ), GameObjectManagerTickInternal::kParallelTickThreshold,
                                 SW_DELEGATE_METHOD( ParallelBlockDelegate, &GameObjectManagerTickInternal::WaveTick::tickRange, &job ) );
        }
    }

    uint32 GameObjectManager::applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count )
    {
        if ( pWrite == nullptr || count == 0 )
            return 0;

        // 틱 중이면 세터로 돌린다. 세터가 지연 경로를 탄다. 배치의 병렬 쓰기는 틱 밖에서만 안전하다.
        if ( isStructuralMutationFrozen() )
        {
            uint32 deferredCount = 0;
            for ( uint32 index = 0; index < count; ++index )
            {
                SceneComponent* pScene = GameObjectManagerTickInternal::resolveSceneComponent( this, pWrite[index]._handle );
                if ( pScene == nullptr )
                    continue;
                if ( pWrite[index]._bSetPosition != SW_FALSE )
                    pScene->setLocalPosition( pWrite[index]._localPosition );
                if ( pWrite[index]._bSetRotation != SW_FALSE )
                    pScene->setLocalRotation( pWrite[index]._localRotation );
                if ( pWrite[index]._bSetScale != SW_FALSE )
                    pScene->setLocalScale( pWrite[index]._localScale );
                ++deferredCount;
            }
            return deferredCount;
        }

        // 워커는 컨테이너를 만지지 않는다. 포인터만 받는다. 핸들 해석은 슬롯 표라 락이 없고, 쓰기는 자기 건의
        // 컴포넌트(와 부모 · 자식의 더티 바이트)뿐이다.
        struct WriteJob
        {
            GameObjectManager*         _pManager{ nullptr };
            const SceneTransformWrite* _pWrite{ nullptr };
            atomic<uint32>             _changedCount{ 0 };

            void applyRange( uint32 start, uint32 end )
            {
                const uint32 changedCount = GameObjectManagerTickInternal::applyTransformWriteRange( _pManager, _pWrite, start, end, false );
                if ( changedCount > 0 )
                    _changedCount.fetch_add( changedCount, std::memory_order_relaxed );
            }
        };
        WriteJob job{};
        job._pManager = this;
        job._pWrite   = pWrite;
        // 워커가 올리는 더티 루트는 슬롯별 스크래치로 간다. 앞에서 슬롯 수만큼 잡아 두고, 끝나면 본 목록으로 합친다.
        _transformHierarchy.mergeQueuedDirtyRoots();
        engine::runParallel( count, SceneTransformHierarchy::kParallelWriteCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &WriteJob::applyRange, &job ) );
        _transformHierarchy.mergeQueuedDirtyRoots();

        const uint32 changedCount = job._changedCount.load( std::memory_order_relaxed );
        if ( changedCount > 0 )
            _transformHierarchy.notifyDirtied();
        return changedCount;
    }

    void GameObjectManager::queueTransformWrite( const SceneTransformWrite& write )
    {
        if ( _transformHierarchy.queueWriteParallel( write ) )
            return;

        // 슬롯이 준비되지 않았다. 틱 밖에서 읽기 전용 구간을 흉내 내는 곳(테스트 · 도구)뿐이다. 예전 지연 경로로 간다.
        deferTransformUpdate( [this, write]()
        {
            if ( GameObjectManagerTickInternal::applyTransformWriteRange( this, &write, 0, 1, false ) > 0 )
                _transformHierarchy.notifyDirtied();
        } );
    }

    uint32 GameObjectManager::applyQueuedTransformWrites()
    {
        const uint32                 slotCount  = _transformHierarchy.getQueuedWriteSlotCount();
        vector<SceneTransformWrite>* pSlot      = _transformHierarchy.getQueuedWriteSlots();
        uint32                       totalCount = 0;
        // 비어 있지 않은 슬롯만 잡을 낸다. 도우미 슬롯(렌더 · 로더 스레드 몫)은 대개 비어 있다.
        _listActiveWriteSlot.clear();
        for ( uint32 slot = 0; slot < slotCount; ++slot )
        {
            if ( pSlot[slot].empty() )
                continue;
            totalCount += static_cast<uint32>( pSlot[slot].size() );
            _listActiveWriteSlot.push_back( slot );
        }
        if ( totalCount == 0 )
            return 0;

        // 슬롯 하나가 잡 하나다. 같은 슬롯의 건은 쌓인 순서대로 한 워커가 적용한다(마지막 값이 이긴다).
        struct SlotWriteJob
        {
            GameObjectManager*           _pManager{ nullptr };
            vector<SceneTransformWrite>* _pSlot{ nullptr };
            const uint32*                _pActiveSlot{ nullptr };
            atomic<uint32>               _changedCount{ 0 };

            void applyRange( uint32 start, uint32 end )
            {
                uint32 changedCount = 0;
                for ( uint32 index = start; index < end; ++index )
                {
                    const vector<SceneTransformWrite>& listWrite = std::as_const( _pSlot[_pActiveSlot[index]] );
                    changedCount += GameObjectManagerTickInternal::applyTransformWriteRange( _pManager, listWrite.data(), 0, static_cast<uint32>( listWrite.size() ), true );
                }
                if ( changedCount > 0 )
                    _changedCount.fetch_add( changedCount, std::memory_order_relaxed );
            }
        };
        SlotWriteJob job{};
        job._pManager            = this;
        job._pSlot               = pSlot;
        job._pActiveSlot         = _listActiveWriteSlot.data();
        const uint32 activeCount = static_cast<uint32>( _listActiveWriteSlot.size() );
        _transformHierarchy.mergeQueuedDirtyRoots();
        if ( totalCount < SceneTransformHierarchy::kParallelWriteCount )
            job.applyRange( 0, activeCount );
        else
            engine::runParallel( activeCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &SlotWriteJob::applyRange, &job ) );
        _transformHierarchy.mergeQueuedDirtyRoots();
        _transformHierarchy.clearQueuedWrites();

        const uint32 changedCount = job._changedCount.load( std::memory_order_relaxed );
        if ( changedCount > 0 )
            _transformHierarchy.notifyDirtied();
        return changedCount;
    }

    void GameObjectManager::deferTransformUpdate( TransformUpdateDelegate func )
    {
        if ( func.isBound() == false )
            return;
        std::scoped_lock<mutex> lock{ _deferredTransformMutex };
        _listDeferredTransformUpdate.push_back( std::move( func ) );
    }

    void GameObjectManager::deferPostTick( PostTickDelegate func )
    {
        if ( func.isBound() == false )
            return;
        std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
        _listDeferredPostTickUpdate.push_back( std::move( func ) );
    }

    void GameObjectManager::executeOrDeferPostTick( PostTickDelegate func )
    {
        if ( func.isBound() == false )
            return;
        if ( isStructuralMutationFrozen() )
            deferPostTick( std::move( func ) );
        else
            func();
    }

    void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.registerRoot( pComp );
    }

    void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
    {
        _transformHierarchy.unregisterRoot( pComp );
    }
} // namespace sw
