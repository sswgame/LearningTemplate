/**
 * @file TestGameObjectMocks.h
 * @brief GameObject·Component 테스트가 함께 쓰는 모의 컴포넌트들.
 * @details 목을 이 헤더에 두므로 GameObject·Component 시험 파일을 주제별로 나눌 수 있다.
 *
 *          **이 목들은 코드젠을 쓰지 않는다.** `REFLECT_BODY()` 가 선언만 하고 `StaticType()` 은 아래에서 손으로
 *          정의한다 — 테스트 로컬 `TypeInfo` 를 만들어 쓰기 때문이다(RTTI 없음). 그래서 그 `TypeInfo` 캐시는
 *          **딱 하나여야 한다**: `makeMockComponentTypeInfo` 의 정의는 `TestGameObjectMocks.cpp` 에 있다.
 *          익명 네임스페이스로 헤더에 두면 include 한 TU 마다 캐시가 갈려, 같은 이름의 `TypeInfo` 가 여러 개
 *          레지스트리에 등록된다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    /**
     * @brief 테스트 전용 TypeInfo 를 만들거나 캐시에서 반환합니다.
     * @details 캐시가 하나여야 하므로 정의는 `TestGameObjectMocks.cpp` 에 있다(파일 머리 주석 참고).
     */
    const TypeInfo* makeMockComponentTypeInfo( Component* ( *addComponent )(GameObject*), hashed_string shortName, hashed_string fqn, size_t size,
                                               hashed_string parentFqn = hashed_string{} );

    /**
     * @brief 모의 컴포넌트 TypeInfo 를 리플렉션 표에 올립니다. 생성 함수(`TypeInfo::_addComponent`)가 함께 올라 `addComponentByName` · 씬 로드가 만들 수 있습니다.
     * @details 표는 프로세스에 하나라 매니저마다 부를 일이 없습니다. 두 번 불러도 됩니다.
     */
    void RegisterMockComponents();

    class MockMeshComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        string _meshName = "CubeMesh";
        int32  _tickCount{ 0 };

        GameObjectManager* _pTickDestroyManager{ nullptr };
        GameObject*        _pTickDestroyObject{ nullptr };
        GameObject*        _pTickRemoveOwner{ nullptr };
        Component*         _pTickRemoveComp{ nullptr };
        SceneComponent*    _pTickAttachChild{ nullptr };
        SceneComponent*    _pTickAttachParent{ nullptr };
        SceneComponent*    _pTickMoveComp{ nullptr };
        float3             _tickMovePos{};
        GameObjectManager* _pBeginPlaySpawnManager{ nullptr }; ///< 설정되면 onBeginPlay 가 이 매니저에 오브젝트 하나를 만든다
        bool               _bBeginPlayAddsTag{ false };        ///< 켜면 onBeginPlay 가 소유자에 태그 "BeginPlayAdded" 를 붙인다(태그 컴포넌트가 붙는다)
        int32              _beginPlayCount{ 0 };
        string             _meshNameAtBeginPlay;               ///< onBeginPlay 가 본 `_meshName`(붙인 뒤 세팅한 값을 보는지)
        int32*             _pEndPlayCount{ nullptr };          ///< onEndPlay 횟수를 적을 곳 — 컴포넌트가 사라진 뒤에도 읽도록 밖에 둔다
        GameObject*        _pTickClearOwner{ nullptr };        ///< 설정되면 틱이 그 오브젝트의 컴포넌트를 모두 비운다(`clearComponents`)
        size_t             _componentCountAfterTickClear{ 0 }; ///< 비운 **직후**(아직 틱 안) 그 오브젝트의 목록 길이 — 미뤄졌으면 그대로다
        GameObject*        _pTickLoadTarget{ nullptr };        ///< 설정되면 틱이 그 오브젝트에 `_tickLoadXml` 을 제자리로 읽는다
        string             _tickLoadXml;
        int32              _tickLoadResult{ -1 };       ///< 그 결과(1 성공 · 0 실패 · -1 안 함)
        GameObject*        _pTickAdoptChild{ nullptr }; ///< 설정되면 틱이 이 오브젝트에 씬 컴포넌트를 붙이고 이어 `_pTickAdoptParent` 에 붙인다
        GameObject*        _pTickAdoptParent{ nullptr };
        TagComponent*      _pTickTagComponent{ nullptr };   ///< 설정되면 틱이 그 컴포넌트에 태그 "TickAdded" 를 더한다
        bool               _bTickTagVisibleInTick{ false }; ///< 더한 **직후**(아직 틱 안) 그 태그가 보였는지 — 미뤘으면 아직 안 보인다
        PrefabCache*       _pTickPrefabs{ nullptr };        ///< 설정되면 틱이 `_tickSpawnPath` 프리팹을 `_pTickSpawnManager` 에 스폰한다
        GameObjectManager* _pTickSpawnManager{ nullptr };
        string             _tickSpawnPath;
        GameObject*        _pTickSpawned{ nullptr };           ///< 스폰이 돌려준 오브젝트
        GameObject*        _pTickRenameTarget{ nullptr };      ///< 설정되면 틱이 그 오브젝트의 이름을 "TickRenamed" 로 바꾼다
        bool               _bTickRenameVisibleInTick{ false }; ///< 바꾼 **직후**(아직 틱 안) 새 이름이 보였는지
        Component*         _pTickStopTicking{ nullptr };       ///< 설정되면 틱이 그 컴포넌트의 틱을 끈다(`setCanEverTick( false )`)
        bool               _bTickStopVisibleInTick{ false };   ///< 끈 **직후**(아직 틱 안) 꺼진 것이 보였는지
        /**
         * @brief 설정되면 틱이 그 컴포넌트의 서브틱을 바꾼다 — 3 을 등록하고, 1 에 `_tickSubTickPrerequisite` 를 선행 조건으로 더하고, 1 을 끄고, 2 를 해제한다.
         * @details 시험이 1 · 2 를 미리 등록해 둔다. 바꾼 **직후**(아직 틱 안) 목록에 보였는지를 하나씩 적는다 — 미뤘으면 넷 다 아직 안 보인다.
         *          원자 마스크는 바로 바뀌어야 한다(끈 서브틱 · 해제한 서브틱은 이번 틱의 남은 항목에서 곧바로 빠진다) — 그것도 적는다.
         */
        Component*    _pTickSubTickTarget{ nullptr };
        SubTickHandle _tickSubTickPrerequisite{};
        bool          _bTickSubTickAddVisibleInTick{ false };
        bool          _bTickSubTickPrerequisiteVisibleInTick{ false };
        bool          _bTickSubTickOffVisibleInTick{ false };
        bool          _bTickSubTickRemoveVisibleInTick{ false };
        bool          _bTickSubTickMaskOffInTick{ false }; ///< 끄고 해제한 1 · 2 가 틱 안에서 곧바로 꺼져 보였는지(마스크)

        /** @brief @p pComponent 에 @p subTickId 서브틱이 등록돼 있으면 그 정보, 없으면 nullptr 입니다. */
        static const SubTickInfo* findSubTick( const Component* pComponent, uint32 subTickId )
        {
            for ( const SubTickInfo& info : pComponent->getAllSubTicks() )
            {
                if ( info._subTickId == subTickId )
                    return &info;
            }
            return nullptr;
        }

        /** @brief 부른 횟수를 세고, 설정된 매니저가 있으면 오브젝트 하나를 만듭니다(onBeginPlay 안의 스폰). */
        void onBeginPlay() override
        {
            Component::onBeginPlay();
            ++_beginPlayCount;
            _meshNameAtBeginPlay = _meshName;
            if ( _pBeginPlaySpawnManager != nullptr )
                _pBeginPlaySpawnManager->createGameObject( hashed_string( "SpawnedInBeginPlay" ) );
            if ( _bBeginPlayAddsTag && getOwner() != nullptr )
                getOwner()->addTag( "BeginPlayAdded"_tag );
        }

        void onEndPlay() override
        {
            if ( _pEndPlayCount != nullptr )
                ++( *_pEndPlayCount );
            Component::onEndPlay();
        }

        /** @brief 틱마다 _tickCount 를 증가시키고, 설정된 틱 액션을 실행합니다. */
        virtual void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _tickCount++;
            if ( _pTickMoveComp != nullptr )
                _pTickMoveComp->setLocalPosition( _tickMovePos );
            if ( _pTickDestroyManager != nullptr && _pTickDestroyObject != nullptr )
                _pTickDestroyManager->destroyObject( _pTickDestroyObject );
            if ( _pTickRemoveOwner != nullptr && _pTickRemoveComp != nullptr )
                (void)_pTickRemoveOwner->removeComponent( _pTickRemoveComp ); // 지워졌는지는 시험이 본다
            if ( _pTickAttachChild != nullptr && _pTickAttachParent != nullptr )
                (void)_pTickAttachChild->attachToComponent( _pTickAttachParent ); // 붙었는지는 시험이 계층으로 본다
            if ( _pTickClearOwner != nullptr )
            {
                _pTickClearOwner->clearComponents();
                _componentCountAfterTickClear = _pTickClearOwner->getComponents().size(); // 삭제 대기도 센다 — 해제됐으면 목록에서 빠졌다
            }
            if ( _pTickLoadTarget != nullptr )
                _tickLoadResult = ObjectStateSerializer::loadFromXmlString( _pTickLoadTarget, _tickLoadXml ) ? 1 : 0;
            if ( _pTickRenameTarget != nullptr )
            {
                _pTickRenameTarget->setName( hashed_string( "TickRenamed" ) );
                _bTickRenameVisibleInTick = ( _pTickRenameTarget->getName() == hashed_string( "TickRenamed" ) );
            }
            if ( _pTickStopTicking != nullptr )
            {
                _pTickStopTicking->setCanEverTick( false );
                _bTickStopVisibleInTick = ( _pTickStopTicking->canEverTick() == false );
            }
            if ( _pTickSubTickTarget != nullptr )
            {
                _pTickSubTickTarget->registerSubTick( TickGroup::PostPhysics, 3 );
                _bTickSubTickAddVisibleInTick = ( findSubTick( _pTickSubTickTarget, 3 ) != nullptr );
                (void)_pTickSubTickTarget->addSubTickPrerequisite( 1, _tickSubTickPrerequisite ); // 붙었는지는 시험이 목록으로 본다
                const SubTickInfo* pFirst              = findSubTick( _pTickSubTickTarget, 1 );
                _bTickSubTickPrerequisiteVisibleInTick = ( pFirst != nullptr && pFirst->_listPrerequisite.empty() == false );
                _pTickSubTickTarget->setSubTickActive( 1, false );
                pFirst                        = findSubTick( _pTickSubTickTarget, 1 );
                _bTickSubTickOffVisibleInTick = ( pFirst != nullptr && pFirst->_bActive == SW_FALSE );
                (void)_pTickSubTickTarget->unregisterSubTick( 2 ); // 빠졌는지는 시험이 목록으로 본다
                _bTickSubTickRemoveVisibleInTick = ( findSubTick( _pTickSubTickTarget, 2 ) == nullptr );
                _bTickSubTickMaskOffInTick       = ( _pTickSubTickTarget->isSubTickActive( 1 ) == false && _pTickSubTickTarget->isSubTickActive( 2 ) == false );
            }
            if ( _pTickPrefabs != nullptr && _pTickSpawnManager != nullptr && _pTickSpawned == nullptr )
                _pTickSpawned = _pTickPrefabs->spawn( _pTickSpawnManager, _tickSpawnPath, "TickSpawned" );
            if ( _pTickTagComponent != nullptr )
            {
                _pTickTagComponent->addTag( "TickAdded"_tag );
                _bTickTagVisibleInTick = _pTickTagComponent->hasTag( "TickAdded"_tag );
            }
            if ( _pTickAdoptChild != nullptr && _pTickAdoptParent != nullptr )
            {
                (void)_pTickAdoptChild->addComponent<SceneComponent>();      // 틱 중이라 미뤄진다(nullptr)
                (void)_pTickAdoptChild->attachToParent( _pTickAdoptParent ); // 이것도 미뤄진다 — 붙었는지는 시험이 본다
            }
        }
    };
} // namespace sw

