/**
 * @file EditorCompactControls.h
 * @brief 긴 글 단추 대신 쓰는 짧은 컨트롤(아이콘 단추 · 보기 옵션 팝업 · 기본값 되돌리기)입니다. 패널마다 같은 모양으로 그립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @class EditorCompactControls
     * @brief 툴바 · 속성 행에서 글 단추 줄을 줄이는 공용 컨트롤입니다.
     * @details 언리얼 · 유니티 · Godot 의 패널 툴바는 동작을 아이콘 단추 + 툴팁으로 두고, 드물게 쓰는 보기 설정은 한 단추(언리얼 Details 의 View Options,
     *          Content Browser 의 Settings)의 팝업에 모은다. 패널마다 따로 짜면 막힘 이유 · 켜진 상태 표시가 갈라져(인스펙터 보기 옵션은 켜도 모양이 같았다)
     *          한 곳에 둔다. 켜진 상태는 강조색이 아니라 중립 단계 바탕이다(앱 화면 규칙 — 강조색은 데이터 · 선택에만).
     */
    class SW_EDITOR_API EditorCompactControls
    {
    public:
        /**
         * @brief 아이콘만 있는 툴바 단추를 그립니다. 눌렀으면 true 입니다.
         * @param pDisabledReason nullptr 가 아니면 단추를 막고, 툴팁에 그 이유를 보입니다(막힌 단추도 왜 막혔는지 말한다).
         */
        static bool drawIconButton( const utf8* pID, const utf8* pIcon, const utf8* pTooltip, const utf8* pDisabledReason = nullptr );

        /**
         * @brief 보기 옵션 단추(깔때기)를 그리고, 그 팝업이 열려 있으면 true 를 반환합니다. true 이면 항목을 그린 뒤 `endViewOptions` 를 부릅니다.
         * @param bFiltered 보기가 걸러져 있으면(일부만 보이면) true — 단추가 켜진 토글 바탕으로 그려져 거른 줄 모르고 항목이 사라졌다고 여기지 않게 한다.
         * @param pMark 시나리오가 단추를 누를 때 쓰는 표지입니다. nullptr 이면 남기지 않습니다.
         */
        static bool beginViewOptions( const utf8* pID, bool bFiltered, const utf8* pTooltip, const utf8* pMark );
        /** @brief `beginViewOptions` 가 true 였을 때 팝업을 닫습니다. */
        static void endViewOptions();

        /**
         * @brief 값이 기본값과 다를 때 행 끝에 두는 되돌리기 단추(↺)를 같은 줄에 그립니다. 눌렀으면 true 입니다.
         * @details 언리얼 Details 의 Reset to Default 화살표 · 유니티 머티리얼 인스펙터의 되돌리기와 같은 자리다. 인스펙터와 머티리얼 창이 함께 쓴다.
         */
        static bool drawResetButton( const utf8* pID, const utf8* pTooltip );
    };
} // namespace sw::editor
