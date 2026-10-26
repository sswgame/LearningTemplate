/**
 * @file AbilityDebugPanel.h
 * @brief 어빌리티 런타임 상태 창입니다. 고른 오브젝트의 `AbilitySystemComponent` 가 지금 가진 어트리뷰트 · 걸린 이펙트 · 태그 개수 · 어빌리티 쿨다운을 읽기 전용으로 보입니다.
 * @details 언리얼 gameplay debugger 의 AbilitySystem 카테고리, `showdebug abilitysystem` 의 자리다. 런타임 상태는 저장하지 않으므로 플레이 중에만 값이 있고,
 *          멈춰 있으면 빈 상태 힌트를 보인다. 대상은 선택한 오브젝트에 컴포넌트가 있으면 그것이고, 없으면 활성 씬의 컴포넌트 목록에서 고른 것이다.
 *          행 만들기는 `AbilityDebugModel` 이 맡는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/GUI/IEditorPanel.h"

#include "GameFramework/PlugIn/Editor/Base/Ability/AbilityDebugModel.h"

namespace sw
{
    class AbilitySystemComponent;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AbilityDebugPanel
     * @brief 어빌리티 시스템의 런타임 상태를 보이는 도구 창입니다.
     */
    class AbilityDebugPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id — 등록(`SW_EDITOR_PANEL`)과 조회가 같은 상수를 쓴다. */
        static constexpr const utf8* kPanelID = "ability_debugger";

        AbilityDebugPanel()
            : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다
        {
        }

        const utf8*      getPanelTitle() const override { return "Ability Debugger"; }
        bool             isToolPanel() const override { return true; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 69.0f, 34.0f ); }
        void             drawContent() override;

    private:
        AbilitySystemComponent* findTarget();
        void                    drawTargetPicker( const AbilitySystemComponent* pCurrent );
        void                    drawSnapshot();

        AbilityDebugSnapshot     _snapshot;
        vector<GameObjectHandle> _listCandidate;
        GameObjectHandle         _pickedHandle;
    };
} // namespace sw::editor
