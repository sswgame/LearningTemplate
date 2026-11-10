/**
 * @file RigTargetGizmoVisualizer.h
 * @brief 리그 대상 오프셋 기즈모 시각화(뷰포트 시각화 하나, 등록은 cpp)와 리그 디버그 창이 함께 보는 편집 상태입니다.
 * @details 리그 디버그 창이 대상을 고르고(`RigTargetEdit::_objectID` · `_targetIndex`), 이 시각화가 그 대상에 기즈모를 그린다. 끄는 동안의 값은 `_pending` 이고,
 *          놓으면 리그 파일에 되써 넣는다(`RigSaveParams::writeToRigFile`, 파일 글 전후가 되돌리기 하나) — 파일 감시가 다시 읽어 인스턴스에 들어올 때까지 기즈모는 `_pending` 에 남는다.
 *          창(패널 층)과 시각화(뷰포트 층)가 다른 층이라 상태는 아래 층인 여기서 에디터 범위의 상태 저장소가 소유한다(에디터는 한 스레드).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw::editor
{
    /** @brief 대상 오프셋 기즈모의 편집 상태입니다. 파일 머리말 참고. */
    struct SW_EDITOR_API RigTargetEdit
    {
        BoneTransform    _pending{};
        string           _status{};      ///< 지난 저장 결과 줄
        uint64           _objectID{ 0 }; ///< 기즈모를 놓은 대상의 오브젝트(0 이면 없음)
        optional<uint32> _targetIndex;   ///< 기즈모를 놓은 대상 번호(없으면 비어 있다)
        uint8            _bPending{ SW_FALSE };
        uint8            _bDragging{ SW_FALSE };

        /** @brief 에디터 범위의 편집 상태입니다. */
        static RigTargetEdit& get();
    };
} // namespace sw::editor
