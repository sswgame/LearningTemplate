/**
 * @file EditorRemoteInspector.h
 * @brief Multiplayer 창 Inspector 탭(서버 디버깅 S3)의 원격 검사기입니다 — 디버그 채널로 서버에 붙어 하이어라키 · 컴포넌트 값을 읽고 개발 명령을 보냅니다. ImGui 에 의존하지 않습니다.
 * @details 채널(`DebugChannelClient`)은 같은 PC 의 루프백 TCP 이고 요청마다 답을 기다리는 동기 방식이다. 서버가 `pause` 로 멈춰 있어도 채널은 답하므로
 *          중단점처럼 멈춘 상태의 값을 읽을 수 있다. 쓰는 명령(`teleport` 등)은 게임이 정한 개발 명령만 지나간다(결정 기록 5-10).
 *          - 붙을 때 `help` 를 한 번 물어 입력줄 자동완성의 명령 이름(`getCommandNames`)을 채운다. 답의 둘째 줄부터 `  <사용법> - <설명>` 이고 사용법의 첫 낱말이 이름이다.
 *          - 하이어라키는 `scene.tree` 의 답(`<id> <이름> parent=… active=… components=…`)을 줄마다 `EditorRemoteObjectRow` 로 나눈다.
 *          - 값은 `object.dump <이름>` 의 답을 그대로 보인다(`Type.property = value`).
 *          언리얼 PIE 의 서버 월드 아웃라이너 · 유니티 Multiplayer Play Mode 의 가상 플레이어 인스펙터 자리이다. 에디터가 띄운 세션이 아니어도 주소 · 포트 · 토큰으로 붙는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class DebugChannelClient;
} // namespace sw

namespace sw::editor
{
    /** @brief `scene.tree` 답의 오브젝트 한 줄입니다. */
    struct EditorRemoteObjectRow
    {
        string _name;          ///< 이름(`<unnamed>` 이면 `object.dump` 로 읽을 수 없다)
        string _parentName;    ///< 부모 이름, 없으면 `-`
        string _components;    ///< 쉼표로 이은 컴포넌트 형식 이름
        uint64 _objectID{ 0 }; ///< 서버 프로세스 안의 오브젝트 ID
        bool   _bActive{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRemoteInspector
     * @brief 원격 서버 하나에 붙은 검사기입니다. 게임(UI) 스레드에서만 부릅니다. 요청은 답이 올 때까지(시한 안) 부른 스레드를 붙잡습니다.
     */
    class SW_EDITOR_API EditorRemoteInspector
    {
    public:
        /** @brief 연결이 열릴 때까지 기다리는 시한(밀리초)입니다. 루프백이라 짧다. */
        static constexpr int32 kConnectTimeoutMilliseconds = 1000;
        /** @brief 요청 하나의 답을 기다리는 시한(밀리초)입니다. 서버 틱이 느려도 채널은 루프마다 답한다. */
        static constexpr int32 kRequestTimeoutMilliseconds = 2000;
        /** @brief 입력줄 결과로 보관하는 줄 수입니다. 넘으면 오래된 줄부터 버립니다. */
        static constexpr size_t kMaxConsoleLine = 400;
        /** @brief 이름 없는 오브젝트를 `scene.tree` 가 적는 글입니다. */
        static constexpr const utf8* kUnnamedObject = "<unnamed>";

        EditorRemoteInspector();
        ~EditorRemoteInspector();

        EditorRemoteInspector( const EditorRemoteInspector& )            = delete;
        EditorRemoteInspector& operator=( const EditorRemoteInspector& ) = delete;

        /**
         * @brief 채널에 붙고 `help` 로 명령 이름을 채웁니다. 토큰이 틀리거나 서버가 없으면 false 이고 이유는 `getLastError` 입니다.
         * @details 이미 붙어 있으면 끊고 다시 붙는다. 하이어라키 · 덤프 · 입력줄 결과는 비운다(다른 서버의 값이다).
         */
        [[nodiscard]] bool connect( const NetAddress& remote, string_view token );
        void               disconnect();
        bool               isConnected() const;
        /** @brief 지금(또는 마지막으로) 붙은 주소입니다. */
        const NetAddress& getRemoteAddress() const { return _remoteAddress; }

        /** @brief `scene.tree` 를 물어 하이어라키를 새로 채웁니다. 실패하면 false 이고 표는 그대로 둡니다. */
        [[nodiscard]] bool refreshTree();
        /** @brief `object.dump <이름>` 을 물어 덤프 글을 채우고 그 이름을 고른 오브젝트로 둡니다. 실패하면 false 이고 덤프 글은 오류 글입니다. */
        [[nodiscard]] bool dumpObject( string_view name );
        /** @brief 입력줄 한 줄을 채널로 보내고 `> <줄>` 과 답을 결과 줄에 더합니다. 서버가 오류로 답하거나 끊기면 false 입니다. */
        [[nodiscard]] bool execute( string_view line );

        /** @brief 명령 이름 가운데 @p prefix 로 시작하는 것(대소문자 무시)을 @p outListName 에 담습니다. */
        void collectCompletions( string_view prefix, vector<string>& outListName ) const;

        const vector<EditorRemoteObjectRow>& getObjectRows() const { return _listObjectRow; }
        const vector<string>&                getCommandNames() const { return _listCommandName; }
        const vector<string>&                getConsoleLines() const { return _listConsoleLine; }
        const string&                        getSelectedName() const { return _selectedName; }
        const string&                        getDumpText() const { return _dumpText; }
        const string&                        getLastError() const { return _lastError; }

        /** @brief 채널 `help` 답에서 명령 이름(사용법의 첫 낱말)을 뽑아 이름 순으로 담습니다. 서버 루프 명령(`pause` · `resume` · `step`)과 `get` · `set` · `help` 도 넣습니다. */
        static void parseCommandNames( string_view helpBody, vector<string>& outListName );
        /** @brief `scene.tree` 답을 줄마다 나눕니다. 첫 줄(`objects N`)과 한도 줄(`... (limit reached)`)은 건너뜁니다. 형식이 틀린 줄은 false 를 돌려줍니다. */
        [[nodiscard]] static bool parseObjectRows( string_view treeBody, vector<EditorRemoteObjectRow>& outListRow );

    private:
        /** @brief 요청 하나를 보냅니다. 끊겼으면 연결 상태를 내리고 오류 글을 남깁니다. */
        [[nodiscard]] bool request( string_view commandLine, string& outBody );
        void               appendConsoleLine( string line );

        unique_ptr<DebugChannelClient> _client;
        vector<EditorRemoteObjectRow>  _listObjectRow;
        vector<string>                 _listCommandName;
        vector<string>                 _listConsoleLine;
        string                         _selectedName;
        string                         _dumpText;
        string                         _lastError;
        NetAddress                     _remoteAddress;
    };
} // namespace sw::editor
