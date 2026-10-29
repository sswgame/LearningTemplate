/**
 * @file GlobalVariablesModel.h
 * @brief 전역 변수 창의 모델 — 보일 변수 목록(테스트용 제외 · 검색 · 모듈 · 이름 순, 등록 세대나 검색 글이 바뀐 프레임에만 다시 짓기), 핀, 프리셋 목록 훑기 · 이름 붙여 저장 · 불러오기. ImGui 를 모릅니다.
 * @details 패널(`GlobalVariablesPanel`)은 툴바 · 표 · 값 위젯을 그리고 검색 글 · 프리셋 이름 입력란 · 모듈별 묶기 토글만 든다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 콘솔 변수 편집기의 변수 목록과 Slate 행이 나뉜 것과 같다). 값 자체는 `GlobalVariableManager` 가 들고 창은 바로 고친다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"

namespace sw
{
    struct GlobalVariableInfo;

    class GlobalVariableManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class GlobalVariablesModel
     * @brief 전역 변수 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class GlobalVariablesModel
    {
    public:
        GlobalVariablesModel();

        /**
         * @brief 등록 세대나 검색 글 @p filterText 가 지은 때와 다르면 보일 변수 목록을 다시 짓습니다. 패널이 그리기마다 부릅니다.
         * @return 보일 변수(모듈 · 이름 순)입니다.
         */
        const vector<GlobalVariableInfo*>& refreshVariableList( const GlobalVariableManager& gvm, string_view filterText );
        /** @brief 테스트용을 뺀 변수 수(필터 전)입니다. */
        uint32 getTotalVariableCount() const { return _totalVariableCount; }
        /** @brief 보일 변수 목록을 다시 지은 횟수입니다(감시 값 `Editor.GlobalVariablesRebuildCount`). */
        uint32 getListRebuildCount() const { return _listRebuildCount; }

        const unordered_set<string>& getPinnedVariables() const { return _uniquePinnedVar; }
        bool                         isPinned( const string& name ) const { return _uniquePinnedVar.find( name ) != _uniquePinnedVar.end(); }
        /**
         * @brief 변수 @p name 의 핀을 꽂거나 뽑으라고 요청합니다. 다음 `refreshVariableList` 가 적용한다.
         * @details 핀 섹션은 핀 집합을 돌며 그리므로, 그리는 도중에 집합을 바꾸면 도는 반복자가 무효가 된다 — 그래서 미뤄 둔다.
         */
        void requestTogglePin( const string& name ) { _pendingPinToggle = name; }

        /**
         * @brief 지금 값을 프리셋 폴더에 @p presetName 이름으로 저장하고 목록을 다시 훑게 합니다. 이름 붙인 프리셋 저장은 세션 문서의 저장이 아니다(dirty 는 그대로).
         * @return 저장했으면 true 입니다(입력란을 비운다). 이름이 비었거나 쓰지 못하면 false 이고 쓰지 못한 것은 로그에 남는다.
         */
        [[nodiscard]] bool savePreset( string_view presetName );
        /** @brief 프리셋 파일 @p presetFile 의 값을 읽어 지금 값으로 둡니다. 읽지 못하면 값이 그대로이고 로그에 남는다. */
        static void loadPreset( const string& presetFile );
        /**
         * @brief 프리셋 목록 훑기 결과를 받고, 다시 훑어야 하면 훑기를 겁니다. 프리셋 팝업이 열린 동안 그리기마다 부릅니다.
         * @return 훑는 중이면 true 입니다(목록을 보이지 않는다).
         */
        bool                  pollPresetFiles();
        const vector<string>& getPresetFiles() const { return _listPresetFile; }
        /** @brief 프리셋 파일 경로 @p presetFile 의 표시 이름(파일 이름에서 `.gvpreset.xml` 을 뗀 것)입니다. */
        static string makePresetDisplayName( string_view presetFile );

    private:
        unordered_set<string> _uniquePinnedVar;
        string                _pendingPinToggle; ///< 다음 목록 갱신에서 꽂거나 뽑을 변수 이름(비면 없음)
        EditorFileCollectJob  _presetJob;
        vector<string>        _listPresetFile;
        /** @brief 보일 변수입니다. 등록 세대(`GlobalVariableManager::getRegistrationGeneration`)나 검색 글이 바뀐 프레임에만 다시 채운다. */
        vector<GlobalVariableInfo*> _listFilteredVariable;
        string                      _listFilterText;     ///< 목록을 지은 때의 검색 글
        uint64                      _listGeneration;     ///< 목록을 지은 때의 등록 세대(0 이면 아직 짓지 않았다 — 세대는 1 부터)
        uint32                      _totalVariableCount; ///< 테스트용을 뺀 변수 수(필터 전)
        uint32                      _listRebuildCount;
        bool                        _bPresetListDirty;
    };
} // namespace sw::editor
