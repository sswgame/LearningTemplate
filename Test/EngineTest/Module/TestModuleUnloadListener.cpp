#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

#include "TestFramework/TestFramework.h"

// ModuleUnloadListenerTest — 모듈 이미지를 내리기 전의 정리가 훑는 리스너 목록(`IModuleUnloadListener`).

#if !defined( SW_SHIPPING )
namespace
{
    /** @brief "모듈 이미지" 로 삼는 바이트 범위입니다. 다른 리스너의 코드 주소가 여기 들 일은 없습니다. */
    uint8 s_arrProbeImage[16]{};

    /** @brief 코드 주소 하나를 들고, 범위 안이면 떼는 가짜 리스너입니다. */
    class ProbeUnloadListener final : public sw::IModuleUnloadListener
    {
    public:
        ProbeUnloadListener()
            : _pCode{ nullptr }
            , _bKeepImageMapped{ false }
        {
        }

        const utf8* getModuleUnloadListenerName() const override { return "probe entries"; }

        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
        {
            if ( isAddressWithin( _pCode, pBegin, pEnd ) == false )
                return 0;
            if ( _bKeepImageMapped )
            {
                outKeepImageMapped = true;
                return 0;
            }
            _pCode = nullptr;
            return 1;
        }

        const void* _pCode;
        bool        _bKeepImageMapped;
    };

    /**
     * @brief 훑기 안에서 다른 리스너를 지우고 새로 만드는 리스너입니다 — 에디터가 확장 패널을 지우면 그 패널의 Undo 스택(리스너)도 사라진다.
     */
    class OwningUnloadListener final : public sw::IModuleUnloadListener
    {
    public:
        OwningUnloadListener()
            : _pOwned{ sw::make_unique<ProbeUnloadListener>() }
            , _pCreated{}
        {
        }

        const utf8* getModuleUnloadListenerName() const override { return "owning entries"; }
        bool        isReleaseExpected() const override { return true; }

        uint32 onModuleUnloading( const void* /*pBegin*/, const void* /*pEnd*/, bool& /*outKeepImageMapped*/ ) override
        {
            _pOwned.reset();                                    // 같은 스레드의 훑기 안에서 지운다 — 잠금을 다시 잡지 않는다
            _pCreated = sw::make_unique<ProbeUnloadListener>(); // 만들어도 된다
            return 1;
        }

        sw::unique_ptr<ProbeUnloadListener> _pOwned;
        sw::unique_ptr<ProbeUnloadListener> _pCreated;
    };
} // namespace

/**
 * @brief [ModuleUnloadListenerTest] 정리는 살아 있는 리스너를 모두 훑는다 — 만들어지면 들고, 사라지면 빠진다
 * @details 등록부를 하나 더할 때 고칠 곳은 그 등록부의 상속 한 줄이어야 한다. 여기서는 엔진이 모르는 가짜 리스너를 세워 `releaseModuleCode` 가
 *          그것까지 지나가는지, 범위 밖은 건드리지 않는지, "이미지를 내리지 말라" 는 답을 그대로 올리는지, 사라진 리스너는 목록에서 빠지는지 본다.
 */
SW_TEST_CASE( ModuleUnloadListenerTest, ReleaseModuleCodeSweepsEveryLiveListener )
{
    SW_TEST_DEFENSIVE_SCOPE( "releaseModuleCode warns about what a probe listener kept" );
    const uint32 listenerCountBefore = sw::IModuleUnloadListener::getListenerCount();
    {
        ProbeUnloadListener probe;
        SW_EXPECT_EQUAL( listenerCountBefore + 1, sw::IModuleUnloadListener::getListenerCount() );

        // 범위 안: 뗀다.
        probe._pCode = &s_arrProbeImage[4];
        bool bKeepImageMapped{ true };
        SW_EXPECT_EQUAL( 1u, sw::ModuleImageUtil::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 16, &bKeepImageMapped ) );
        SW_EXPECT_NULL( probe._pCode );
        SW_EXPECT_FALSE( bKeepImageMapped );

        // 범위 밖: 그대로다.
        probe._pCode = &s_arrProbeImage[12];
        SW_EXPECT_EQUAL( 0u, sw::ModuleImageUtil::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 8, &bKeepImageMapped ) );
        SW_EXPECT_TRUE( probe._pCode == &s_arrProbeImage[12] );

        // 떼어 낼 수 없다는 리스너의 답은 부르는 쪽에 그대로 간다.
        probe._bKeepImageMapped = true;
        SW_EXPECT_EQUAL( 0u, sw::ModuleImageUtil::releaseModuleCode( "ProbeModule", s_arrProbeImage, s_arrProbeImage + 16, &bKeepImageMapped ) );
        SW_EXPECT_TRUE( bKeepImageMapped );

        // 사본도 따로 오른다(복사로 생긴 등록부가 훑기에서 빠지지 않게).
        const ProbeUnloadListener copy{ probe };
        SW_EXPECT_EQUAL( listenerCountBefore + 2, sw::IModuleUnloadListener::getListenerCount() );
    }
    SW_EXPECT_EQUAL( listenerCountBefore, sw::IModuleUnloadListener::getListenerCount() );
}

