/**
 * @file EditorPanelDump.h
 * @brief 이번 프레임에 ImGui 가 **실제로 무엇을 그렸는지** 로그로 덤프합니다(패널 변경 검증용).
 *
 * @details 에디터 패널에는 단위 테스트가 없습니다. ImGui 를 띄워야 그려지므로 `Test/EditorTest` 는 ImGui 없이 도는
 *          것만 검증합니다. 그래서 패널을 고치면 "컴파일은 되는데 화면이 비었다" 가 조용히 통과합니다. 화면을 볼 수 없는
 *          환경(CI · 자동화 · 원격)에서도 그 실패를 잡으려면 그려진 결과를 숫자로 남겨야 합니다.
 *
 *          `-gv_editorPanelDump=N` 을 주면 N 번째 프레임에 창 하나당 한 줄(이름 · 크기 · 정점 수 · 활성 여부)을
 *          남깁니다. **열려 있는데 정점이 0 인 창**이 곧 빈 패널이고, 요약 줄이 그 개수를 따로 셉니다. 리팩터링
 *          전후의 덤프를 비교하면 됩니다.
 *
 * @note 비교할 때 정점 수가 **정확히 같기를** 기대하면 안 됩니다. 폰트 · DPI · 도킹 상태 · 애니메이션이 값을
 *       흔듭니다. 보는 것은 "0 이 아닌가" 와 "창 목록이 그대로인가" 입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorPanelDump
     * @brief ImGui 창별 드로우 통계를 로그로 남기는 진단 도구입니다.
     */
    struct EditorPanelDump
    {
        /**
         * @brief 요청된 프레임이면 한 번 덤프합니다. ImGui EndFrame 직후에 부릅니다.
         * @details `gv_editorPanelDump` 가 0 이면 아무것도 하지 않습니다. EndFrame 이후여야
         *          창의 DrawList 가 이번 프레임의 최종 내용을 담습니다.
         */
        static void dumpIfRequested();
    };
} // namespace sw::editor
