/**
 * @file EditorInspectorCommands.h
 * @brief 인스펙터 프로퍼티 Undo / 프리팹 적용·복원 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorInspectorCommands
     * @brief ImGui 활성화 스냅샷이 끝난 뒤 Undo 스택에 올리고, 프리팹을 적용/복원합니다.
     */
    class EditorInspectorCommands
    {
    public:
        /** @brief POD 프로퍼티 전/후 바이트를 Undo에 올립니다. */
        static void pushPodEdit( void* pData, size_t size, vector<uint8> beforeBytes, vector<uint8> afterBytes,
                                 string_view label, uint64 selectedObjectId );
        /** @brief 문자열 프로퍼티 전/후 값을 Undo에 올립니다. */
        static void pushStringEdit( string* pPtr, string before, string after, string_view label, uint64 selectedObjectId );
        /** @brief 선택 오브젝트 상태를 프리팹 XML로 저장합니다. */
        static bool applyToPrefab( GameObject* pObj, string_view prefabPath );
        /** @brief 프리팹 상태로 되돌리고 Undo에 기록합니다. */
        static bool revertToPrefab( GameObject* pObj, string_view prefabPath );
        /** @brief 오브젝트와 프리팹 연결을 끊습니다. */
        static void unlinkPrefab( GameObject* pObj );
    };
} // namespace sw::editor
