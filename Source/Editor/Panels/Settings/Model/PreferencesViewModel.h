/**
 * @file PreferencesViewModel.h
 * @brief 환경설정 창의 모델 — 검색 글에 맞는 섹션 목록(검색 글 · 섹션 등록 세대 · 타입 레지스트리 세대가 바뀐 프레임에만 다시 짓기), 고른 섹션, "바뀐 것만" 숨길 프로퍼티,
 *        섹션 되돌리기, 편집 되돌리기(Ctrl+Z), 고친 뒤 잠시 기다렸다 저장하기. ImGui 를 모릅니다.
 * @details 패널(`PreferencesPanel`)은 섹션 목록 · 프로퍼티 그리드 · 검색 칸(`EditorPropertyGrid`)을 그리기만 한다 — 전역 변수 창(`GlobalVariablesViewModel`)과 같은 선이다
 *          (언리얼 Editor Preferences 의 섹션 목록 · 설정 객체와 디테일 뷰가 나뉜 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class EditorStateTextHistory;
    struct EditorSettingsRegistration;

    /**
     * @class PreferencesViewModel
     * @brief 환경설정 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PreferencesViewModel
    {
    public:
        PreferencesViewModel();
        ~PreferencesViewModel();
        PreferencesViewModel( const PreferencesViewModel& )            = delete;
        PreferencesViewModel& operator=( const PreferencesViewModel& ) = delete;

        /**
         * @brief 검색 글 @p pFilterText 에 맞는 섹션 목록(등록 순서)을 돌려줍니다. 검색 글 · 섹션 등록 세대 · 타입 레지스트리 세대가 바뀐 때만 다시 짓습니다.
         * @details 고른 섹션이 목록에 없으면(걸러졌거나 모듈과 함께 내려갔다) 첫 섹션을 고른다 — 목록이 비면 고른 것이 없다. 지은 목록이 그리는 동안 바뀌지 않게
         *          고름은 여기서만 고친다. 보인 섹션 수(감시 값)도 함께 남긴다.
         */
        const vector<const EditorSettingsRegistration*>& refreshVisibleSections( const utf8* pFilterText );
        /** @brief 고른 섹션입니다. 없으면 nullptr 입니다. */
        const EditorSettingsRegistration* getSelected() const { return _pSelected; }
        /** @brief 목록에서 @p registration 을 고릅니다. */
        void selectSection( const EditorSettingsRegistration& registration );
        /** @brief 지난 `refreshVisibleSections` 가 보인 섹션 수입니다. */
        uint32 getVisibleSectionCount() const { return static_cast<uint32>( _listVisibleSection.size() ); }

        /** @brief "Modified only" 체크칸이 바로 고치는 값입니다. */
        bool& editModifiedOnly() { return _bModifiedOnly; }
        /** @brief "Modified only" 면 고른 섹션에서 기본값과 같은 프로퍼티 이름을 @p outListHidden 에 채웁니다(그리드가 숨긴다). */
        void collectHiddenProperties( vector<hashed_string>& outListHidden ) const;

        /**
         * @brief 그리드가 고른 섹션을 고쳤다 — 섹션의 변경 훅을 부르고 되돌리기 이름 @p label 로 기록하고 저장을 기다리게 합니다.
         * @param bInteracting 위젯을 끄는 중이면 true 입니다 — 놓을 때(`flushEdit`) 한 단계다.
         */
        void recordEdited( string_view label, bool bInteracting );
        /** @brief 쥔 편집이 있고 위젯이 풀렸으면 되돌리기 한 단계로 올립니다. 패널이 그리기마다 부릅니다. */
        void flushEdit( bool bInteracting );
        /** @brief 고른 섹션을 기본값으로 되돌리고(변경 훅은 되돌리기가 부른다) 한 단계로 기록하고 저장을 기다리게 합니다. */
        void resetSelectedSection();
        /** @brief Ctrl+Z 가 갈 스택입니다. 되돌리면 변경 훅을 부르고 저장을 기다리게 한다(저장 단추가 없는 창이라 되돌림도 저장된다). */
        CommandStack& getCommandStack();
        /** @brief 기다리는 저장이 있고 첫 편집 뒤 `kSaveDelayNanoseconds` 가 지났거나 @p bForce 면 파일에 씁니다. */
        void savePendingChanges( bool bForce );
        /** @brief 파일에 쓰기를 기다리는 편집이 있으면 true 입니다. */
        bool hasPendingSave() const { return _pendingSaveNanoseconds != 0; }

        /** @brief 첫 편집 뒤 저장까지 기다리는 시간입니다. 끌기 한 번에 파일을 수십 번 쓰지 않게 묶는다. */
        static constexpr int64 kSaveDelayNanoseconds = constant::time::kNanosecondsPerSecond / 2;

    private:
        /** @brief 저장을 기다리지 않고 있으면 지금을 첫 편집 시각으로 둡니다. */
        void requestSave();
        /** @brief 고른 섹션을 되돌리기 기록의 대상으로 둡니다. 섹션은 id 로 찾는다 — 모듈이 내려가 없어진 섹션의 단계는 아무것도 하지 않는다. */
        void syncHistorySource();
        /** @brief 섹션 @p sectionID 의 값을 글 @p text 로 두고 변경 훅을 부르고 저장을 기다리게 합니다(되돌리기 단계). */
        void restoreSection( const string& sectionID, string_view text );

    private:
        unique_ptr<EditorStateTextHistory>        _pHistory;               ///< 편집 되돌리기
        vector<const EditorSettingsRegistration*> _listVisibleSection;     ///< 검색 글에 맞는 섹션(등록 순서)
        string                                    _sectionFilterText;      ///< 목록을 지은 때의 검색 글
        uint32                                    _sectionListGeneration;  ///< 목록을 지은 때의 섹션 등록 세대
        uint32                                    _sectionTypeGeneration;  ///< 목록을 지은 때의 타입 레지스트리 세대
        uint8                                     _bSectionListBuilt : 1;  ///< 한 번이라도 지었는지(세대가 0 부터라 첫 프레임을 세대로 가를 수 없다)
        bool                                      _bModifiedOnly;          ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        const EditorSettingsRegistration*         _pSelected;              ///< 늘 `_listVisibleSection` 안의 항목이거나 nullptr 다(`refreshVisibleSections` 가 맞춘다)
        int64                                     _pendingSaveNanoseconds; ///< 저장을 기다리는 첫 편집 시각(0 이면 기다리지 않는다)
    };
} // namespace sw::editor
