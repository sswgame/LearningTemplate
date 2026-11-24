/**
 * @file GlobalVariablesViewModel.h
 * @brief 전역 변수 창의 모델 — 보일 변수 목록(테스트용 제외 · 검색 · 모듈 · 이름 순, 등록 세대나 검색 글이 바뀐 프레임에만 다시 짓기), 핀, 프리셋 목록 훑기 · 이름 붙여 저장 · 불러오기, 되돌리기(값 · Reset All · 프리셋 불러오기). ImGui 를 모릅니다.
 * @details 패널(`GlobalVariablesPanel`)은 툴바 · 표 · 값 위젯을 그리고 검색 글 · 프리셋 이름 입력란 · 모듈별 묶기 토글만 든다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 콘솔 변수 편집기의 변수 목록과 Slate 행이 나뉜 것과 같다). 값 자체는 `GlobalVariableManager` 가 들고 창은 바로 고친다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"

namespace sw
{
    class CommandStack;
    struct EnumInfo;
    struct GlobalVariableInfo;

    class GlobalVariableManager;
} // namespace sw

namespace sw::editor
{
    class EditorStateTextHistory;

    /**
     * @class GlobalVariablesViewModel
     * @brief 전역 변수 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class GlobalVariablesViewModel
    {
    public:
        GlobalVariablesViewModel();
        ~GlobalVariablesViewModel();
        GlobalVariablesViewModel( const GlobalVariablesViewModel& )            = delete;
        GlobalVariablesViewModel& operator=( const GlobalVariablesViewModel& ) = delete;

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
         * @return 저장했으면 true 입니다(입력란을 비운다). 이름이 비었거나 경로 글자가 있거나 쓰지 못하면 false 이고 비지 않은 이름의 실패는 로그에 남는다.
         */
        [[nodiscard]] bool savePreset( string_view presetName );
        /** @brief 프리셋 파일 @p presetFile 의 값을 지금 값으로 두고 되돌리기 한 단계로 기록합니다. 읽지 못하면 값이 그대로이고 로그에 남고 false 입니다. */
        [[nodiscard]] bool loadPreset( const AbsolutePath& presetFile );
        /**
         * @brief 프리셋 목록 훑기 결과를 받고, 다시 훑어야 하면 훑기를 겁니다. 프리셋 팝업이 열린 동안 그리기마다 부릅니다.
         * @return 훑는 중이면 true 입니다(목록을 보이지 않는다).
         */
        bool pollPresetFiles();
        /** @brief `pollPresetFiles` 와 같되 훑을 폴더 @p presetFolder 를 받습니다(프리셋 폴더는 에디터 상태 폴더 아래라 테스트는 빈 폴더를 넘긴다). */
        bool pollPresetFilesIn( string_view presetFolder );
        /** @brief 다음 `pollPresetFiles` 가 목록을 다시 훑게 합니다. 지금 훑는 중이면 그 결과를 받은 뒤 한 번 더 훑는다. */
        void                  markPresetListStale() { _bPresetListDirty = SW_TRUE; }
        const vector<string>& getPresetFiles() const { return _listPresetFile; }
        /** @brief 등록된 전역 변수를 모두 기본값으로 되돌리고 되돌리기 한 단계로 기록합니다(Reset All). */
        void resetAllToDefault();
        /** @brief 문서 되돌리기 — 모두 기본값으로 두고 되돌리기 기록을 비웁니다. */
        void revertToDefault();

        /** @brief 변수 @p name 의 값을 쓰기 직전입니다 — 쥔 편집이 없으면 지금 값이 되돌릴 값이 된다. */
        void beginEdit( const string& name );
        /** @brief `beginEdit` 뒤에 값을 썼습니다. @p bInteracting(위젯을 끄는 중)이면 놓을 때(`flushEdit`) 한 단계다. */
        void recordEdited( bool bInteracting );
        /** @brief 쥔 편집이 있고 위젯이 풀렸으면 되돌리기 한 단계로 올립니다. 패널이 그리기마다 부릅니다. */
        void flushEdit( bool bInteracting );
        /** @brief Ctrl+Z 가 갈 스택입니다. */
        CommandStack& getCommandStack();
        /** @brief 지난 호출 뒤에 되돌리기 · 다시 하기가 값을 움직였으면 true 입니다(패널이 세션을 저장 안 됨으로 둔다). */
        [[nodiscard]] bool takeUndoMoved();

        /** @brief 이름 @p name 의 전역 변수입니다. 없으면 nullptr 입니다(고정한 변수 줄 — 값은 창이 바로 고친다, 머리말). */
        [[nodiscard]] static GlobalVariableInfo* findVariable( string_view name );
        /** @brief 에디터 범위의 관리자에서 `refreshVariableList( gvm, filterText )` 를 부릅니다. 관리자는 Model 안에서만 찾는다(MVVM 계약). */
        const vector<GlobalVariableInfo*>& refreshVariableList( string_view filterText );
        /** @brief 지금 값을 세션 문서로 저장합니다(`EditorGlobalVariableCommands::saveSessionPreset`). 못 쓰면 false 이고 로그에 남는다. */
        [[nodiscard]] static bool saveSessionPreset();
        /** @brief `saveSessionPreset` 가 쓸 세션 문서(디스크 경로)를 @p outListPath 에 더합니다(`EditorGlobalVariableCommands::getSessionPresetPath`). */
        static void collectFilesToSave( vector<string>& outListPath );
        /** @brief @p absolutePath 가 세션 문서면 true 입니다. */
        [[nodiscard]] static bool isSessionFile( string_view absolutePath );
        /** @brief 세션 문서 글입니다(자동 저장 사본 — 저장할 글과 같다). */
        [[nodiscard]] static string makeSessionText();
        /** @brief 세션 문서의 사본 글 @p text 를 지금 값으로 두고 되돌리기 단계를 비웁니다(갈아 끼운 값의 단계는 남지 않는다). 읽지 못하면 false 이고 값은 그대로입니다. */
        [[nodiscard]] bool applySessionText( string_view text );
        /** @brief 열거형 전역 변수의 리플렉션 열거 정보입니다. @p enumType 이 비었거나 등록되지 않았으면 nullptr 입니다. */
        static const EnumInfo* findEnumInfo( const string& enumType );
        /** @brief 열거형 @p enumType 의 값 @p value 의 이름입니다. 없으면 nullptr 입니다. */
        static const utf8* findEnumName( const string& enumType, int32 value );
        /** @brief 프리셋 파일 경로 @p presetFile 의 표시 이름(파일 이름에서 `.gvpreset.xml` 을 뗀 것)입니다. */
        static string makePresetDisplayName( string_view presetFile );

    private:
        /** @brief 에디터 범위의 전역 변수 관리자입니다. 없으면 멈춘다. Model 안에서만 쓴다. */
        static GlobalVariableManager& getVariableManager();
        /** @brief 이름 @p listName 의 변수를 되돌리기 대상으로 둡니다. 단계는 이 변수들의 값만 담는다 — 전부를 담으면 그 사이 콘솔이 고친 변수까지 되돌린다. */
        void selectEdit( const vector<string>& listName );

    private:
        unique_ptr<EditorStateTextHistory> _pHistory; ///< 편집 되돌리기
        string                             _editName; ///< 쥔 편집의 변수(비면 쥐지 않았다)

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
        uint8                       _bPresetListDirty : 1;
        uint8                       _bUndoMoved       : 1; ///< 되돌리기가 값을 움직였고 패널이 아직 받지 않았다
    };
} // namespace sw::editor
