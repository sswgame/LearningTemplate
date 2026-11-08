#include "pch.h"

#include "App/AppDevTools.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

// 개발 구성에만 있다 — 배포는 이 폴더를 짓지 않고, App 은 `build::kShipping` 이 아닌 갈래에서만 부른다(배포의 프레임 시간은 늘 벽시계다).
namespace sw
{
    /**
     * @brief `-gv_fixedFrameDelta=<초>`: 0 보다 크면 프레임마다 실제 시계 대신 그 시간을 흘립니다(자동화 시나리오, 픽셀 비교, 재현). 0 이면 실시간입니다.
     * @details 고정 스텝(물리, fixedUpdate)도 이 시간으로 나뉩니다. 1/60 이면 프레임마다 정확히 한 스텝입니다. 시나리오(`-scenario`)는 이 값을
     *          시나리오의 `fixedDelta` 로 설정합니다(`AutomationSession` · `ScenarioPlayer` 가 이름으로 찾는다).
     */
    SW_TEST_GLOBAL_VARIABLE( float32, gv_fixedFrameDelta, 0.0f, "프레임마다 흘릴 고정 시간(초, 0=실시간) — 결정적 실행" );

    float32 AppDevTools::getFixedFrameDelta()
    {
        return gv_fixedFrameDelta;
    }
} // namespace sw
