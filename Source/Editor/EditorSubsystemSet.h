/**
 * @file EditorSubsystemSet.h
 * @brief 에디터 범위에 들어가는 내장 하위 객체를 정하는 조립점입니다(셸이 `EditorContext::initialize` 에 넘긴다).
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class EditorContext;

    /**
     * @struct EditorSubsystemSet
     * @brief 에디터의 내장 하위 객체(선택 · 패널 · 팝업 · 인스펙터 매니저 · 플레이 세션 · 테마 등) 목록과 확장 등록 맞추기입니다.
     * @details 컨텍스트(`EditorContext`, 모델 층)는 범위와 생명 주기만 들고, 무엇을 넣는지는 여기(최상위 층)가 정한다 — 모델 층이 GUI 매니저를 include 하지 않는다.
     */
    struct SW_EDITOR_API EditorSubsystemSet
    {
        /** @brief @p scope 에 내장 하위 객체를 모두 넣습니다. 넣는 순서는 뜻이 없고 초기화 순서는 각자 선언한 의존이 정한다. */
        static void install( SubsystemScope& scope, EditorContext& context );

        /**
         * @brief 패널, 팝업, 인스펙터 매니저를 등록 목록과 맞춥니다. 에디터 프레임 앞에서 호출합니다.
         * @details 확장 모듈은 에디터가 뜬 뒤에도 로드되고 언로드된다(핫 리로드). 등록 세대가 같으면 비교 한 번으로 끝난다.
         */
        static void syncExtensionRegistrations( EditorContext& context );
    };
} // namespace sw::editor
