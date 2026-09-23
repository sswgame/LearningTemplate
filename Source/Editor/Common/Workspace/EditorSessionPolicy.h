/**
 * @file EditorSessionPolicy.h
 * @brief 미저장 확인·플레이 중 편집 허용 여부 (UI 없이 테스트 가능)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"

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
     * @brief 씬 dirty · 플레이 세션 상태에 따라 무엇을 허용할지 정합니다.
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
        /** @brief Save 를 골랐으면 true 입니다(동작을 실행하기 전에 저장합니다). */
        static bool shouldSaveBeforeAction( EditorUnsavedChoice choice ) { return choice == EditorUnsavedChoice::Save; }
        /** @brief Cancel 이 아니면 true 입니다(대기 중인 씬 동작을 실행합니다). */
        static bool shouldProceedWithAction( EditorUnsavedChoice choice )
        {
            return choice == EditorUnsavedChoice::Save || choice == EditorUnsavedChoice::Discard;
        }
        /** @brief Don't Save 를 골랐으면 true 입니다(저장하지 않고 dirty 를 지웁니다). */
        static bool shouldClearDirtyWithoutSave( EditorUnsavedChoice choice )
        {
            return choice == EditorUnsavedChoice::Discard;
        }
        /** @brief Stopped일 때만 씬 오브젝트 편집이 허용됩니다. */
        static bool areSceneEditsAllowed( bool bPlayStopped ) { return bPlayStopped == true; }
        /** @brief Isolation은 활성 씬을 유지하므로 dirty 씬에서도 들어갈 수 있습니다. */
        static bool requiresCleanSceneForPrefabIsolation() { return false; }
        /**
         * @brief 컨테이너 **원소를 제자리에서 편집해도 되는지** 판단합니다. 두 조건이 모두 허락해야 합니다.
         * @param bReadOnly                 프로퍼티가 `ReadOnly` 로 표시돼 있는가.
         * @param bAllowsInPlaceElementWrite 컨테이너가 제자리 쓰기를 허용하는가
         *                                  (`ISequenceContainerWrapper::allowsInPlaceElementWrite`).
         * @details 인스펙터는 두 질문을 **따로** 물어 각각 다른 것을 막고 있었습니다. `ReadOnly` 는 `+ Add` · `Clear` 버튼만
         *          감췄고, **원소 위젯은 그대로 편집할 수 있었습니다.** 즉 컨테이너 프로퍼티에서는 `ReadOnly` 가 조용히
         *          무시되고 있었습니다. 연관 컨테이너에서는 더 나쁩니다. 원소가 곧 정렬 키라 제자리 편집이 **트리를
         *          깨뜨립니다.** 두 조건을 여기 한곳에 모아 테스트가 지키게 합니다.
         */
        static bool areContainerElementEditsAllowed( bool bReadOnly, bool bAllowsInPlaceElementWrite )
        {
            return bReadOnly == false && bAllowsInPlaceElementWrite == true;
        }
        /**
         * @brief 노드가 움직였는지 판단합니다. **어느 한 축이라도** 달라지면 움직인 것입니다.
         * @details 그래프 패널 둘이 이 판단을 각자 적고 있었고 **연산자가 서로 달랐습니다.** 애니메이션은 `||`, 대화는 `&&`
         *          였습니다. `&&` 쪽에서는 노드를 **정확히 수평으로만**(또는 수직으로만) 옮기면 두 축 중 하나가 그대로라
         *          "안 움직였다" 가 되어, 그 레이아웃 변경이 dirty 로 잡히지 않고 조용히 사라졌습니다. 축 하나만 움직이는 것은
         *          드문 일이 아닙니다. 캔버스 정렬이 그렇게 만듭니다. 판단을 여기로 올려 **한 곳에서만** 정하고, 테스트가 그
         *          규칙을 지킵니다.
         */
        static bool hasNodeMoved( float32 previousX, float32 previousY, float32 currentX, float32 currentY )
        {
            return ( MathUtil::nearEqual( previousX, currentX ) == false ) ||
                   ( MathUtil::nearEqual( previousY, currentY ) == false );
        }

        /** @brief 레이아웃이 한 번 동기화된 뒤에만 노드 이동을 dirty로 칩니다. */
        static bool shouldMarkDocumentDirtyOnNodeMove( bool bLayoutReady, bool bPositionChanged )
        {
            return bLayoutReady == true && bPositionChanged == true;
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
