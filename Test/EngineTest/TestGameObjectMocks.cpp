#include "pch.h"

#include "EngineTest/TestGameObjectMocks.h"

namespace sw
{
    /** @brief 테스트 전용 TypeInfo 를 만들거나 캐시에서 반환합니다. */
    const TypeInfo* makeMockComponentTypeInfo( hashed_string shortName, hashed_string fqn, size_t size, hashed_string parentFqn )
    {
        // 테스트 로컬 TypeInfo (RTTI 없음). 키는 ComponentManager 팩토리 등록과 일치합니다.
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
        it                       = s_types.emplace( shortName, std::move( info ) ).first;
        if ( engine::areEngineServicesBound() )
            engine::getTypeRegistry().registerClass( it->second );
        return &it->second;
    }

    /** @brief 모의 컴포넌트 TypeInfo 와 팩토리를 등록합니다. */
    void RegisterMockComponents( GameObjectManager& manager )
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

        manager.registerComponentType<MockMeshComponent>( hashed_string( "MockMeshComponent" ) );
        manager.registerComponentType<MockAudioComponent>( hashed_string( "MockAudioComponent" ) );
        manager.registerComponentType<MockCallbackComponent>( hashed_string( "MockCallbackComponent" ) );
        manager.registerComponentType<MockTickSceneComponent>( hashed_string( "MockTickSceneComponent" ) );
        manager.registerComponentType<MockRootComponent>( hashed_string( "MockRootComponent" ) );
        manager.registerComponentType<MockBasePawnComponent>( hashed_string( "MockBasePawnComponent" ) );
        manager.registerComponentType<MockVehicleComponent>( hashed_string( "MockVehicleComponent" ) );
        manager.registerComponentType<MockFlyingVehicleComponent>( hashed_string( "MockFlyingVehicleComponent" ) );
        manager.registerComponentType<MockMidTickDeactivatorComponent>( hashed_string( "MockMidTickDeactivatorComponent" ) );
        manager.registerComponentType<MockSubTickStressComponent>( hashed_string( "MockSubTickStressComponent" ) );
        manager.registerComponentType<MockPoolLifecycleComponent>( hashed_string( "MockPoolLifecycleComponent" ) );
    }
} // namespace sw
