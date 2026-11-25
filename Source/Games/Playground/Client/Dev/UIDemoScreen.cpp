/**
 * @file UIDemoScreen.cpp
 * @brief 개발용 UI 견본 화면의 스위치입니다(`-gv_uiDemo=1`). 화면은 문서(벤치 설정의 `_uiDemoDocument` — `uidemo.ui.xml`)이고, 여기서는 열고 닫기만 합니다.
 */
#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/Service/UINotificationService.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

#include "Games/Playground/Client/Dev/StressTestSceneSettings.h"

SW_TEST_GLOBAL_VARIABLE( bool, gv_uiDemo, false,
                         "UI 테스트 화면을 띄운다 — 글 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 필드 · 그리기 견본(둥근 상자 · 자르기 · 9-슬라이스 · 오른쪽에서 왼쪽 글)" );

namespace sw
{
    namespace
    {
        struct UIDemoScreenInternal
        {
            /**
             * @brief 화면 스위치: `gv_uiDemo` 를 따라 견본 화면을 열고 닫습니다(열면 입력 방식을 탐색으로 바꿔 첫 버튼에 포커스 테두리를 보입니다).
             *        열 수 없으면(설정 · 문서 오류) 스위치를 내려 프레임마다 다시 시도하지 않는다.
             */
            static UIScreenHandle syncSwitch( UISystem& uiSystem, UIScreenHandle current )
            {
                UIScreen* const pCurrent = uiSystem.findScreen( current );
                const bool      bOpen    = pCurrent != nullptr;
                if ( gv_uiDemo == bOpen )
                    return current;
                if ( bOpen )
                {
                    pCurrent->close();
                    return kInvalidUIScreenHandle;
                }
                StressTestSceneSettings settings{};
                const UIScreenHandle    screen = StressTestSceneSettings::loadForGame( settings ) ? uiSystem.openScreen( settings._uiDemoDocument.path() ) : kInvalidUIScreenHandle;
                if ( screen == kInvalidUIScreenHandle )
                {
                    gv_uiDemo = false;
                    return kInvalidUIScreenHandle;
                }
                uiSystem.setInputMode( UIInputMode::Navigation );
                // 알림과 액션 글리프도 함께 보인다(오버레이 레이어, 스크린샷 확인용).
                UINotificationDef hint{};
                hint._text            = "[action=UI.Accept] Select";
                hint._durationSeconds = 600.0f;
                hint._kind            = UINotificationKind::Hint;
                game::getSubsystem<UINotificationService>().post( hint );
                return screen;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_UI_SCREEN_SWITCH( UIDemo, "UIDemo", 0, &UIDemoScreenInternal::syncSwitch );
} // namespace sw
