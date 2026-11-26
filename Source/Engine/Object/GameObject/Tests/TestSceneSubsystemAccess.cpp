// SceneSubsystemAccessTest — 씬 서브시스템 등록 줄의 갱신 접근 선언을 매니저가 다른 서비스의 넘긴 일(창)과 견주는지 본다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"

using namespace sw;

namespace
{
    /** @brief 아무것도 하지 않는 서비스입니다. 등록 줄만 시험한다. */
    class AccessProbeService final : public ISceneSubsystem
    {
    public:
        explicit AccessProbeService( GameObjectManager& manager ) { (void)manager; }
        const utf8* getSubsystemName() const override { return "AccessProbeService"; }
        void        update( float32 deltaTime ) override { (void)deltaTime; }
        void        onSceneCleared() override {}
    };

    /** @brief 같은 모양의 둘째 서비스입니다(씬 범위는 타입으로 찾으므로 타입이 달라야 한다). */
    class AccessProbeJobService final : public ISceneSubsystem
    {
    public:
        explicit AccessProbeJobService( GameObjectManager& manager ) { (void)manager; }
        const utf8* getSubsystemName() const override { return "AccessProbeJobService"; }
        void        update( float32 deltaTime ) override { (void)deltaTime; }
        void        onSceneCleared() override {}
    };

    /** @brief 테스트 동안만 등록 줄을 올립니다. */
    struct ScopedSubsystemRegistration
    {
        const SceneSubsystemRegistration* _pRegistration;

        explicit ScopedSubsystemRegistration( const SceneSubsystemRegistration& registration )
            : _pRegistration{ &registration }
        {
            const bool bAdded = SceneSubsystemRegistry::add( _pRegistration );
            SW_ASSERT( bAdded );
            (void)bAdded;
        }
        ~ScopedSubsystemRegistration() { SceneSubsystemRegistry::remove( _pRegistration ); }
    };
} // namespace

/** @brief [SceneSubsystemAccessTest] 넘긴 일이 쓰는 데이터를 그 창 안의 단계에서 고치는 갱신은 매니저를 만들 때 오류다 — 겹치지 않는 선언은 조용하다 */
SW_TEST_CASE( SceneSubsystemAccessTest, UpdateInsideFrameJobWindowIsChecked )
{
    // 일: Navigation 단계 끝에 넘겨 FlushPostAnimation 앞에서 마친다(그 사이 Animation 단계가 돈다). 일은 HitRecord 를 쓴다.
    const SceneSubsystemRegistration  jobLine{ &createSceneSubsystem<AccessProbeJobService>, SceneFrameStep::Navigation, SceneFrameAccess{}, SceneFrameStep::FlushPostAnimation,
                                              makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::HitRecord }, SceneDataKindSet{}, SceneDataKindSet{} ) };
    const ScopedSubsystemRegistration jobScope{ jobLine };
    {
        const SceneSubsystemRegistration  quietLine{ &createSceneSubsystem<AccessProbeService>, SceneFrameStep::Animation,
                                                    makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::AnimatorState }, SceneDataKindSet{}, SceneDataKindSet{} ), SceneFrameStep::Count, SceneFrameAccess{} };
        const ScopedSubsystemRegistration quietScope{ quietLine };
        GameObjectManager                 manager; // 겹치지 않는다 — 로그가 없다
    }
    {
        const SceneSubsystemRegistration  racingLine{ &createSceneSubsystem<AccessProbeService>, SceneFrameStep::Animation,
                                                     makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::HitRecord } ), SceneFrameStep::Count, SceneFrameAccess{} };
        const ScopedSubsystemRegistration racingScope{ racingLine };
        SW_EXPECT_LOG( "GameObjectManager.ServiceUpdateDuringFrameJob" );
        GameObjectManager manager; // 일이 쓰는 것을 창 안에서 읽는다
    }
}
