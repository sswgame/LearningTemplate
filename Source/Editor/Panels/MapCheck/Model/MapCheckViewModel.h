/**
 * @file MapCheckViewModel.h
 * @brief 맵 검사 창의 ViewModel — 활성 씬의 검증 결과 표(`SceneValidationIssues`)에서 줄을 모으고(`gather`), 고르기 · 초점 · 고침 · 다시 검사 명령과 상태줄 수를 맡습니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 줄은 결과 번호(활성 씬 세대 + 표의 결과 번호)나 거르기가 바뀔 때만 다시 만들고, 줄마다의 "활성 씬에 있다 · 골랐다" 는 그리기마다 본다.
 *          고침은 문서 데이터라 되돌리기 한 단계를 남긴다(`MapCheckFixCommands`). 고르기 · 초점 · 거르기는 보기만 바꾼다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/MapCheck/Model/MapCheckCounts.h"

namespace sw::editor
{
    /** @brief 결과 한 줄입니다(값만). */
    struct MapCheckRow
    {
        ValidationIssue _issue{};
        uint8           _bInActiveScene{ SW_FALSE }; ///< 출처 오브젝트가 활성 씬에 있다(지운 오브젝트 · 다른 씬의 결과면 거짓)
        uint8           _bSelected{ SW_FALSE };      ///< 출처 오브젝트가 메인 씬 선택의 첫 오브젝트다
        uint8           _bFixable{ SW_FALSE };       ///< 안전한 고침이 있다(`MapCheckFixCommands::canFix`)
    };

    /** @brief 맵 검사 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MapCheckViewState
    {
        vector<MapCheckRow> _listRow{};
        MapCheckCounts      _counts{};             ///< 거르기 전 무게별 수
        uint32              _fixableRowCount{ 0 }; ///< 안전한 고침이 있는 줄 수(Fix All 단추의 수)
        uint32              _rowsGeneration{ 0 };  ///< 줄을 채운 ViewModel 세대 — 같으면 줄을 다시 복사하지 않는다
    };

    /**
     * @class MapCheckViewModel
     * @brief 맵 검사 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class MapCheckViewModel
    {
    public:
        MapCheckViewModel();

        /** @brief 창이 그릴 값을 모읍니다. 결과 번호나 거르기가 바뀌었으면 줄을 다시 만듭니다. */
        void gather( MapCheckViewState& outState );

        /** @brief 검색어를 바꿉니다(다음 `gather` 가 줄을 다시 만든다). 보기만 바꾼다. */
        void setSearch( string_view search );
        /** @brief 오류 줄을 보일지 정합니다. 보기만 바꾼다. */
        void setShowErrors( bool bShow );
        /** @brief 경고 줄을 보일지 정합니다. 보기만 바꾼다. */
        void setShowWarnings( bool bShow );
        bool isShowingErrors() const { return _bShowErrors == SW_TRUE; }
        bool isShowingWarnings() const { return _bShowWarnings == SW_TRUE; }

        /** @brief 지난 `gather` 의 줄 가운데 고칠 수 있는 것을 모두 고칩니다(되돌리기 한 단계 "Map Check: Fix All"). 고친 수입니다. */
        uint32 applyAllFixes();
        /** @brief 결과 @p issue 하나를 고칩니다(되돌리기 한 단계, 이름은 고침의 설명). 고친 수입니다. */
        static uint32 applyFix( const ValidationIssue& issue );
        /** @brief 활성 씬의 오브젝트 @p objectID 를 메인 씬 선택으로 고릅니다. 보기만 바꾼다. 활성 씬에 없으면 false 입니다. */
        static bool selectObject( uint64 objectID );
        /** @brief 오브젝트 @p objectID 를 씬 뷰가 담게 카메라를 옮깁니다(줄 더블클릭). 씬 뷰나 오브젝트 · 씬 컴포넌트가 없으면 아무것도 하지 않는다. */
        static void focusObject( uint64 objectID );
        /**
         * @brief 활성 씬의 모든 오브젝트를 다시 검증합니다(Check Map 단추). 결과는 그 씬의 표에 남는다.
         * @return 검증한 오브젝트 수
         */
        static uint32 validateActiveScene();

        /**
         * @brief 상태줄의 맵 검사 수를 돌려줍니다. 결과 번호가 바뀌었을 때만 다시 센다.
         * @details 활성 씬이 바뀐 첫 프레임에 오류가 있으면 토스트를 한 번 띄운다(언리얼은 맵을 열 때 Map Check 를 띄운다). 패널이 닫혀도 상태줄은 그려지므로
         *          기억하는 값은 에디터 범위의 상태 저장소에 둔다.
         */
        static MapCheckCounts updateStatusCounts();
        /** @brief 창 @p panelID 열기를 요청합니다(상태줄 단추). */
        static void requestOpenWindow( string_view panelID );
        /** @brief 활성 씬 표의 결과 수(거르기 전)입니다. 씬이 없으면 false 입니다(감시 값 `Editor.MapCheckIssueCount`). */
        [[nodiscard]] static bool readActiveIssueCount( uint32& outCount );

    private:
        /** @brief 결과 번호나 거르기가 바뀌었으면 줄 캐시를 다시 만들고 세대를 올립니다. */
        void syncRows();

    private:
        vector<ValidationIssue> _listIssue; ///< 거르고 정렬한 결과(줄 캐시)
        MapCheckCounts          _counts;
        string                  _search;
        string                  _builtSearch;      ///< 줄을 만들 때의 검색어
        uint64                  _builtRevisionKey; ///< 줄을 만들 때의 결과 열쇠(활성 씬 세대 + 표의 결과 번호)
        uint32                  _rowsGeneration;   ///< 줄 캐시를 다시 만들 때마다 오른다
        uint8                   _bShowErrors   : 1;
        uint8                   _bShowWarnings : 1;
        uint8                   _bRowsDirty    : 1;
        [[maybe_unused]] uint8  _reservedFlags : 5;
    };
} // namespace sw::editor
