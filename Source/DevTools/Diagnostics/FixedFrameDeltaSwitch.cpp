/**
 * @file DevTools/Diagnostics/FixedFrameDeltaSwitch.cpp
 * @brief `-gv_fixedFrameDelta=<초>` — 사람이 실행 인자 · 콘솔로 주는 프레임 고정 시간을 결정적 실행 정책(`DeterministicRunPolicy::_fixedFrameDelta`)에 옮기는 개발 스위치입니다.
 * @details 개발 도구 소스다(DevTools — 배포 구성이 짓지 않는다). App 은 이 파일을 모르고 정책만 읽는다. 자동화 실행기 · 시나리오 재생기는 이 변수를 거치지 않고 정책에 직접 넣는다.
 *          모양은 `DevTools/Diagnostics/EnvironmentFreezeSwitch.cpp` 와 같다(기동 때 지금 값을 옮기고, 변경 콜백으로 뒤의 변경을 받는다).
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    /**
     * @brief `-gv_fixedFrameDelta=<초>`: 0 보다 크면 프레임마다 실제 시계 대신 그 시간을 흘립니다(재현 · 픽셀 비교). 0 이면 실시간입니다.
     * @details 고정 스텝(물리, fixedUpdate)도 이 시간으로 나뉩니다. 1/60 이면 프레임마다 정확히 한 스텝입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( float32, gv_fixedFrameDelta, 0.0f, "프레임마다 흘릴 고정 시간(초, 0=실시간) — 결정적 실행" );

    namespace
    {
        struct FixedFrameDeltaSwitchInternal
        {
            static constexpr const utf8* kVariableName = "gv_fixedFrameDelta";

            /** @brief 지금 값을 정책에 옮깁니다. */
            static void applyToPolicy()
            {
                DeterministicRun&      run    = engine::getSubsystem<DeterministicRun>();
                DeterministicRunPolicy policy = run.getPolicy();
                policy._fixedFrameDelta       = gv_fixedFrameDelta > 0.0f ? static_cast<float32>( gv_fixedFrameDelta ) : 0.0f;
                run.setPolicy( policy );
            }

            static void onVariableChanged( GlobalVariableInfo* pVariable )
            {
                (void)pVariable;
                applyToPolicy();
            }
        };
    } // namespace

    /**
     * @class FixedFrameDeltaSwitch
     * @brief 기동 때 지금 값(실행 인자)을 정책에 옮기고, 그 뒤의 변경(콘솔 · 에디터 목록)을 변경 콜백으로 받습니다.
     */
    class FixedFrameDeltaSwitch final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "FixedFrameDeltaSwitch";

        SubsystemInitResult initializeSubsystem() override
        {
            GlobalVariableInfo* const pVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( FixedFrameDeltaSwitchInternal::kVariableName );
            if ( pVariable == nullptr )
            {
                SW_LOG_ERROR_ID( "FixedFrameDeltaSwitch.VariableMissing", "The fixed frame delta switch found no '%#' registration", FixedFrameDeltaSwitchInternal::kVariableName );
                return SubsystemInitResult::Failed;
            }
            pVariable->_onValueChanged = GlobalVariableChangedDelegate::create<&FixedFrameDeltaSwitchInternal::onVariableChanged>();
            if ( gv_fixedFrameDelta > 0.0f )
                FixedFrameDeltaSwitchInternal::applyToPolicy(); // 사람이 준 값만 옮긴다 — 0 이면 정책(자동화가 넣었을 수 있다)을 덮지 않는다
            return SubsystemInitResult::Succeeded;
        }

        void shutdownSubsystem() override
        {
            GlobalVariableManager* const pManager  = engine::findSubsystem<GlobalVariableManager>();
            GlobalVariableInfo* const    pVariable = pManager != nullptr ? pManager->findVariable( FixedFrameDeltaSwitchInternal::kVariableName ) : nullptr;
            if ( pVariable != nullptr )
                pVariable->_onValueChanged = {};
        }

        const utf8* getSubsystemName() const override { return kSubsystemName; }
    };

    // 기동 단계 — 서비스 단계 초기화 중에는 엔진 서비스(GlobalVariableManager 조회)가 아직 묶이지 않았다(`EnvironmentFreezeSwitch` 와 같다).
    SW_ENGINE_SUBSYSTEM( FixedFrameDeltaSwitch, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<FixedFrameDeltaSwitch>();
    } );
} // namespace sw
