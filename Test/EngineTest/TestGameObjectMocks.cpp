#include "pch.h"

#include "EngineTest/TestGameObjectMocks.h"

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
        static mutex                                  s_mutex;
        std::lock_guard<mutex>                        lock( s_mutex );
        static unordered_map<hashed_string, TypeInfo> s_types;
        auto                                          it = s_types.find( shortName );
        if ( it != s_types.end() )
            return &it->second;

        TypeInfo info{};
        info._name               = shortName;
        info._typeId             = static_cast<uint32>( shortName.getHash() );
        info._fullyQualifiedName = fqn;
        info._parentFQN          = parentFqn;
        info._size               = size;
        info._addComponent       = addComponent;
        it                       = s_types.emplace( shortName, std::move( info ) ).first;
        if ( engine::areEngineServicesBound() )
            engine::getTypeRegistry().registerClass( it->second );
        return &it->second;
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
        MockPoolLifecycleComponent::StaticType();
        MockPostLoadSpawnerComponent::StaticType();
        MockPostLoadProbeComponent::StaticType();
        MockRuntimeStateComponent::StaticType();
    }
} // namespace sw