namespace sw
{
    /** @brief MockMeshComponent 의 정적 TypeInfo 를 반환합니다. */
    inline const TypeInfo* MockMeshComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockMeshComponent>, hashed_string( "MockMeshComponent" ),
                                          hashed_string( "sw::MockMeshComponent" ), sizeof( MockMeshComponent ) );
    }

    class MockAudioComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        float32 _volume{ 1.0f };
        int32   _playCount{ 0 };

        /** @brief 틱마다 _playCount 를 증가시킵니다. */
        virtual void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _playCount++;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief MockAudioComponent 의 정적 TypeInfo 를 반환합니다. */
    inline const TypeInfo* MockAudioComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockAudioComponent>, hashed_string( "MockAudioComponent" ),
                                          hashed_string( "sw::MockAudioComponent" ), sizeof( MockAudioComponent ) );
    }

    class MockCallbackComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        hashed_string          _lastChangedProperty;
        int32*                 _pUnregisterCount{ nullptr }; ///< 해제 콜백 횟수를 적을 곳 — 컴포넌트가 사라진 뒤에도 읽도록 밖에 둔다
        int32*                 _pDestroyCount{ nullptr };
        MockCallbackComponent* _pSiblingToRemoveOnUnregister{ nullptr }; ///< 설정되면 해제 콜백이 이 형제를 곧바로 지운다(한 번)

        /** @brief 변경된 프로퍼티 이름을 기록합니다. */
        virtual void onPropertyChanged( hashed_string propertyName ) override
        {
            Component::onPropertyChanged( propertyName );
            _lastChangedProperty = propertyName;
        }

        void onUnregister( GameObjectManager& manager ) override
        {
            if ( _pUnregisterCount != nullptr )
                ++( *_pUnregisterCount );
            // 모듈 콜백이 형제를 지우는 자리다. 짝이 서로를 지우지 않게 상대의 표시를 먼저 지운다.
            MockCallbackComponent* pSibling = _pSiblingToRemoveOnUnregister;
            _pSiblingToRemoveOnUnregister   = nullptr;
            if ( pSibling != nullptr && getOwner() != nullptr )
            {
                pSibling->_pSiblingToRemoveOnUnregister = nullptr;
                (void)getOwner()->removeComponent( pSibling ); // 지워졌는지는 시험이 본다
            }
            Component::onUnregister( manager );
        }

        void onDestroy() override
        {
            if ( _pDestroyCount != nullptr )
                ++( *_pDestroyCount );
            Component::onDestroy();
        }
    };
} // namespace sw

