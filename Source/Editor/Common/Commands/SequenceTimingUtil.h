/**
 * @file SequenceTimingUtil.h
 * @brief 시퀀서 타임라인의 클립 배치(시작 · 끝 · 종류 · 개수)를 떠 두고 바뀌었는지 봅니다. ImGui 를 모릅니다(EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Sequencer/SequenceAsset.h"

namespace sw::editor
{
    /** @brief 클립 하나의 배치입니다. */
    struct SequenceClipTiming
    {
        int32            _start{ 0 };
        int32            _end{ 0 };
        SequenceItemKind _kind{ SequenceItemKind::Clip };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct SequenceTimingUtil
     * @brief 타임라인 위젯(ImSequencer)을 부르기 전후의 배치를 비교합니다.
     * @details ImSequencer 는 여러 항목을 그리는 위젯이라 `ImGui::IsItemEdited()`("마지막 항목") 가 클립 끌기 · 더하기 · 지우기를 뜻하지 않는다.
     *          그래서 끌어 옮긴 클립이 dirty 표시 · 되돌리기에 남지 않았다. 부르기 전 배치를 떠 두고 뒤와 비교한다.
     */
    struct SequenceTimingUtil
    {
        /** @brief 항목들의 배치를 @p outListTiming 에 뜹니다(앞 내용은 버린다 — 멤버 버퍼를 다시 쓰면 프레임마다 할당하지 않는다). */
        static void captureTiming( const vector<SequenceTrackItem>& listItem, vector<SequenceClipTiming>& outListTiming );
        /** @brief 떠 둔 배치와 지금 항목들의 개수 · 시작 · 끝 · 종류 가운데 하나라도 다르면 true 입니다. */
        static bool hasTimingChanged( const vector<SequenceClipTiming>& listTimingBefore, const vector<SequenceTrackItem>& listItem );
    };
} // namespace sw::editor
