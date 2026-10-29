/**
 * @file PreferencesModel.h
 * @brief 환경설정 창의 모델 — 검색 글에 맞는 섹션 목록(검색 글 · 섹션 등록 세대 · 타입 레지스트리 세대가 바뀐 프레임에만 다시 짓기), 고른 섹션, "바뀐 것만" 숨길 프로퍼티,
 *        섹션 되돌리기, 고친 뒤 잠시 기다렸다 저장하기. ImGui 를 모릅니다.
 * @details 패널(`PreferencesPanel`)은 섹션 목록 · 프로퍼티 그리드 · 검색 칸(`EditorPropertyGrid`)을 그리기만 한다 — 전역 변수 창(`GlobalVariablesModel`)과 같은 선이다
 *          (언리얼 Editor Preferences 의 섹션 목록 · 설정 객체와 디테일 뷰가 나뉜 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw::editor
{
    struct EditorSettingsRegistration;

    /**
     * @class PreferencesModel
     * @brief 환경설정 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PreferencesModel
    {
    public:
        PreferencesModel();

        /**
         * @brief 검색 글 @p pFilterText 에 맞는 섹션 목록(등록 순서)을 돌려줍니다. 검색 글 · 섹션 등록 세대 · 타입 레지스트리 세대가 바뀐 때만 다시 짓습니다.
         * @details 고른 섹션이 목록에 없으면(걸러졌거나 모듈과 함께 내려갔다) 첫 섹션을 고른다 — 목록이 비면 고른 것이 없다. 지은 목록이 그리는 동안 바뀌지 않게
         *          고름은 여기서만 고친다. 보인 섹션 수(감시 값)도 함께 남긴다.
         */
        const vector<const EditorSettingsRegistration*>& refreshVisibleSections( const utf8* pFilterText );
        /** @brief 고른 섹션입니다. 없으면 nullptr 입니다. */
        const EditorSettingsRegistration* getSelected() const { return _pSelected; }
        /** @brief 목록에서 @p registration 을 고릅니다. */
        void selectSection( const EditorSettingsRegistration& registration ) { _pSelected = &registration; }
        /** @brief 지난 `refreshVisibleSections` 가 보인 섹션 수입니다. */
        uint32 getVisibleSectionCount() const { return static_cast<uint32>( _listVisibleSection.size() ); }

        /** @brief "Modified only" 체크칸이 바로 고치는 값입니다. */
        bool& editModifiedOnly() { return _bModifiedOnly; }
        /** @brief "Modified only" 면 고른 섹션에서 기본값과 같은 프로퍼티 이름을 @p outListHidden 에 채웁니다(그리드가 숨긴다). */
        void collectHiddenProperties( vector<hashed_string>& outListHidden ) const;

        /** @brief 그리드가 고른 섹션을 고쳤다 — 섹션의 변경 훅을 부르고 저장을 기다리게 합니다. */
        void noteEdited();
        /** @brief 고른 섹션을 기본값으로 되돌리고(변경 훅은 되돌리기가 부른다) 저장을 기다리게 합니다. */
        void resetSelectedSection();
        /** @brief 기다리는 저장이 있고 첫 편집 뒤 `kSaveDelayNanoseconds` 가 지났거나 @p bForce 면 파일에 씁니다. */
        void savePendingChanges( bool bForce );

        /** @brief 첫 편집 뒤 저장까지 기다리는 시간입니다. 끌기 한 번에 파일을 수십 번 쓰지 않게 묶는다. */
        static constexpr int64 kSaveDelayNanoseconds = constant::kNanosecondsPerSecond / 2;

    private:
        /** @brief 저장을 기다리지 않고 있으면 지금을 첫 편집 시각으로 둡니다. */
        void requestSave();

    private:
        vector<const EditorSettingsRegistration*> _listVisibleSection;    ///< 검색 글에 맞는 섹션(등록 순서)
        string                                    _sectionFilterText;     ///< 목록을 지은 때의 검색 글
        uint32                                    _sectionListGeneration; ///< 목록을 지은 때의 섹션 등록 세대
        uint32                                    _sectionTypeGeneration; ///< 목록을 지은 때의 타입 레지스트리 세대
        bool                                      _bSectionListBuilt;     ///< 한 번이라도 지었는지(세대가 0 부터라 첫 프레임을 세대로 가를 수 없다)
        bool                                      _bModifiedOnly;
        const EditorSettingsRegistration*         _pSelected;              ///< 늘 `_listVisibleSection` 안의 항목이거나 nullptr 다(`refreshVisibleSections` 가 맞춘다)
        int64                                     _pendingSaveNanoseconds; ///< 저장을 기다리는 첫 편집 시각(0 이면 기다리지 않는다)
    };
} // namespace sw::editor
