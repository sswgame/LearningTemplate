/**
 * @file InspectorEditRecorder.h
 * @brief 인스펙터 프로퍼티 편집을 되돌리기 한 단계로 묶는 기록기입니다 — 편집이 시작될 때 선택 오브젝트의 "편집 전" 스냅샷을 찍고, 끝난 편집을 프레임 끝에 기록합니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약). 위젯의 활성 · 해제를 읽어 여기로 넘기는 일은 View(`InspectorPropertyUndo`)가 한다.
 * @details 프로퍼티 그리드 편집 모델 층의 첫 조각이다(같은 절의 '프로퍼티 그리드 편집 모델'). 편집 키는 위젯 id 이고, 대상은 위젯이 풀릴 때까지 여러 프레임을 넘기므로 핸들로 든다.
 *          상태는 에디터 범위의 상태 저장소가 소유한다 — 에디터가 사라지면 붙잡은 스냅샷도 함께 사라진다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 인스펙터 편집 기록기입니다(상태는 에디터 상태 저장소). */
    struct InspectorEditRecorder
    {
        /** @brief 위젯 @p widgetID 의 편집이 시작되었습니다. 겨누는 선택의 오브젝트 전부의 "편집 전" 을 찍습니다. @p pLabel 이 널이면 "Property" 입니다. */
        static void beginEdit( uint32 widgetID, const utf8* pLabel );
        /** @brief 위젯 @p widgetID 의 편집이 바뀐 값으로 끝났습니다. 기록은 `commitFinishedEdits` 가 합니다(시작되지 않은 id 면 아무것도 하지 않는다). */
        static void finishEdit( uint32 widgetID );
        /**
         * @brief 값을 쓰기 **직전에** 불러 그 한 번의 변경을 되돌리기에 남깁니다. 편집 전을 지금 찍고, 기록은 `commitFinishedEdits` 가 한다.
         * @details 콤보처럼 고르는 순간 값이 바뀌는 위젯용이다(고른 항목은 팝업 창의 다른 위젯이라 해제로는 잡히지 않는다).
         */
        static void recordImmediateEdit( const utf8* pLabel );
        /**
         * @brief 이번 프레임에 끝난 편집을 되돌리기에 남깁니다. 인스펙터가 그리기를 마친 뒤 부릅니다.
         * @details 위젯이 풀린 순간이 아니라 그 뒤에 기록하는 까닭: 다중 선택은 편집 통지(위젯 뒤)에서 값을 나머지 오브젝트에 입힌다. 풀린 순간에 "뒤" 스냅샷을 뜨면 그 입히기가 빠진다.
         *          플레이 중 편집은 기록하지 않는다 — 편집 내역은 맡겨 두었고(`CommandStack::suspendHistory`) Stop 이 스냅샷으로 되돌린다.
         */
        static void commitFinishedEdits();
    };
} // namespace sw::editor
