/**
 * @file PrefabOverridesModel.h
 * @brief 프리팹 오버라이드 도구의 모델 — 고른 인스턴스의 프리팹 경로 · 이름, 오버라이드 · 중첩 프리팹 목록, 바뀐 것만 보기, 다시 훑기 · 적용 · 되돌리기 명령. ImGui 를 모릅니다.
 * @details 패널(`PrefabPanel`)은 표 · 단추와 프리팹 창(`PrefabEditorPanel`) 열기만 한다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 레벨의 블루프린트 인스턴스 오버라이드 보기가 인스턴스 · 원형 차이 계산과 Slate 그리기를 나누는 것과 같다).
 *          대상 인스턴스는 메인 씬 선택의 첫 오브젝트다. 명령은 `EditorToolAssetCommands` 를 부르고 결과를 로그에 남긴 뒤 목록을 다시 훑는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"

namespace sw::editor
{
    /**
     * @class PrefabOverridesModel
     * @brief 프리팹 오버라이드 도구의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PrefabOverridesModel
    {
    public:
        /** @brief 지금 선택으로 한 번 훑은 채 시작합니다. */
        PrefabOverridesModel();

        /** @brief 워크스페이스 포커스(프리팹 경로)나 고른 오브젝트가 지난 훑기 뒤 바뀌었으면 다시 훑습니다. 패널이 그리기마다 부릅니다. */
        void refreshIfFocusChanged();
        /** @brief 지금 고른 프리팹 경로로 다시 훑습니다(Refresh Overrides 단추). */
        void rescan();

        /** @brief 인스턴스의 오버라이드를 모두 프리팹 원형에 씁니다. 결과는 로그에 남기고 목록을 다시 훑습니다. */
        void applyAllToTemplate();
        /** @brief 인스턴스의 오버라이드를 모두 원형 값으로 되돌립니다. 결과는 로그에 남기고 목록을 다시 훑습니다. */
        void revertAll();
        /** @brief 오버라이드 @p overrideIndex 하나를 원형 값으로 되돌리고 목록을 다시 훑습니다. */
        void revertOverride( size_t overrideIndex );

        /** @brief 고른 인스턴스의 프리팹 경로입니다. 프리팹 인스턴스가 아니면 비어 있습니다. */
        const string&                     getPrefabPath() const { return _selectedPrefabPath; }
        const string&                     getInstanceName() const { return _selectedInstanceName; }
        const vector<PrefabOverrideItem>& getOverrides() const { return _listOverride; }
        const vector<string>&             getNestedPrefabs() const { return _listNestedPrefab; }
        /** @brief 바뀐 프로퍼티만 보이는지입니다(체크 상자가 직접 고친다). */
        bool& editShowOnlyModified() { return _bShowOnlyModified; }
        /** @brief 표에 @p item 을 보이면 true 입니다(바뀐 것만 보기면 바뀐 것만). */
        bool isVisible( const PrefabOverrideItem& item ) const { return _bShowOnlyModified == false || item._bModified; }

    private:
        void scan( const utf8* pPrefabPath );

        string                     _selectedPrefabPath;
        string                     _selectedInstanceName;
        string                     _lastScanKey; ///< 지난 훑기의 포커스 열쇠(`EditorAssetTypeRegistry::consumeWorkspaceFocusKey`)
        vector<PrefabOverrideItem> _listOverride;
        vector<string>             _listNestedPrefab;
        bool                       _bShowOnlyModified;
    };
} // namespace sw::editor
