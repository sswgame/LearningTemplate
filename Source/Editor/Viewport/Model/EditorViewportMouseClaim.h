/**
 * @file EditorViewportMouseClaim.h
 * @brief 확장 도구(모디파이어 기즈모 등)가 씬 뷰의 마우스를 가져가는 진입점입니다. 가져간 동안 클릭 선택과 변환 기즈모가 물러납니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @class EditorViewportMouseClaim
     * @brief 소유자 포인터 하나를 둡니다. `EditorColliderHandleTool::isCapturingMouse` 와 같은 방식으로 뷰포트가 이 값을 봅니다.
     * @details 소유자는 마우스가 손잡이 위에 있거나 끄는 동안 **매 프레임** `acquire` 하고, 벗어나면 `release` 한다. 새 소유자가 가져가면 이전 소유자는 잃는다.
     *          포인터는 비교하기만 하고 따라가지 않는다. 메인(UI) 스레드에서만 부른다.
     *
     *          **프레임 순서 계약.** 뷰포트는 한 프레임을 `beginFrame` → 시각화 그리기(`acquire` 가 여기서 일어난다) → 클릭 선택 · 변환 기즈모(`isClaimed` 를 본다)
     *          순서로 처리한다. 시각화가 같은 프레임에 잡아야 그 프레임의 첫 클릭이 선택으로 새지 않는다 — 클릭 선택은 항상 시각화 그리기 뒤다.
     *          소유자가 한 프레임 동안 `acquire` 를 다시 부르지 않으면(끄는 중에 시각화 토글을 끈 경우처럼 그리기 함수가 안 불린다) 다음 `beginFrame` 이 소유권을 회수한다.
     *          그래서 `release` 가 빠져도 마우스가 영영 잠기지 않는다.
     */
    class SW_EDITOR_API EditorViewportMouseClaim
    {
    public:
        /** @brief 새 프레임이 시작됐음을 알립니다. 같은 프레임 번호로 여러 번 불러도(뷰포트가 여럿) 한 번만 셉니다. 지난 프레임에 `acquire` 가 없던 소유자는 잃습니다. */
        static void beginFrame( uint64 frameIndex );
        /** @brief pOwner 가 마우스를 가져갑니다(이미 소유자면 이번 프레임에도 유지됨을 알립니다). 다른 소유자가 있으면 그 소유자는 잃습니다. */
        static void acquire( const void* pOwner );
        /** @brief pOwner 가 지금 소유자일 때만 풉니다. */
        static void release( const void* pOwner );
        /** @brief 누군가 마우스를 가져갔으면 true 입니다. */
        static bool isClaimed();
        /** @brief pOwner 가 지금 소유자이면 true 입니다. */
        static bool isClaimedBy( const void* pOwner );
    };
} // namespace sw::editor
