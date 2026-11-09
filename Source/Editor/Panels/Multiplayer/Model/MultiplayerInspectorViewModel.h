/**
 * @file MultiplayerInspectorViewModel.h
 * @brief Multiplayer 창 Inspector 탭의 ViewModel — 원격 검사기(`EditorRemoteInspector`)의 연결 · 하이어라키 · 값 · 입력줄 결과를 모으고(`gather`), 붙기 · 끊기 · 새로 읽기 · 고르기 · 입력줄 명령을 받습니다. ImGui 를 모릅니다.
 * @details 검사기는 에디터 범위 상태 저장소가 소유해 창을 닫았다 열어도 연결이 남는다. 요청은 루프백에서 답을 기다리는 동기 방식이라 누를 때만 묻는다(서버 디버깅 S3).
 *          원격 서버를 읽고 명령을 보낼 뿐 씬 문서가 아니라 되돌리기에 남지 않는다. 감시 값 `Editor.NetPlayInspectorObjects` 와 개발 명령 `netplay.inspect` 도 이 .cpp 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 원격 하이어라키의 오브젝트 한 줄입니다(값만). */
    struct MultiplayerInspectorObjectRow
    {
        string _name{};
        string _parentName{};
        string _components{};
        uint64 _objectID{ 0 };
        uint8  _bActive{ SW_FALSE };
        uint8  _bNamed{ SW_FALSE }; ///< 이름이 있어 값을 읽을 수 있다(`<unnamed>` 가 아니다)
    };

    /** @brief Inspector 탭이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MultiplayerInspectorViewState
    {
        vector<MultiplayerInspectorObjectRow> _listObjectRow{};
        vector<string>                        _listConsoleLine{};
        string                                _remoteAddress{}; ///< 붙은(또는 마지막으로 붙은) 주소 글
        string                                _selectedName{};  ///< 값을 읽은 오브젝트. 비어 있으면 고른 것이 없다
        string                                _dumpText{};
        string                                _lastError{};
        uint8                                 _bConnected{ SW_FALSE };
    };

    /**
     * @class MultiplayerInspectorViewModel
     * @brief Inspector 탭의 ViewModel 입니다. 파일 머리말 참고. 실패는 경고 로그로 알리고 마지막 오류는 `_lastError` 로 보입니다.
     */
    class MultiplayerInspectorViewModel
    {
    public:
        void gather( MultiplayerInspectorViewState& outState ) const;

        /** @brief 세션이 띄운 전용 서버의 채널에 붙고 하이어라키를 읽습니다. */
        void connectToSession();
        /** @brief @p address(`127.0.0.1:<포트>` 만)와 @p token 으로 이미 떠 있는 서버에 붙고 하이어라키를 읽습니다. 루프백이 아닌 주소는 붙기 전에 거절합니다. */
        void attachByAddress( string_view address, string_view token );
        void disconnect();
        /** @brief 하이어라키를 새로 읽습니다. */
        void refresh();
        /** @brief 오브젝트 @p name 의 값을 읽습니다. 실패면 값 칸에 오류 글이 보입니다. */
        void selectObject( const string& name );
        /** @brief 입력줄 한 줄을 채널로 보냅니다. 결과(오류 포함)는 입력줄 결과에 남습니다. */
        void execute( string_view line );
        /** @brief 명령 이름 가운데 @p prefix 로 시작하는 것을 @p outListName 에 담습니다(입력줄 Tab 자동완성). */
        void collectCompletions( string_view prefix, vector<string>& outListName ) const;
    };
} // namespace sw::editor
