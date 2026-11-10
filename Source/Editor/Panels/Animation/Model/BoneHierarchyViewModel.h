/**
 * @file BoneHierarchyViewModel.h
 * @brief 본 계층 팝업의 ViewModel — 고른 오브젝트의 자식 오브젝트 · 씬 컴포넌트 계층을 앞 순서(pre-order) 줄로 펴서 모읍니다(`gather`). ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 보기 전용이라 명령이 없다. 줄은 깊이를 들어 View 가 트리를 다시 세운다(접힌 노드의 아래 줄은 View 가 건너뛴다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObject;
    class SceneComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 팝업이 그릴 내용의 종류입니다. */
    enum class BoneHierarchyContent : uint8
    {
        NoSelection = 0, ///< 고른 오브젝트가 없다
        NoActiveScene,   ///< 편집 중인 씬이 없다
        ObjectNotFound,  ///< 고른 오브젝트를 활성 씬에서 찾지 못했다
        Tree             ///< 줄이 있다
    };

    /** @brief 계층 한 줄입니다(값만). 오브젝트 줄은 펼칠 수 있고, 컴포넌트 줄은 잎이다. */
    struct BoneHierarchyRow
    {
        string _name{};
        uint64 _id{ 0 };                  ///< 오브젝트 id 또는 컴포넌트 id(트리 노드 id)
        uint32 _depth{ 0 };               ///< 오브젝트 깊이(고른 오브젝트가 0, 그 컴포넌트 · 자식이 1)
        uint8  _bComponent{ SW_FALSE };   ///< 씬 컴포넌트 줄이다
        uint8  _bHasChildren{ SW_FALSE }; ///< 오브젝트 줄에 아래 줄(컴포넌트 · 자식)이 있다
    };

    /** @brief 본 계층 팝업이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct BoneHierarchyViewState
    {
        BoneHierarchyContent     _content{ BoneHierarchyContent::NoSelection };
        string                   _selectedName{};
        vector<BoneHierarchyRow> _listRow{};
    };

    /** @brief 본 계층 팝업의 ViewModel 입니다(상태 없음). */
    class BoneHierarchyViewModel
    {
    public:
        /** @brief 고른 오브젝트의 계층 줄을 @p outState 에 채웁니다(비우고 채운다). */
        void gather( BoneHierarchyViewState& outState ) const;

    private:
        /** @brief 오브젝트 @p object 와 그 아래(주 씬 컴포넌트 계층, 자식 오브젝트)를 @p depth 에서 줄로 붙입니다. */
        static void collectObjectRows( const GameObject& object, uint32 depth, vector<BoneHierarchyRow>& outListRow );
        /** @brief 씬 컴포넌트 @p component 와 그 자식들을 @p depth 의 잎 줄로 붙입니다. */
        static void collectComponentRows( const SceneComponent& component, uint32 depth, vector<BoneHierarchyRow>& outListRow );
    };
} // namespace sw::editor
