/**
 * @file LoadingScreenAutomation.cpp
 * @brief 화면 페이드의 자동화 캡처 차단(`ScreenFade`)입니다.
 * @details 시나리오 캡처만 묻는 조건이라 로딩 화면 TU 가 아니라 이 파일에 둔다 — 컨트롤러의 공개 읽기(`getUISystem` · `findFadeScreen`)만 쓴다.
 */
#include "pch.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 화면 페이드가 그림을 덮고 있으면 true 입니다(캡처 차단 조건). 로딩 화면이 닫힌 뒤 실제 시간으로 걷히므로 그동안 찍으면 어두운 화면이 찍힌다.
         * @details 페이드는 입력을 막지 않아 시나리오 시작은 미루지 않는다. 화면 UI 를 막은 동안(멈춘 에디터)은 그려지지 않으니 덮지 않는다.
         */
        bool isScreenFadeCoveringCapture()
        {
            const LoadingScreenController* pController = game::findSubsystem<LoadingScreenController>();
            const UISystem*                pUISystem   = pController != nullptr ? pController->getUISystem() : nullptr;
            return pUISystem != nullptr && pUISystem->isOnScreenSuppressed() == false && pController->findFadeScreen() != nullptr;
        }
    } // namespace

    SW_AUTOMATION_CAPTURE_BLOCKER( screenFade, "ScreenFade", &isScreenFadeCoveringCapture );
} // namespace sw
