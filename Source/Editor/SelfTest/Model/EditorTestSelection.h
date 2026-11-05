/**
 * @file EditorTestSelection.h
 * @brief Test Runner 창의 고르기 상태입니다 — 체크 · 검색 · 묶음 나누기 · 실행 패턴 만들기(자체 테스트 · 단위 테스트), 시나리오 목록과 고른 시나리오(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 목록의 묶음 하나입니다(`Suite.Case` 의 `Suite`). */
    struct EditorTestGroup
    {
        string         _name;
        vector<uint32> _listCaseIndex; ///< 이 묶음에서 검색에 맞는 케이스(`EditorTestSelection::getCases` 의 번호, 목록 순서)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorTestSelection
     * @brief 케이스 목록 하나의 체크와 검색입니다. 묶음과 패턴은 바뀔 때만 다시 만들어 그리기가 프레임마다 할당하지 않습니다.
     * @details 체크는 이름으로 들어 목록이 바뀌어도(핫 리로드로 잠시 빠져도) 남습니다. 패턴은 언제나 지금 목록에서 만드므로 없어진 이름은 실행에 들지 않고, 비면 돌릴 것이 없습니다.
     *          검색에 가려진 케이스는 체크되어 있어도 돌지 않습니다 — 보이는 것만 돈다.
     */
    class EditorTestSelection
    {
    public:
        /** @brief 케이스 목록을 바꿉니다. 체크는 이름으로 남습니다. */
        void setCases( vector<string> listCase );
        /** @brief 검색 글을 바꿉니다. 앞뒤 공백은 보지 않고 대소문자를 가리지 않습니다(`EditorListFilter`). 글이 같으면 아무것도 하지 않습니다. */
        void setFilter( string_view filter );
        void setSelected( string_view name, bool bSelected );
        bool isSelected( string_view name ) const { return _uniqueSelected.count( string( name ) ) != 0; }

        const vector<string>& getCases() const { return _listCase; }
        /** @brief 검색에 맞는 케이스의 묶음입니다. 목록에서 떨어져 있는 같은 스위트도 한 묶음입니다(묶음은 처음 나온 순서). */
        const vector<EditorTestGroup>& getGroups() const { return _listGroup; }
        /** @brief 체크했고 검색에 맞는 케이스를 쉼표로 이은 실행 패턴입니다. 비면 돌릴 것이 없습니다. */
        const string& getSelectedPattern() const { return _selectedPattern; }
        /** @brief 검색에 맞는 모든 케이스를 쉼표로 이은 실행 패턴입니다. 비면 돌릴 것이 없습니다. */
        const string& getVisiblePattern() const { return _visiblePattern; }

        /** @brief 묶음 아래 줄의 뒷이름입니다(`theme.palette` → `palette`). 점이 없으면 전체입니다. */
        static string_view findLeafName( string_view name );

    private:
        void rebuildVisible();
        void rebuildSelectedPattern();

    private:
        vector<string>          _listCase;
        vector<EditorTestGroup> _listGroup;
        unordered_set<string>   _uniqueSelected;
        string                  _filter;
        string                  _selectedPattern;
        string                  _visiblePattern;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorScenarioList
     * @brief 시나리오 탭의 목록과 고른 시나리오입니다.
     * @details 고른 것을 번호가 아니라 경로로 듭니다 — 번호로 들면 Refresh 로 목록이 바뀐 뒤 다른 시나리오를 가리켜 고르지 않은 것이 돈다.
     */
    class EditorScenarioList
    {
    public:
        /** @brief 파일 목록에서 시나리오(`<영역>/automation/<이름>.scenario.xml`)만 @p root 기준 리소스 id 로 담아 정렬합니다. 고른 것이 사라졌으면 고르지 않은 것이 됩니다. */
        void setFromFiles( const vector<string>& listFilePath, string_view root );
        void choose( string_view path ) { _chosen = string( path ); }

        const vector<string>& getPaths() const { return _listPath; }
        /** @brief 고른 시나리오입니다. 비면 고르지 않았습니다. */
        const string& getChosen() const { return _chosen; }

    private:
        vector<string> _listPath;
        string         _chosen;
    };
} // namespace sw::editor
