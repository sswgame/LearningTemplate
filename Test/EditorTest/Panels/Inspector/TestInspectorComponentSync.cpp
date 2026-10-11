#include "pch.h"

#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorComponentManager.h"

#include "Engine/Object/Component/CameraComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 시험 전용 컴포넌트 인스펙터 — 그리는 것이 없다. */
    class FakeInspector final : public IInspectorComponent
    {
    };

    unique_ptr<IInspectorComponent> createFakeInspector()
    {
        return make_unique<FakeInspector>();
    }

    const TypeInfo* getCameraType()
    {
        return CameraComponent::StaticType();
    }
} // namespace

/**
 * @brief [InspectorComponentSyncTest] 매니저는 등록 세대를 따라간다 — 새 줄은 만들고, 사라진 줄은 지우고, 이미지 범위의 줄은 언로드 전에 뗀다
 * @details 확장 모듈은 에디터가 뜬 뒤에 로드되고 언로드된다. 언로드되는 이미지의 등록 줄로 만든 확장(vtable 이 그 이미지에 있다)이 남으면 다음 그리기가 내려간 코드로 뛴다.
 */
SW_TEST_CASE( InspectorComponentSyncTest, FollowsRegistrationsAndReleasesByImageRange )
{
    InspectorComponentManager manager;
    manager.registerDefaults();
    SW_EXPECT_TRUE( manager.find( "CameraComponent" ) == nullptr );
    {
        const EditorRegistrar<EditorInspectorRegistration> registrar{
            EditorInspectorRegistration{ { "CameraComponent", 0 }, &getCameraType, &createFakeInspector }
        };
        manager.syncWithRegistry();
        SW_EXPECT_TRUE( manager.find( "CameraComponent" ) != nullptr );

        const EditorInspectorRegistration* pRegistration = &registrar.getRegistration();
        SW_EXPECT_EQUAL( 0u, manager.releaseInspectorsWithin( pRegistration + 1, pRegistration + 2 ) ); // 다른 이미지 범위
        SW_EXPECT_EQUAL( 1u, manager.releaseInspectorsWithin( pRegistration, pRegistration + 1 ) );
        SW_EXPECT_TRUE( manager.find( "CameraComponent" ) == nullptr );
    }
    manager.syncWithRegistry(); // 등록자가 사라졌다(세대가 올랐다) — 다시 만들지 않는다
    SW_EXPECT_TRUE( manager.find( "CameraComponent" ) == nullptr );
}
