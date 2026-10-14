/**
 * @file EditorViewportMouseClaim.h
 * @brief 확장 도구(모디파이어 기즈모 등)가 씬 뷰의 마우스를 가져가는 창구입니다. 가져간 동안 클릭 선택과 변환 기즈모가 물러납니다.
 */
#pragma once
#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @class EditorViewportMouseClaim
     * @brief 소유자 포인터 하나를 둡니다. `EditorColliderHandleTool::isCapturingMouse` 와 같은 자리에서 뷰포트가 이 값을 봅니다.
     * @details 소유자는 마우스가 손잡이 위에 있거나 끄는 동안 `acquire`, 벗어나면 `release` 한다. 새 소유자가 가져가면 이전 소유자는 잃는다.
     *          포인터는 견주기만 하고 따라가지 않는다. 메인(UI) 스레드에서만 부른다.
     */
    class SW_EDITOR_API EditorViewportMouseClaim
    {
    public:
        /** @brief pOwner 가 마우스를 가져갑니다. 이미 다른 소유자가 있으면 그 소유자는 잃습니다. */
        static void acquire( const void* pOwner );
        /** @brief pOwner 가 지금 소유자일 때만 풉니다. */
        static void release( const void* pOwner );
        /** @brief 누군가 마우스를 가져갔으면 true 입니다. */
        static bool isClaimed();
        /** @brief pOwner 가 지금 소유자이면 true 입니다. */
        static bool isClaimedBy( const void* pOwner );
    };
} // namespace sw::editor