namespace sw
{
    /** @brief MockCallbackComponent 의 정적 TypeInfo 를 반환합니다. */
    inline const TypeInfo* MockCallbackComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockCallbackComponent>, hashed_string( "MockCallbackComponent" ),
                                          hashed_string( "sw::MockCallbackComponent" ),
                                          sizeof( MockCallbackComponent ) );
    }

    class MockTickSceneComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        float3*         _pObservedWorld;
        SceneComponent* _pWatchedComp;     ///< 틱 안에서 이 컴포넌트의 로컬 · 월드 위치를 읽어 둔다 — 다른 오브젝트가 틱 중에 보는 값을 본다
        GameObject*     _pTryParentOnTick; ///< 틱 안에서 자기 오브젝트를 이 오브젝트에 붙여 본다 — 결과는 `_bAttachAcceptedInTick`
        TagID           _probeTag;         ///< `_bClearTagsOnTick` 이 지운 뒤 틱 안에서 아직 보이는지 볼 태그
        float3          _watchedLocalPos;  ///< 틱 안에서 읽은 `_pWatchedComp` 의 로컬 위치
        float3          _watchedWorldPos;  ///< 틱 안에서 읽은 `_pWatchedComp` 의 월드 위치
        float3          _tickLocalPos;
        float3          _tickLocalScale;
        uint8           _bWriteLocalOnTick     : 1;
        uint8           _bWriteScaleOnTick     : 1; ///< 위치에 이어 스케일도 쓴다 — 같은 컴포넌트에 잇따른 두 세터
        uint8           _bWriteTwiceOnTick     : 1; ///< 위치를 두 번 쓴다(먼저 엉뚱한 값) — 마지막 값이 이겨야 한다
        uint8           _bDetachOnTick         : 1; ///< 틱에서 자기를 부모에서 뗀다 — 틱 중이라 미뤄져야 한다
        uint8           _bParentKeptInTick     : 1; ///< 뗀 직후에도 부모가 그대로였는지(미뤄졌는지) 기록한다
        uint8           _bAttachAcceptedInTick : 1; ///< 틱 안의 `attachToParent( _pTryParentOnTick )` 가 true 를 돌려줬는지
        uint8           _bClearTagsOnTick      : 1; ///< 틱에서 소유 오브젝트의 태그를 모두 지운다 — 틱 중이라 미뤄져야 한다
        uint8           _bProbeTagKeptInTick   : 1; ///< 지운 직후에도 `_probeTag` 가 보였는지(미뤄졌는지) 기록한다
        int32           _worldUpdateCount;          ///< `onWorldTransformUpdated` 가 불린 횟수 — 합성 경로가 훅을 빠뜨리지 않는지 본다

        MockTickSceneComponent()
            : _pObservedWorld{ nullptr }
            , _pWatchedComp{ nullptr }
            , _pTryParentOnTick{ nullptr }
            , _probeTag{}
            , _watchedLocalPos{}
            , _watchedWorldPos{}
            , _tickLocalPos{}
            , _tickLocalScale{ 1.0f, 1.0f, 1.0f }
            , _bWriteLocalOnTick{ SW_FALSE }
            , _bWriteScaleOnTick{ SW_FALSE }
            , _bWriteTwiceOnTick{ SW_FALSE }
            , _bDetachOnTick{ SW_FALSE }
            , _bParentKeptInTick{ SW_FALSE }
            , _bAttachAcceptedInTick{ SW_FALSE }
            , _bClearTagsOnTick{ SW_FALSE }
            , _bProbeTagKeptInTick{ SW_FALSE }
            , _worldUpdateCount{ 0 }
        {
            setCanEverTick( true );
        }

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onWorldTransformUpdated() override
        {
            SceneComponent::onWorldTransformUpdated();
            ++_worldUpdateCount;
        }

        void onTick( float32 deltaTime ) override
        {
            SceneComponent::onTick( deltaTime );
            if ( _pObservedWorld != nullptr )
                *_pObservedWorld = getWorldPosition();
            if ( _pWatchedComp != nullptr )
            {
                _watchedLocalPos = _pWatchedComp->getLocalPosition();
                _watchedWorldPos = _pWatchedComp->getWorldPosition();
            }
            if ( _bWriteTwiceOnTick == SW_TRUE )
                setLocalPosition( float3( _tickLocalPos._x + 1000.0f, _tickLocalPos._y, _tickLocalPos._z ) );
            if ( _bWriteLocalOnTick == SW_TRUE )
                setLocalPosition( _tickLocalPos );
            if ( _bWriteScaleOnTick == SW_TRUE )
                setLocalScale( _tickLocalScale );
            if ( _bDetachOnTick == SW_TRUE )
            {
                detachFromComponent();
                _bParentKeptInTick = ( getParent() != nullptr ) ? SW_TRUE : SW_FALSE;
            }
            if ( _bClearTagsOnTick == SW_TRUE && getOwner() != nullptr )
            {
                getOwner()->clearTags();
                _bProbeTagKeptInTick = getOwner()->hasTag( _probeTag ) ? SW_TRUE : SW_FALSE;
            }
            if ( _pTryParentOnTick != nullptr && getOwner() != nullptr )
                _bAttachAcceptedInTick = getOwner()->attachToParent( _pTryParentOnTick ) ? SW_TRUE : SW_FALSE;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief MockTickSceneComponent 의 정적 TypeInfo 를 반환합니다. */
    inline const TypeInfo* MockTickSceneComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockTickSceneComponent>, hashed_string( "MockTickSceneComponent" ),
                                          hashed_string( "sw::MockTickSceneComponent" ),
                                          sizeof( MockTickSceneComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    // ------------------------------------------------------------------------------
    // 다단계 컴포넌트 상속 계층 정의:
    // Component -> SceneComponent -> MockRootComponent -> MockBasePawnComponent -> MockVehicleComponent -> MockFlyingVehicleComponent
    // ------------------------------------------------------------------------------
    class MockRootComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        vector<string>* _pTickOrderLog{ nullptr };
        string          _componentTag{ "Root" };

        MockRootComponent()
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            SceneComponent::onTick( deltaTime );
            if ( _pTickOrderLog != nullptr )
                _pTickOrderLog->push_back( _componentTag );
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            SceneComponent::onSubTick( subTickId, deltaTime );
            if ( _pTickOrderLog != nullptr )
            {
                string entry = _componentTag;
                entry += "_SubTick_";
                entry += std::to_string( subTickId ).c_str();
                _pTickOrderLog->push_back( entry );
            }
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockRootComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockRootComponent>, hashed_string( "MockRootComponent" ),
                                          hashed_string( "sw::MockRootComponent" ),
                                          sizeof( MockRootComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    class MockBasePawnComponent : public MockRootComponent
    {
    public:
        REFLECT_BODY();

        int32 _pawnHealth{ 100 };
        int32 _pawnTickCount{ 0 };

        MockBasePawnComponent()
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockRootComponent::onTick( deltaTime );
            ++_pawnTickCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockBasePawnComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockBasePawnComponent>, hashed_string( "MockBasePawnComponent" ),
                                          hashed_string( "sw::MockBasePawnComponent" ),
                                          sizeof( MockBasePawnComponent ),
                                          hashed_string( "sw::MockRootComponent" ) );
    }

    class MockVehicleComponent : public MockBasePawnComponent
    {
    public:
        REFLECT_BODY();

        float32 _maxSpeed{ 120.0f };
        int32   _vehicleTickCount{ 0 };

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockBasePawnComponent::onTick( deltaTime );
            ++_vehicleTickCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockVehicleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockVehicleComponent>, hashed_string( "MockVehicleComponent" ),
                                          hashed_string( "sw::MockVehicleComponent" ),
                                          sizeof( MockVehicleComponent ),
                                          hashed_string( "sw::MockBasePawnComponent" ) );
    }

    class MockFlyingVehicleComponent : public MockVehicleComponent
    {
    public:
        REFLECT_BODY();

        float32 _maxAltitude{ 5000.0f };
        int32   _flyingTickCount{ 0 };

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockVehicleComponent::onTick( deltaTime );
            ++_flyingTickCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockFlyingVehicleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockFlyingVehicleComponent>, hashed_string( "MockFlyingVehicleComponent" ),
                                          hashed_string( "sw::MockFlyingVehicleComponent" ),
                                          sizeof( MockFlyingVehicleComponent ),
                                          hashed_string( "sw::MockVehicleComponent" ) );
    }

    class MockMidTickDeactivatorComponent : public Component
    {
    public:
        REFLECT_BODY();

        Component*      _pTargetComp{ nullptr };
        vector<string>* _pTickOrderLog{ nullptr };
        string          _componentTag{ "Deactivator" };
        uint32          _targetSubTickId{ 0 };
        uint32          _selfSubTickToUnregister{ 0 };
        int32           _subTickCount{ 0 };

        MockMidTickDeactivatorComponent()
            : _pTargetComp{ nullptr }
            , _pTickOrderLog{ nullptr }
            , _componentTag{ "Deactivator" }
            , _targetSubTickId{ 0 }
            , _selfSubTickToUnregister{ 0 }
            , _subTickCount{ 0 }
        {
            setCanEverTick( false );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            Component::onSubTick( subTickId, deltaTime );
            ++_subTickCount;
            if ( _pTickOrderLog != nullptr )
            {
                string entry = _componentTag;
                entry += "_SubTick_";
                entry += std::to_string( subTickId ).c_str();
                _pTickOrderLog->push_back( entry );
            }

            if ( _pTargetComp != nullptr && _targetSubTickId != 0 )
                _pTargetComp->setSubTickActive( _targetSubTickId, false );

            if ( _selfSubTickToUnregister != 0 )
                unregisterSubTick( _selfSubTickToUnregister );
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockMidTickDeactivatorComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockMidTickDeactivatorComponent>, hashed_string( "MockMidTickDeactivatorComponent" ),
                                          hashed_string( "sw::MockMidTickDeactivatorComponent" ),
                                          sizeof( MockMidTickDeactivatorComponent ),
                                          hashed_string{} );
    }

    class MockSubTickStressComponent : public Component
    {
    public:
        REFLECT_BODY();

        std::atomic<uint32>* _pGlobalTickSequence{ nullptr };
        std::atomic<uint32>* _pExecutionOrderArray{ nullptr };
        std::atomic<uint32>  _tickCount{ 0 };
        std::atomic<uint32>  _subTickCount{ 0 };
        uint32               _subTickGlobalIdOffset{ 0 };

        MockSubTickStressComponent()
            : _pGlobalTickSequence{ nullptr }
            , _pExecutionOrderArray{ nullptr }
            , _tickCount{ 0 }
            , _subTickCount{ 0 }
            , _subTickGlobalIdOffset{ 0 }
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _tickCount.fetch_add( 1, std::memory_order_relaxed );
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            Component::onSubTick( subTickId, deltaTime );
            _subTickCount.fetch_add( 1, std::memory_order_relaxed );
            if ( _pGlobalTickSequence != nullptr && _pExecutionOrderArray != nullptr )
            {
                const uint32 order    = _pGlobalTickSequence->fetch_add( 1, std::memory_order_relaxed );
                const uint32 globalId = _subTickGlobalIdOffset + subTickId;
                _pExecutionOrderArray[globalId].store( order, std::memory_order_release );
            }
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockSubTickStressComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockSubTickStressComponent>, hashed_string( "MockSubTickStressComponent" ),
                                          hashed_string( "sw::MockSubTickStressComponent" ),
                                          sizeof( MockSubTickStressComponent ),
                                          hashed_string{} );
    }

    /**
     * @class MockSubTickMoverComponent
     * @brief 서브틱 하나(`kMoveSubTick`)에서 다른 오브젝트의 위치를 읽고 자기 위치를 쓰는 씬 컴포넌트입니다 — 선행 조건 사슬의 트랜스폼 가시성을 봅니다.
     * @details 쓰는 값은 (`_pWatched` 의 로컬 위치, 없으면 원점) + `_offset` 입니다. 사슬 A→B→C 에서 C 가 쓰는 값은 같은 프레임의 A · B 결과에 기댑니다.
     */
    class MockSubTickMoverComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kMoveSubTick = 1;

        SceneComponent*   _pWatched{ nullptr };           ///< 서브틱 안에서 읽을 다른 오브젝트의 씬 컴포넌트
        float3            _offset{};                      ///< 읽은 위치에 더해 쓸 값
        float3            _observedLocal{};               ///< 서브틱 안에서 읽은 `_pWatched` 의 로컬 위치
        float3            _observedWorld{};               ///< 서브틱 안에서 읽은 `_pWatched` 의 월드 위치
        const GameObject* _pTickingObjectSeen{ nullptr }; ///< 서브틱 안에서 본 `GameObjectManager::getTickingObject()`
        SceneComponent*   _pAttachChild{ nullptr };       ///< 서브틱에서 이 컴포넌트를 `_pAttachParent` 에 `KeepWorld` 로 붙인다(틱 중이라 미뤄진다)
        SceneComponent*   _pAttachParent{ nullptr };
        GameObject*       _pDestroyObject{ nullptr }; ///< 서브틱에서 이 오브젝트를 파괴한다(틱 중이라 삭제 표시만 선다)
        uint32            _subTickCount{ 0 };
        uint8             _bWriteOnSubTick{ SW_TRUE };

        MockSubTickMoverComponent()
        {
            setCanEverTick( false );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            SceneComponent::onSubTick( subTickId, deltaTime );
            ++_subTickCount;
            _pTickingObjectSeen = GameObjectManager::getTickingObject();
            float3 base{};
            if ( _pWatched != nullptr )
            {
                _observedLocal = _pWatched->getLocalPosition();
                _observedWorld = _pWatched->getWorldPosition();
                base           = _observedLocal;
            }
            if ( _bWriteOnSubTick == SW_TRUE )
                setLocalPosition( base + _offset );
            if ( _pAttachChild != nullptr && _pAttachParent != nullptr )
                (void)_pAttachChild->attachToComponent( _pAttachParent, AttachRule::KeepWorld );
            if ( _pDestroyObject != nullptr && getOwner() != nullptr && getOwner()->getManager() != nullptr )
            {
                getOwner()->getManager()->destroyObject( _pDestroyObject );
                _pDestroyObject = nullptr;
            }
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockSubTickMoverComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockSubTickMoverComponent>, hashed_string( "MockSubTickMoverComponent" ),
                                          hashed_string( "sw::MockSubTickMoverComponent" ),
                                          sizeof( MockSubTickMoverComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    class MockPoolLifecycleComponent : public Component
    {
    public:
        REFLECT_BODY();

        static atomic<int32> s_ctorCount;
        static atomic<int32> s_dtorCount;

        int32 _customData{ 42 };

        MockPoolLifecycleComponent()
        {
            s_ctorCount.fetch_add( 1, std::memory_order_relaxed );
        }

        ~MockPoolLifecycleComponent() override
        {
            s_dtorCount.fetch_add( 1, std::memory_order_relaxed );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }
    };
} // namespace sw

namespace sw
{
    // 정의는 TestGameObjectMocks.cpp 에 **한 번만** 둔다. 헤더에서 `inline` 으로 정의하면 이 헤더를
    // 포함하는 TU 마다 사본이 생길 수 있고(EngineTest 는 Engine.dll 과 링크한다), 그러면 생성자가
    // 올린 수를 소멸자가 다른 사본에서 내리게 된다 — clang 이 `-Wunique-object-duplication` 으로 짚는다.

    inline const TypeInfo* MockPoolLifecycleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockPoolLifecycleComponent>, hashed_string( "MockPoolLifecycleComponent" ),
                                          hashed_string( "sw::MockPoolLifecycleComponent" ),
                                          sizeof( MockPoolLifecycleComponent ) );
    }

    /**
     * @brief `onTick` 을 오버라이드했지만 틱을 선언하지 않는 씬 컴포넌트입니다. 생성자 인자로 끄면(`setCanEverTick( false )`) 끈 것이 이깁니다.
     */
    class MockSelfTickSceneComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        int32 _tickCount{ 0 };

        explicit MockSelfTickSceneComponent( bool bOptOut = false )
            : _tickCount{ 0 }
        {
            if ( bOptOut )
                setCanEverTick( false );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            SceneComponent::onTick( deltaTime );
            ++_tickCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockSelfTickSceneComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockSelfTickSceneComponent>, hashed_string( "MockSelfTickSceneComponent" ),
                                          hashed_string( "sw::MockSelfTickSceneComponent" ),
                                          sizeof( MockSelfTickSceneComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    /** @brief `onTick` 이 없는 데이터 컴포넌트입니다(유니티의 Update 없는 스크립트). */
    class MockNoTickComponent : public Component
    {
    public:
        REFLECT_BODY();

        int32 _value{ 0 };

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockNoTickComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockNoTickComponent>, hashed_string( "MockNoTickComponent" ),
                                          hashed_string( "sw::MockNoTickComponent" ),
                                          sizeof( MockNoTickComponent ) );
    }

    /**
     * @brief 상태를 다 읽으면(`onPostLoad`) 정해 둔 프리팹을 스폰하는 컴포넌트입니다 — 프리팹 안에 두면 "스폰이 스폰을 부르는" 순환이 된다.
     * @details 스폰의 상태 읽기가 만드는 컴포넌트라 설정은 정적 칸으로 받습니다(정의는 TestGameObjectMocks.cpp 에 한 번). 순환 방어가 없을 때
     *          시험이 스택을 넘기지 않고 지도록 `kMaxNestedSpawn` 번에서 멈춥니다.
     */
    class MockPostLoadSpawnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr int32 kMaxNestedSpawn = 4;
        static PrefabCache*    s_pPrefabs; ///< 설정되면 `onPostLoad` 가 `s_spawnPath` 를 같은 매니저에 스폰한다
        static string          s_spawnPath;
        static int32           s_spawnAttemptCount; ///< `onPostLoad` 가 스폰을 시도한 횟수
        static int32           s_spawnedCount;      ///< 그 스폰이 오브젝트를 돌려준 횟수

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onPostLoad() override
        {
            Component::onPostLoad();
            if ( s_pPrefabs == nullptr || getOwner() == nullptr || getOwner()->getManager() == nullptr || s_spawnAttemptCount >= kMaxNestedSpawn )
                return;
            ++s_spawnAttemptCount;
            if ( s_pPrefabs->spawn( getOwner()->getManager(), s_spawnPath, "Nested" ) != nullptr )
                ++s_spawnedCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockPostLoadSpawnerComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockPostLoadSpawnerComponent>, hashed_string( "MockPostLoadSpawnerComponent" ),
                                          hashed_string( "sw::MockPostLoadSpawnerComponent" ),
                                          sizeof( MockPostLoadSpawnerComponent ) );
    }

    /**
     * @brief 상태를 다 읽었을 때(`onPostLoad`) 소유 오브젝트가 이미 부모에 붙어 있었는지 적는 컴포넌트입니다.
     * @details 오브젝트 사이의 부착 · 핸들 PROPERTY 는 묶음이 모두 읽은 뒤(`ObjectStateBatch::finish`) 풉니다. `onPostLoad` 가 그보다 먼저 오면
     *          다른 오브젝트를 가리키는 값이 아직 저장된 id 입니다 — 이 목이 그 순서를 봅니다. 설정 · 결과는 정적 칸입니다(정의는 TestGameObjectMocks.cpp).
     */
    class MockPostLoadProbeComponent : public Component
    {
    public:
        REFLECT_BODY();

        static int32 s_postLoadCount;           ///< `onPostLoad` 가 불린 횟수
        static int32 s_postLoadWithParentCount; ///< 그때 소유 오브젝트가 부모에 붙어 있던 횟수

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onPostLoad() override
        {
            Component::onPostLoad();
            ++s_postLoadCount;
            if ( getOwner() != nullptr && getOwner()->getParent() != nullptr )
                ++s_postLoadWithParentCount;
        }
    };
} // namespace sw

