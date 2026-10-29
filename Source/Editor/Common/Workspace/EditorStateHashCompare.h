/**
 * @file EditorStateHashCompare.h
 * @brief Multiplayer 창 Sync 탭(서버 디버깅 S7)의 틱별 상태 해시 비교입니다 — 두 이력을 모아 처음 어긋난 틱을 찾습니다. ImGui 에 의존하지 않습니다.
 * @details 이력의 원천은 셋이다. 디버그 채널로 붙은 서버의 `state.hashes`(세션이 띄운 전용 서버는 `-gv_stateHashHistory` 를 늘 켠다), 저장한 이력 파일,
 *          그리고 같은 줄 형식의 글(`StateHashHistory::writeReport` — `<tick> <16자리 16진 해시>`)이다. 파일은 그 글을 그대로 담으므로 운영 서버가 남긴 이력과도 비교한다.
 *          해시는 씬 트랜스폼(이름 · 활성 · 월드 위치)만 접는다(`SceneStateHash`). 처음 어긋난 틱을 찾는 규칙은 `StateHashHistory::findFirstDivergence` 하나다.
 *          클라이언트(App)는 서버 틱 번호를 모르므로 아직 원천이 아니다 — 같은 입력의 두 서버, 또는 서버와 저장한 이력을 비교한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/NetTypes.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Scene/SceneStateHash.h"

namespace sw::editor
{
    /** @brief 비교할 이력 하나입니다. */
    struct EditorStateHashSource
    {
        string                  _label;      ///< 어디서 읽었는가(주소 · 파일 경로) — 탭에 보인다
        vector<StateHashSample> _listSample; ///< 틱 오름차순
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 두 이력을 비교한 결과입니다. */
    struct EditorStateHashComparison
    {
        uint64 _firstDivergentTick{ 0 }; ///< `_bDiverged` 일 때 처음 어긋난 틱
        uint32 _commonTickCount{ 0 };    ///< 두 이력에 모두 있는 틱 수 — 0 이면 비교할 것이 없었다
        bool   _bDiverged{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorStateHashCompare
     * @brief 이력 슬롯 두 개(A · B)과 그 비교입니다. 게임(UI) 스레드에서만 부릅니다. 채널 읽기는 답이 올 때까지 부른 스레드를 붙잡습니다.
     */
    class SW_EDITOR_API EditorStateHashCompare
    {
    public:
        /** @brief 이력 슬롯 수입니다(A · B). */
        static constexpr int32 kSourceCount = 2;
        /** @brief 채널에서 한 번에 묻는 틱 수의 상한입니다 — 서버 이력 용량보다 크면 가진 만큼만 온다. */
        static constexpr uint32 kMaxRequestedTicks = 65536;
        /** @brief 채널 연결 · 답을 기다리는 시한(밀리초)입니다. 이력이 길면 답이 크다. */
        static constexpr int32 kChannelTimeoutMilliseconds = 5000;

        EditorStateHashCompare();

        /**
         * @brief 디버그 채널로 붙은 서버의 이력 전부(`state.hashes 0 <상한>`)를 슬롯 @p sourceIndex 에 담습니다. 실패하면 false 이고 슬롯은 그대로입니다.
         * @details 디버그 채널은 개발 명령과 함께만 있다(배포 구성에는 없다 — `DebugChannel.h`).
         */
        [[nodiscard]] bool loadFromChannel( int32 sourceIndex, const NetAddress& remote, string_view token );
        /**
         * @brief 두 칸이 채널에서 읽은 서버의 현재 오브젝트별 해시(`state.objects`)를 다시 받아 견주고, 다른 오브젝트 이름을 `getDifferingObjects` 에 채웁니다.
         * @details 어긋난 틱 근처에서 두 서버를 `pause` 한 채 부른다(이력에 오브젝트별 해시를 쌓지 않는 까닭 — 틱 × 오브젝트가 크다). 채널에서 읽은 칸이 아니거나 한쪽 답이 오브젝트 목록이 아니면 false 이고 이유는 `getLastError` 다.
         */
        [[nodiscard]] bool compareObjectsFromChannels();
        /** @brief 칸 @p sourceIndex 를 읽은 디버그 채널의 서버에 `state.objects` 를 다시 물어 틱과 오브젝트별 해시를 받습니다. 채널에서 읽은 칸이 아니거나 답이 목록이 아니면 false 이고 이유는 `getLastError` 다. */
        [[nodiscard]] bool readObjectsFromChannel( int32 sourceIndex, uint64& outTick, vector<SceneObjectHash>& outListObject );
        /**
         * @brief 칸 @p sourceIndex 가 세션 클라이언트 프로세스 @p processIndex 에서 읽은 것임을 적습니다(`loadFromReport` 뒤에 부른다 — 칸이 다시 채워지면 지워진다).
         * @details 클라이언트는 디버그 채널이 없어 오브젝트 해시를 표준 입력(`state.objectline`)으로 받는다. 누가 어느 프로세스에 물을지는 부르는 쪽(탭)이 이 번호로 안다.
         */
        void setClientOrigin( int32 sourceIndex, int32 processIndex );
        /** @brief 칸이 세션 클라이언트에서 읽은 것이면 그 프로세스 번호, 아니면 -1 입니다. */
        int32 getClientOrigin( int32 sourceIndex ) const;
        /** @brief 칸이 디버그 채널에서 읽은 것이면 true 입니다. */
        bool hasChannelOrigin( int32 sourceIndex ) const;
        /**
         * @brief 틱을 단 오브젝트 해시 두 목록을 견줘 다른 오브젝트 이름을 채웁니다. 두 틱이 다르면 거절합니다(다른 시각의 해시를 견주지 않는다).
         * @details `compareObjectReports` 와 탭의 클라이언트 칸(표준 입력으로 받은 목록)이 같이 쓰는 하나의 규칙이다.
         */
        [[nodiscard]] bool compareObjects( uint64 leftTick, const vector<SceneObjectHash>& listLeft, uint64 rightTick, const vector<SceneObjectHash>& listRight );
        /**
         * @brief 두 오브젝트 목록 글(`state.objects` 답)을 견줘 다른 오브젝트 이름을 채웁니다.
         * @details 답마다 첫 줄에 서버 틱이 있고, 두 틱이 다르면 거절합니다(다른 시각의 해시를 견주지 않는다). 형식이 다른 글이어도 false 이고 결과는 그대로입니다.
         */
        [[nodiscard]] bool compareObjectReports( string_view leftText, string_view rightText );
        /** @brief 마지막으로 견준 오브젝트 해시의 틱입니다(두 서버가 같은 틱에서 답했을 때만 견주므로 하나다). */
        uint64 getObjectTick() const { return _objectTick; }
        /** @brief 마지막 `compareObjects*` 가 찾은 다른 오브젝트 이름(이름 순, 중복 없음)입니다. */
        const vector<string>& getDifferingObjects() const { return _listDifferingObject; }
        /** @brief 마지막 `compareObjects*` 가 끝까지 갔으면 true 입니다(false 면 `getDifferingObjects` 가 비어 있어도 "같다" 가 아니다). */
        bool hasObjectComparison() const { return _bObjectComparison; }
        /**
         * @brief 이력을 어느 줄에서 읽을지 정합니다(`StateHashHistory::kSceneChannelName` 이 씬 줄, 아니면 키트가 더한 줄 — 예 `destruction` · `rollback`).
         * @details 채널로 읽는 서버(`loadFromChannel`)와 탭이 클라이언트에 묻는 `state.hashline` 이 같은 이름을 쓴다. 이름 형식이 틀리면 false 이고 그대로 둔다.
         *          줄을 바꾸면 이미 담은 칸은 다른 줄의 값이라 비교에서 빼도록 비교 결과를 낡은 것으로 표시한다(칸은 지우지 않는다 — 다시 읽으면 바뀐다).
         */
        [[nodiscard]] bool setHashChannel( string_view channelName );
        const string&      getHashChannel() const { return _hashChannel; }
        /** @brief 이력 파일을 칸에 담습니다. 형식이 다른 줄이 있으면 false 입니다. */
        [[nodiscard]] bool loadFromFile( int32 sourceIndex, string_view path );
        /** @brief 이력 글을 칸에 담습니다(`writeReport` 형식). 형식이 다르거나 비어 있으면 false 입니다. */
        [[nodiscard]] bool loadFromReport( int32 sourceIndex, string_view label, string_view reportText );
        /** @brief 칸의 이력을 파일로 씁니다(같은 형식 — `loadFromFile` 이 되읽는다). 비어 있으면 false 입니다. */
        [[nodiscard]] bool saveToFile( int32 sourceIndex, string_view path ) const;
        /** @brief 칸을 비웁니다. */
        void clearSource( int32 sourceIndex );

        /** @brief 두 칸을 비교합니다. 한 칸이라도 비어 있으면 false 이고 결과는 그대로입니다. */
        [[nodiscard]] bool compare();

        const EditorStateHashSource&     getSource( int32 sourceIndex ) const;
        const EditorStateHashComparison& getComparison() const { return _comparison; }
        /** @brief 마지막으로 비교한 뒤 칸이 바뀌지 않았으면 true 입니다(탭이 낡은 결과를 가리는 데 쓴다). */
        bool          isComparisonCurrent() const { return _bComparisonCurrent; }
        const string& getLastError() const { return _lastError; }

        /** @brief 틱 순으로 정렬된 두 목록에서 둘 다 있는 틱 수를 셉니다. */
        static uint32 countCommonTicks( const vector<StateHashSample>& listLeft, const vector<StateHashSample>& listRight );

    private:
        [[nodiscard]] bool isSourceIndexValid( int32 sourceIndex );

        /** @brief 칸을 채널에서 읽었을 때의 주소와 토큰입니다(`compareObjectsFromChannels` 가 같은 서버에 다시 묻는다). */
        struct ChannelOrigin
        {
            NetAddress _remote{};
            string     _token{};
            bool       _bValid{ false };
        };

        EditorStateHashSource     _arrSource[kSourceCount];
        ChannelOrigin             _arrChannel[kSourceCount];
        int32                     _arrClientProcess[kSourceCount];
        EditorStateHashComparison _comparison;
        vector<string>            _listDifferingObject;
        string                    _lastError;
        string                    _hashChannel; ///< 이력을 읽을 줄(`setHashChannel`)
        uint64                    _objectTick;
        bool                      _bComparisonCurrent;
        bool                      _bObjectComparison;
    };
} // namespace sw::editor
