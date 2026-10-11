/**
 * @file UIDemoScreen.h
 * @brief 개발용 UI 테스트 화면입니다(`-gv_uiDemo=1`). 텍스트, 버튼 다섯 개, 슬라이더, 체크, 진행 막대, 콤보, 입력 필드를 한 패널에 띄웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

SW_EXTERN_GLOBAL_VARIABLE( bool, gv_uiDemo );

namespace sw
{
    class UIScreen;

    /**
     * @struct UIDemoScreen
     * @brief 코드로 만든 위젯 트리가 게임 창에 보이는지(네 백엔드 스크린샷)와 패드로 포커스가 옮겨지는지 확인하는 화면입니다. 에셋 없이 동작합니다(이미지 없음).
     * @details `UISystem::update` 가 `gv_uiDemo` 를 보고 열고 닫는다. 열 때 입력 방식을 탐색으로 두어 첫 버튼에 포커스 테두리가 보인다.
     */
    struct SW_API UIDemoScreen
    {
        /** @brief 테스트 화면을 만듭니다(Menu 레이어, 게임 정지 없음, 기본 포커스는 첫 버튼). */
        static unique_ptr<UIScreen> create();
    };
} // namespace sw
