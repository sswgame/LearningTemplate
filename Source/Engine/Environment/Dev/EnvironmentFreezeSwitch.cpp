/**
 * @file Engine/Environment/Dev/EnvironmentFreezeSwitch.cpp
 * @brief `-gv_environmentAnimate=0` — 바람 · 파도 시간을 0 에 멈추는 개발 스위치입니다. 값을 결정적 실행 정책(`DeterministicRunPolicy::_bFreezeEnvironmentTime`)에 옮깁니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 물 · 풀 틱은 이 파일을 모르고 정책만 읽는다. 시나리오가 `<Variable>` · `<Launch>` 로 이 이름을 건다
 *          (백엔드끼리 사진을 견주는 시나리오 — 벽시계 시간이 실행마다 달라 같은 프레임도 다른 그림이 된다).
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
    /** @brief `-gv_environmentAnimate=0` 이면 바람과 파도 시간을 0 에서 멈춥니다(백엔드 스크린샷 비교와 결정적 측정에 씁니다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_environmentAnimate, 1, "바람 · 파도 시간이 흐른다 (0=멈춤, 스크린샷 비교용)" );

    namespace
    {
        struct EnvironmentFreezeSwitchInternal
        {
            static constexpr const utf8* kVariableName = "gv_environmentAnimate";

            /** @brief 지금 값을 정책에 옮깁니다. */
            static void applyToPolicy()
            {
                DeterministicRunPolicy policy  = DeterministicRun::getPolicy();
                policy._bFreezeEnvironmentTime = gv_environmentAnimate == 0 ? SW_TRUE : SW_FALSE;
                DeterministicRun::setPolicy( policy );
            }

            static void onVariableChanged( GlobalVariableInfo* pVariable )
            {
                (void)pVariable;
                applyToPolicy();
            }
        };
    } // namespace

    /**
     * @class EnvironmentFreezeSwitch
     * @brief 기동 때 지금 값(실행 인자)을 정책에 옮기고, 그 뒤의 변경(시나리오 `<Variable>` · 콘솔 · 에디터 목록)을 변경 콜백으로 받습니다.
     */
    class EnvironmentFreezeSwitch final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EnvironmentFreezeSwitch";

        SubsystemInitResult initializeSubsystem() override
        {
            GlobalVariableInfo* const pVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( EnvironmentFreezeSwitchInternal::kVariableName );
            if ( pVariable == nullptr )
            {
                SW_LOG_ERROR_ID( "EnvironmentFreezeSwitch.VariableMissing", "The environment freeze switch found no '%#' registration", EnvironmentFreezeSwitchInternal::kVariableName );
                return SubsystemInitResult::Failed;
            }
            pVariable->_onValueChanged = GlobalVariableChangedDelegate::create<&EnvironmentFreezeSwitchInternal::onVariableChanged>();
            EnvironmentFreezeSwitchInternal::applyToPolicy();
            return SubsystemInitResult::Succeeded;
        }

        void shutdownSubsystem() override
        {
            GlobalVariableManager* const pManager  = engine::findSubsystem<GlobalVariableManager>();
            GlobalVariableInfo* const    pVariable = pManager != nullptr ? pManager->findVariable( EnvironmentFreezeSwitchInternal::kVariableName ) : nullptr;
            if ( pVariable != nullptr )
                pVariable->_onValueChanged = {};
        }

        const utf8* getSubsystemName() const override { return kSubsystemName; }
    };

    // 기동 단계 — 서비스 단계 초기화 중에는 엔진 서비스(GlobalVariableManager 조회)가 아직 묶이지 않았다(--test 가 시작 중 멈췄다).
    SW_ENGINE_SUBSYSTEM( EnvironmentFreezeSwitch, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        (void)scope.emplaceSubsystem<EnvironmentFreezeSwitch>();
    } );
} // namespace sw
