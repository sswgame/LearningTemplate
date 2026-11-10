#include "pch.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Profiling/FrameProfiler.h"

using namespace sw;

namespace
{
    /** @brief 아무것도 하지 않는 프레임 시스템입니다 — 붙인 이름으로 구간이 열리는지만 본다. */
    class ProfileProbeSystem final : public ISceneFrameSystem
    {
    public:
        uint32 _runCount{ 0 };

        void             runBeforeTick( GameObjectManager& /*manager*/, float32 /*deltaTime*/ ) override { ++_runCount; }
        SceneFrameAccess getFrameAccess() const override { return SceneFrameAccess{}; }
    };

    /** @brief 엔진 프로파일러에서 이름이 @p pName 인 슬롯을 찾습니다. 없으면 `kInvalidSlot` 입니다. */
    uint32 findScopeSlot( const FrameProfiler& profiler, const utf8* pName )
    {
        for ( uint32 slot = 0; slot < profiler.getScopeCount(); ++slot )
        {
            const utf8* pScopeName = profiler.findScopeName( slot );
            if ( pScopeName != nullptr && std::strcmp( pScopeName, pName ) == 0 )
                return slot;
        }
        return FrameProfiler::kInvalidSlot;
    }

    /** @brief 엔진 프로파일러에서 이름이 @p pPrefix 로 시작하고 마지막 프레임에 불린 구간 수를 셉니다. */
    uint32 countCalledScopesWithPrefix( const FrameProfiler& profiler, const utf8* pPrefix )
    {
        const size_t prefixLength = std::strlen( pPrefix );
        uint32       count        = 0;
        for ( uint32 slot = 0; slot < profiler.getScopeCount(); ++slot )
        {
            const utf8* pScopeName = profiler.findScopeName( slot );
            if ( pScopeName != nullptr && std::strncmp( pScopeName, pPrefix, prefixLength ) == 0 && profiler.getLastFrameCount( slot ) > 0 )
                ++count;
        }
        return count;
    }
} // namespace

// 매니저가 붙인 프레임 시스템 · 씬 서비스를 부르는 자리마다 등록 이름으로 구간을 연다 — 시스템이 손으로 구간을 걸지 않아도 `GT.Scene.tick` 이 나뉘어 보인다.
SW_TEST_CASE( DispatchProfileTest, FrameSystemAndServicesOpenScopesByRegisteredName )
{
    FrameProfiler& profiler    = engine::getSubsystem<FrameProfiler>();
    const bool     bWasEnabled = profiler.isEnabled();
    profiler.setEnabled( true );

    GameObjectManager   manager;
    auto                pProbe    = make_unique<ProfileProbeSystem>();
    ProfileProbeSystem* pProbeRaw = pProbe.get();
    SW_REQUIRE_TRUE( manager.addFrameSystem( hashed_string( "DispatchProfileProbe" ), std::move( pProbe ) ) );

    profiler.beginFrame();
    manager.update( 1.0f / 60.0f );
    profiler.endFrame();
    profiler.setEnabled( bWasEnabled );

    SW_EXPECT_EQUAL( 1u, pProbeRaw->_runCount );
    const uint32 slot = findScopeSlot( profiler, "GT.Scene.frameSystem.DispatchProfileProbe" );
    SW_REQUIRE_TRUE( slot != FrameProfiler::kInvalidSlot );
    SW_EXPECT_EQUAL( uint64{ 1 }, profiler.getLastFrameCount( slot ) );
    // 씬 서비스는 기능 폴더가 정적 초기화에서 올린다 — 어느 것이 있는지는 링크한 모듈에 달려, 적어도 하나가 자기 이름으로 불렸는지만 본다.
    SW_EXPECT_TRUE( countCalledScopesWithPrefix( profiler, "GT.Scene.service." ) > 0 );
}
