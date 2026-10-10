/**
 * @file UIDemoScreen.h
 * @brief 개발용 UI 시험 화면입니다(`-gv_uiDemo=1`) — 글 · 버튼 다섯 · 슬라이더 · 체크 · 진행 · 콤보 · 입력 칸을 한 패널에 띄웁니다.
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
     * @brief 코드로 지은 위젯 트리가 게임 창에 보이는지(네 백엔드 스크린샷) · 패드로 포커스가 옮겨지는지 보는 화면입니다. 에셋 없이 섭니다(그림 없음).
     * @details `UISystem::update` 가 `gv_uiDemo` 를 보고 열고 닫습니다. 열 때 입력 방식을 탐색으로 두어 첫 버튼에 포커스 테두리가 보입니다.
     */
    struct SW_API UIDemoScreen
    {
        /** @brief 시험 화면을 짓습니다(Menu 층 · 게임 정지 없음 · 기본 포커스 첫 버튼). */
        static unique_ptr<UIScreen> create();
    };
} // namespace sw