/**
 * @brief [ModuleUnloadListenerTest] 훑기 안에서 리스너를 지우거나 만들어도 멈추지 않고, 지워진 리스너는 건너뛴다
 * @details 목록 잠금은 재진입하지 않는다. 훑는 스레드는 잠금 없이 목록을 고치고, 훑기는 시작할 때의 사본을 돈다.
 */
SW_TEST_CASE( ModuleUnloadListenerTest, ListenerMayDestroyAndCreateListenersDuringTheSweep )
{
    const uint32 listenerCountBefore = sw::IModuleUnloadListener::getListenerCount();
    {
        OwningUnloadListener owner;
        SW_EXPECT_EQUAL( listenerCountBefore + 2, sw::IModuleUnloadListener::getListenerCount() );
        sw::vector<sw::IModuleUnloadListener::ReleaseResult> listResult;
        sw::IModuleUnloadListener::releaseAllWithin( s_arrProbeImage, s_arrProbeImage + 16, listResult );
        SW_EXPECT_TRUE( owner._pOwned == nullptr );
        SW_EXPECT_TRUE( owner._pCreated != nullptr );
        SW_EXPECT_EQUAL( listenerCountBefore + 2, sw::IModuleUnloadListener::getListenerCount() );
        uint32 expectedCount = 0;
        for ( const sw::IModuleUnloadListener::ReleaseResult& result : listResult )
        {
            if ( result._bExpected )
                expectedCount += result._releasedCount;
        }
        SW_EXPECT_EQUAL( 1u, expectedCount );
    }
    SW_EXPECT_EQUAL( listenerCountBefore, sw::IModuleUnloadListener::getListenerCount() );
}

/**
 * @brief [ModuleUnloadListenerTest] RHI 백엔드 모듈을 내릴 때도 그 이미지의 코드를 쥔 등록을 뗀다
 * @details 게임 · 에디터 모듈과 같은 창구(`ModuleImageUtil::unloadModuleImage`)를 지나는지 본다. 실행 파일 옆의 RHI 모듈 하나를
 *          레지스트리로 올리고, 그 모듈의 `createRHIDevice` 주소를 쥔 가짜 리스너를 세운 뒤 `unloadModules` 로 내린다. 이미지를 바로 내리면
 *          리스너는 내려간 코드를 쥔 채 남는다. 시험이 따로 올린 핸들이 이미지를 붙들어 두므로 주소는 끝까지 유효하다.
 */
SW_TEST_CASE( ModuleUnloadListenerTest, RHIModuleUnloadReleasesItsCode )
{
    SW_TEST_DEFENSIVE_SCOPE( "unloadModuleImage warns about what a probe listener kept" );

    struct RHIModule
    {
        sw::RHIBackend _backend;
        const utf8*    _pBaseName;
    };
    const RHIModule kArrRHIModule[] = {
        {sw::RHIBackend::DirectX11,   "RHI_DX11"},
        {sw::RHIBackend::DirectX12,   "RHI_DX12"},
        {   sw::RHIBackend::Vulkan, "RHI_Vulkan"},
        {   sw::RHIBackend::OpenGL,     "RHI_GL"},
    };

    for ( const RHIModule& rhiModule : kArrRHIModule )
    {
        const sw::string path = sw::ModuleImageUtil::findModuleLibraryPath( rhiModule._pBaseName );
        if ( sw::FileUtil::exists( path ) == false )
            continue;
        void* pKeepMapped = sw::ModuleImageUtil::loadDynamicLibrary( path );
        if ( pKeepMapped == nullptr )
            continue; // 드라이버 · 로더가 없는 기계(예: Vulkan 로더 없는 CI)
        const void* pCreateDevice = sw::ModuleImageUtil::getDynamicSymbol( pKeepMapped, "createRHIDevice" );
        SW_ASSERT_TRUE( pCreateDevice != nullptr );

        {
            sw::RHIBackendRegistry registry;
            SW_ASSERT_TRUE( registry.tryLoadModule( rhiModule._backend, path ) );

            ProbeUnloadListener probe;
            probe._pCode = pCreateDevice;
            registry.unloadModules();
            SW_EXPECT_NULL( probe._pCode );
        }

        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( rhiModule._pBaseName, pKeepMapped ) );
        return;
    }
    SW_TEST_SKIP( "no loadable RHI module next to the executable" );
}
#endif
