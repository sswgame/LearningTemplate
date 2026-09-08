/**
 * @file EditorSessionPolicy.h
 * @brief 미저장 확인·플레이 중 편집 허용 여부 (UI 없이 테스트 가능)
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /** @brief 미저장 모달에서 고른 항목 */
    enum class EditorUnsavedChoice : uint8
    {
        None = 0,
        Save,
        Discard,
        Cancel
    };

    /** @brief 미저장 확인 뒤에 이어서 할 씬·세션 동작 */
    enum class EditorPendingSceneAction : uint8
    {
        None = 0,
        Load,
        New,
        Quit
    };

    /**
     * @class EditorSessionPolicy
     * @brief 씬 dirty / 플레이 세션에 대한 가드 결정
     */
    class EditorSessionPolicy
    {
    public:
        /** @brief 저장하지 않은 변경이 있으면 확인이 필요합니다. */
        static bool needsUnsavedPrompt( bool bDirty ) { return bDirty == true; }
        /** @brief 씬 또는 도구 문서가 dirty면 종료 확인이 필요합니다. */
        static bool needsQuitPrompt( bool bSceneDirty, uint32 dirtyDocumentCount )
        {
            return bSceneDirty == true || dirtyDocumentCount > 0;
        }
        /** @brief Save를 고르면 동작을 실행하기 전에 저장합니다. */
        static bool shouldSaveBeforeAction( EditorUnsavedChoice choice ) { return choice == EditorUnsavedChoice::Save; }
        /** @brief Cancel이 아니면 대기 중인 씬 동작을 실행합니다. */
        static bool shouldProceedWithAction( EditorUnsavedChoice choice )
        {
            return choice == EditorUnsavedChoice::Save || choice == EditorUnsavedChoice::Discard;
        }
        /** @brief Don't Save면 저장하지 않고 dirty를 지웁니다. */
        static bool shouldClearDirtyWithoutSave( EditorUnsavedChoice choice )
        {
            return choice == EditorUnsavedChoice::Discard;
        }
        /** @brief Stopped일 때만 씬 오브젝트 편집이 허용됩니다. */
        static bool areSceneEditsAllowed( bool bPlayStopped ) { return bPlayStopped == true; }
        /** @brief Isolation은 활성 씬을 유지하므로 dirty 씬에서도 들어갈 수 있습니다. */
        static bool requiresCleanSceneForPrefabIsolation() { return false; }
        /** @brief 레이아웃이 한 번 동기된 뒤에만 노드 이동을 dirty로 칩니다. */
        static bool shouldMarkDocumentDirtyOnNodeMove( bool bLayoutReady, bool bPositionChanged )
        {
            return bLayoutReady == true && bPositionChanged == true;
        }
        /** @brief 로컬라이즈·게임 데이터·세션 GV 중 하나라도 dirty면 도구 세션이 dirty입니다. */
        static bool isToolSessionDirty( bool bLocalizationDirty, bool bGameDataDirty, bool bGlobalVariableDirty )
        {
            return bLocalizationDirty == true || bGameDataDirty == true || bGlobalVariableDirty == true;
        }
        /** @brief 같은 coalesce 키만 연속 편집으로 합칩니다. 빈 키는 합치지 않습니다. */
        static bool shouldCoalesceDocumentEdits( string_view previousKey, string_view nextKey )
        {
            if ( previousKey.empty() || nextKey.empty() )
                return false;
            return previousKey == nextKey;
        }
        /** @brief Undo 복원 텍스트가 마지막 저장본과 같으면 dirty를 지웁니다. */
        static bool shouldClearDocumentDirtyOnRestore( bool bMatchesLastSaved )
        {
            return bMatchesLastSaved == true;
        }
    };
} // namespace sw::editor
