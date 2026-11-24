/**
 * @file DevTools/Client/Render/DeviceLostPolicySwitch.cpp
 * @brief `-gv_deviceLostPolicy=<정책>` — 시나리오가 장치 분실 정책을 설정 파일 대신 고르는 개발 스위치입니다. 렌더 드라이버의 개발 입구(`IEngineRenderDriver::overrideDeviceLostPolicy`)에 덮는다.
 * @details 개발 도구 소스다(배포 구성은 짓지 않는다). 설정 파일을 고치지 않고 Recreate 길을 끝까지 본다(`devicerecreate.scenario.xml` 의 `<Launch variable="gv_deviceLostPolicy" value="Recreate"/>`).
 *          예전에는 백엔드 교체기가 기동 때 덮었다 — 교체와 상관없는 렌더 드라이버의 스위치라 따로 둔다. 변수는 리플렉션 enum 이라 정책이 아닌 값은 명령줄에서 거절된다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Presentation/IEngineRenderDriver.h"
#include "Engine/Renderer/EngineRenderDriver.h"

namespace sw
{
    /** @brief 장치 분실 정책 덮기입니다. `None` 이면 `EngineConfig::_deviceLostPolicy` 를 쓴다. */
    SW_TEST_GLOBAL_VARIABLE( RHIDeviceLostPolicy, gv_deviceLostPolicy, RHIDeviceLostPolicy::None, "장치 분실 정책 덮기 (None=EngineConfig 값, Quit, Recreate — 시나리오가 Recreate 길을 끝까지 볼 때)" );

    /**
     * @class DeviceLostPolicySwitch
     * @brief 기동 때 `gv_deviceLostPolicy` 가 정책이면 렌더 드라이버에 덮습니다. 덮은 값은 설정 읽기(`EngineRenderDriver::onStartupFinished`)보다 이긴다.
     */
    class DeviceLostPolicySwitch final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "DeviceLostPolicySwitch";

        explicit DeviceLostPolicySwitch( IEngineRenderDriver& renderDriver )
            : _renderDriver{ renderDriver }
        {
        }

        SubsystemInitResult initializeSubsystem() override
        {
            if ( gv_deviceLostPolicy != RHIDeviceLostPolicy::None )
                _renderDriver.overrideDeviceLostPolicy( gv_deviceLostPolicy );
            return SubsystemInitResult::Succeeded;
        }
        void        shutdownSubsystem() override {}
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        // 렌더 드라이버를 참조로 든다 — 먼저 서고 나중에 진다.
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { EngineRenderDriverSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

    private:
        IEngineRenderDriver& _renderDriver; // 소유하지 않는다 — 같은 기동 범위의 렌더 드라이버 단계가 든다
    };

    // 렌더 드라이버가 선 기동만(렌더러를 끈 기동은 드라이버가 없다).
    SW_ENGINE_SUBSYSTEM( DeviceLostPolicySwitch, Process, Startup, ( EngineRenderDriverSubsystem ), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        IEngineRenderDriver* const pRenderDriver = build._pStartupParts->_pRenderDriver;
        if ( pRenderDriver == nullptr )
            return;
        (void)scope.emplaceSubsystem<DeviceLostPolicySwitch>( *pRenderDriver );
    } );
} // namespace sw