namespace sw
{
    inline const TypeInfo* MockPostLoadProbeComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockPostLoadProbeComponent>, hashed_string( "MockPostLoadProbeComponent" ),
                                          hashed_string( "sw::MockPostLoadProbeComponent" ),
                                          sizeof( MockPostLoadProbeComponent ) );
    }

    /**
     * @brief PROPERTY 가 아닌 런타임 상태 하나(`_counter`)를 `writeState` · `restoreState` 로 내는 컴포넌트입니다(`ComponentStateStore` 의 약속).
     * @details 디렉터 컴포넌트의 모양을 줄인 것입니다 — 상태는 스냅샷의 씬 섹션이 아니라 컴포넌트 섹션으로만 옮겨집니다.
     */
    class MockRuntimeStateComponent : public Component
    {
    public:
        REFLECT_BODY();

        int32 _counter{ 0 };      ///< PROPERTY 가 아닌 상태
        int32 _restoreCount{ 0 }; ///< `restoreState` 가 받아 적용한 횟수

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void writeState( Archive& outArchive ) const { outArchive << _counter; }

        void restoreState( vector<uint8>&& bytes )
        {
            Archive archive( bytes.data(), bytes.size() );
            int32   counter = 0;
            archive >> counter;
            if ( archive.isError() )
                return;
            _counter = counter;
            ++_restoreCount;
        }
    };

    inline const TypeInfo* MockRuntimeStateComponent::StaticType()
    {
        return makeMockComponentTypeInfo( &GameObject::addComponentTo<MockRuntimeStateComponent>, hashed_string( "MockRuntimeStateComponent" ),
                                          hashed_string( "sw::MockRuntimeStateComponent" ), sizeof( MockRuntimeStateComponent ) );
    }

} // namespace sw
