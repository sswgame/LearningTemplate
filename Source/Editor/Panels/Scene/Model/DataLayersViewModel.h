/**
 * @file DataLayersViewModel.h
 * @brief 데이터 레이어 창의 ViewModel — 활성 씬에서 레이어 줄을 모으고(`gather`) 켜고 끄는 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 보이기(`setLayerShown`)는 플레이 중에는 런타임과 같은 길로 올리고 내린다(`SceneDataLayerSubsystem`). 편집 중에 끄면 오브젝트를 내리지 않고 에디터에서만 숨긴다 —
 *          내리면 저장하지 않은 편집을 잃는다. 숨김은 보기만 바꾸므로 되돌리기 · 씬 dirty 에 남지 않는다(언리얼 Data Layer 눈 아이콘 · 유니티 Scene visibility 와 같다).
 *          켜면 아직 짓지 않은 묶음을 짓는다. 씬이 열릴 때 켜질지(`setLayerInitiallyActive`)는 씬 파일에 적히는 문서 데이터라 되돌리기를 거친다(`EditorSceneSettingsCommands`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 레이어 하나의 표 줄입니다(값만). */
    struct DataLayerRow
    {
        string _name;
        uint32 _objectCount{ 0 };             ///< 지금 지어진 오브젝트 수
        uint8  _bShown{ SW_FALSE };           ///< 켜져 있고 숨기지 않았다
        uint8  _bInitiallyActive{ SW_FALSE }; ///< 씬이 열릴 때 켜진다(저장되는 값)
    };

    /** @brief 창이 그릴 내용의 종류입니다. */
    enum class DataLayersContent : uint8
    {
        NoActiveScene = 0, ///< 편집 중인 씬이 없다
        NoLayers,          ///< 씬이 레이어를 선언하지 않았다
        Layers             ///< 줄이 있다
    };

    /** @brief 데이터 레이어 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct DataLayersViewState
    {
        DataLayersContent    _content{ DataLayersContent::NoActiveScene };
        vector<DataLayerRow> _listRow{};
    };

    /** @brief 데이터 레이어 창의 ViewModel 입니다. */
    class DataLayersViewModel
    {
    public:
        DataLayersViewModel();

        /** @brief 활성 씬에서 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). 활성 씬이 바뀌었으면 숨긴 레이어 기록을 버립니다(다른 씬의 이름이다). */
        void gather( DataLayersViewState& outState );
        /** @brief 레이어를 보이거나 감춥니다(파일 머리말의 규칙). 활성 씬이 없으면 아무것도 하지 않습니다. */
        void setLayerShown( const string& name, bool bShown );
        /** @brief 씬이 열릴 때 레이어가 켜질지 정합니다. 되돌리기 한 단계와 씬 dirty 를 남깁니다(`EditorSceneSettingsCommands::apply`). */
        void setLayerInitiallyActive( const string& name, bool bInitiallyActive );

    private:
        /** @brief @p scene 의 줄을 채웁니다(비우고 채운다). */
        void collectRows( const Scene& scene, vector<DataLayerRow>& outListRow ) const;

    private:
        unordered_set<string> _uniqueHiddenLayer; ///< 편집 중에 감춘 레이어
        uint64                _sceneGeneration;   ///< 숨긴 기록이 속한 씬 세대(`SceneManager::getSceneGeneration`)
    };
} // namespace sw::editor
