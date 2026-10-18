#include "pch.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    /** @brief 풀 재사용 검증용 생성·소멸 카운터. 헤더가 아니라 여기 한 번만 정의한다(헤더 주석 참고). */
    atomic<int32> MockPoolLifecycleComponent::s_ctorCount{ 0 };
    atomic<int32> MockPoolLifecycleComponent::s_dtorCount{ 0 };

    PrefabCache* MockPostLoadSpawnerComponent::s_pPrefabs{ nullptr };
    string       MockPostLoadSpawnerComponent::s_spawnPath{};
    int32        MockPostLoadSpawnerComponent::s_spawnAttemptCount{ 0 };
    int32        MockPostLoadSpawnerComponent::s_spawnedCount{ 0 };

    int32 MockPostLoadProbeComponent::s_postLoadCount{ 0 };
    int32 MockPostLoadProbeComponent::s_postLoadWithParentCount{ 0 };

    /** @brief 테스트 전용 TypeInfo 를 만들거나 캐시에서 반환합니다. */
    const TypeInfo* makeMockComponentTypeInfo( Component* ( *addComponent )(GameObject*), hashed_string shortName, hashed_string fqn, size_t size, hashed_string parentFqn )
    {
        // 테스트 로컬 TypeInfo (RTTI 없음). 생성 함수도 여기 실려 리플렉션 표에 오른다 — 이름으로 만드는 길이 코드젠 타입과 같다.
        // **값은 `unique_ptr` 로 둔다.** `sw::unordered_map` 은 밀집 배열이라 타입이 늘면 값이 옮겨진다. 값을 그대로 두면 앞서 돌려준 포인터
        // (각 모의 타입의 `StaticType()` 가 정적 변수로 쥔다)가 매달린다(타입 500 개 벤치에서 버킷 하나가 사라졌다). 힙 객체의 주소는 옮겨지지 않는다.
        static mutex                                              s_mutex;
        std::lock_guard<mutex>                                    lock( s_mutex );
        static unordered_map<hashed_string, unique_ptr<TypeInfo>> s_mapNameToType;
        auto                                                      it = s_mapNameToType.find( shortName );
        if ( it != s_mapNameToType.end() )
            return it->second.get();

        unique_ptr<TypeInfo> pInfo  = make_unique<TypeInfo>();
        pInfo->_name                = shortName;
        pInfo->_typeID              = static_cast<uint32>( shortName.getHash() );
        pInfo->_fullyQualifiedName  = fqn;
        pInfo->_parentFQN           = parentFqn;
        pInfo->_size                = size;
        pInfo->_addComponent        = addComponent;
        const TypeInfo* pStableInfo = pInfo.get();
        s_mapNameToType.emplace( shortName, std::move( pInfo ) );
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<TypeRegistry>().registerClass( *pStableInfo );
        return pStableInfo;
    }

    /** @brief 모의 컴포넌트 TypeInfo(생성 함수 포함)를 리플렉션 표에 올립니다. */
    void RegisterMockComponents()
    {
        MockMeshComponent::StaticType();
        MockAudioComponent::StaticType();
        MockCallbackComponent::StaticType();
        MockTickSceneComponent::StaticType();
        MockRootComponent::StaticType();
        MockBasePawnComponent::StaticType();
        MockVehicleComponent::StaticType();
        MockFlyingVehicleComponent::StaticType();
        MockMidTickDeactivatorComponent::StaticType();
        MockSubTickStressComponent::StaticType();
        MockSubTickMoverComponent::StaticType();
        MockPoolLifecycleComponent::StaticType();
        MockPostLoadSpawnerComponent::StaticType();
        MockPostLoadProbeComponent::StaticType();
        MockRuntimeStateComponent::StaticType();
    }
} // namespace sw
