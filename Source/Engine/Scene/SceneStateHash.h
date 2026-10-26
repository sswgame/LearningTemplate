/**
 * @file Engine/Scene/SceneStateHash.h
 * @brief 활성 씬의 상태 해시와 틱별 해시 이력입니다 — 두 프로세스의 상태가 처음 어긋난 틱을 찾는 데 씁니다(서버 디버깅 S7).
 * @details 해시는 이름이 있는 오브젝트마다 (이름, 활성, 컴포넌트마다 `Component::foldStateHash` 가 접은 상태)를 오브젝트 해시로 만들고 (이름, 해시) 정수 순으로 접습니다.
 *          트랜스폼(`SceneComponent`) · 속도(강체 · 캐릭터 컨트롤러 · 내비 에이전트) · 난수 상태는 그 컴포넌트가 넣는다 — 새 시뮬레이션 상태는 컴포넌트가 재정의해 더한다.
 *          실수는 `StateHashBuilder` 가 -0.0 · NaN 을 정규화해 접는다. 같은 씬을 같은 입력으로 돌린 두 프로세스는 같은 틱에 같은 값을 냅니다.
 *          오브젝트 ID 는 넣지 않습니다(프로세스마다 다를 수 있다). 비용은 프로파일 구간 `GT.StateHash.collect` 와 카운터 `GT.StateHash.objects` 로 잰다.
 *          이력은 엔진 서비스(`StateHashHistory`) 하나이고 게임 스레드만 씁니다. 용량 0(기본)이면 꺼져 있어 해시를 계산하지 않습니다(`-gv_stateHashHistory=N`).
 *          전용 서버는 자기 틱 번호로, 클라이언트(App)는 서버가 알린 틱 번호(`GameNetSession::tryGetServerTick`)로 남겨 두 이력의 틱이 같은 뜻이다.
 *          개발 명령 `state.hash`(지금 값)와 `state.hashes <from> <count>`(이력)가 같은 줄 형식으로 답하고, `parseReport` 가 그 글을 되읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 틱 하나의 해시입니다. */
    struct StateHashSample
    {
        uint64 _tick{ 0 };
        uint64 _hash{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 오브젝트 하나의 해시입니다(이름 + 활성 + 컴포넌트 상태). 어긋난 틱에서 어느 오브젝트가 다른지 짚는 데 씁니다. */
    struct SceneObjectHash
    {
        string _name{};
        uint64 _hash{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @struct SceneStateHash @brief 씬 상태 해시 계산입니다. */
    struct SW_API SceneStateHash
    {
        /** @brief `collectObjects` 의 (이름, 오브젝트 해시)를 그 순서로 접은 해시입니다. 오브젝트가 없으면 빈 해시 값입니다. */
        static uint64 compute( const GameObjectManager& manager );
        /** @brief 이름 있는 오브젝트마다 해시 하나를 (이름, 해시) 순으로 채웁니다. `compute` 가 이 목록을 접습니다. */
        static void collectObjects( const GameObjectManager& manager, vector<SceneObjectHash>& outListObject );
        /** @brief 한 줄에 하나(`<16자리 16진 해시> <이름>`)로 씁니다. 이름은 줄 끝까지입니다. */
        static void writeObjectReport( const vector<SceneObjectHash>& listObject, string& outText );
        /** @brief `writeObjectReport` 의 글을 되읽습니다. 형식이 다른 줄이 하나라도 있으면 목록을 비우고 false 입니다(다른 글을 오브젝트 목록으로 받지 않는다). */
        [[nodiscard]] static bool parseObjectReport( string_view text, vector<SceneObjectHash>& outListObject );
        /**
         * @brief 첫 줄에 틱(`tick <번호>`)을 단 오브젝트 목록 글을 씁니다. `state.objects` 의 답 형식이다.
         * @details 오브젝트 해시가 이력의 어느 틱에 맞는지 답이 스스로 들고 있게 한다 — 두 서버의 답이 다른 틱이면 받는 쪽이 거절한다.
         */
        static void writeTickedObjectReport( uint64 tick, const vector<SceneObjectHash>& listObject, string& outText );
        /** @brief `writeTickedObjectReport` 의 글을 되읽습니다. 첫 줄이 틱이 아니거나 목록 형식이 다르면 false 이고 목록을 비웁니다. */
        [[nodiscard]] static bool parseTickedObjectReport( string_view text, uint64& outTick, vector<SceneObjectHash>& outListObject );
        /** @brief 한 줄 판의 머리입니다. 표준 입력 명령(`state.objectline`)의 답이 로그 한 줄로 나가는 길에서 에디터가 이 머리를 찾아 거둡니다(Sync 탭의 클라이언트 칸). */
        static constexpr const utf8* kObjectReportLinePrefix = "State object hashes:";
        /**
         * @brief 틱을 단 오브젝트 목록을 한 줄(`kObjectReportLinePrefix <tick> <16자리 16진 해시>:<이름>,…`)로 씁니다.
         * @details 이름의 공백 · 쉼표 · ':' · '%' · 제어 문자는 `%XX`(대문자 16진)로 쓴다 — 로그 꼬리(공백 뒤)와 구분자에 섞이지 않게. 오브젝트가 없으면 목록 자리가 `-` 다.
         *          줄 길이 상한은 `DebugReplyLine::kMaxBytes` 하나다(`StateHashHistory::writeReportLine` 도 같다). 이 줄들은 목록을 줄여 담을 수 없는 형식이라 잘림 표시 대신,
         *          넘으면 쓰는 쪽이 오류로 멈추고(`DebugReplyLine::fits`) 읽는 쪽도 넘는 줄을 거절한다.
         */
        [[nodiscard]] static bool writeObjectReportLine( uint64 tick, const vector<SceneObjectHash>& listObject, string& outLine );
        /** @brief @p line 에서 `kObjectReportLinePrefix` 를 찾아 틱과 목록을 읽습니다. 머리가 없거나 항목 형식이 다르거나 `DebugReplyLine::kMaxBytes` 를 넘으면 false 이고 목록을 비웁니다. */
        [[nodiscard]] static bool tryParseObjectReportLine( string_view line, uint64& outTick, vector<SceneObjectHash>& outListObject );
        /**
         * @brief 두 목록에서 (이름, 해시) 짝이 한쪽에만 있는 오브젝트의 이름을 이름 순으로 중복 없이 채웁니다. 이름이 같은 오브젝트가 여럿이면 개수까지 견줍니다.
         * @return 하나라도 다르면 true
         */
        [[nodiscard]] static bool findDifferingObjects( const vector<SceneObjectHash>& listLeft, const vector<SceneObjectHash>& listRight, vector<string>& outListName );
    };
} // namespace sw

namespace sw
{
    /**
     * @class StateHashHistory
     * @brief 최근 N 틱의 해시 이력(링 버퍼)입니다. 서버가 틱마다 `record` 하고 개발 명령이 읽습니다.
     * @details 엔진 서비스입니다(`engine::findSubsystem<StateHashHistory>()`) — 서비스 범위가 소유한다. 용량은 서비스가 설 때 실행 인자
     *          `-gv_stateHashHistory=N` 으로 정한다(0 이면 끈 채로 둔다. Shipping 에는 인자가 없어 늘 꺼져 있다).
     */
    class SW_API StateHashHistory final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "StateHashHistory";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 담을 틱 수를 정합니다(0 이면 끄고 비웁니다). */
        void   setCapacity( uint32 capacity );
        uint32 getCapacity() const { return _capacity; }
        bool   isRecording() const { return _capacity > 0; }

        /** @brief 틱 하나를 남깁니다. 가득 차면 가장 오래된 것을 덮습니다. 켜져 있지 않으면 아무 일도 없습니다. */
        void record( uint64 tick, uint64 hash );
        /**
         * @brief 활성 씬의 해시를 틱 @p tick 으로 남깁니다. 켜져 있지 않거나, 엔진 서비스 · 활성 씬이 없거나, 가장 최근 샘플이 이미 이 틱이면 아무 일도 없습니다.
         * @details 클라이언트는 프레임마다 부르고 서버 틱이 바뀐 프레임만 남는다(같은 틱을 두 번 남기면 비교가 같은 틱의 두 값을 본다).
         */
        void recordActiveScene( uint64 tick );
        /** @brief 남은 샘플 중 가장 최근 것입니다(없으면 false). */
        [[nodiscard]] bool tryGetLatest( StateHashSample& outSample ) const;
        /** @brief @p fromTick 이후(포함)의 샘플을 틱 순으로 최대 @p count 개 채웁니다. */
        void collect( uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const;

        /** @brief 샘플을 한 줄에 하나(`<tick> <16자리 16진 해시>`)로 씁니다. */
        static void writeReport( const vector<StateHashSample>& listSample, string& outText );
        /** @brief 한 줄 판의 머리입니다. 자식 프로세스의 로그 줄에서 이 머리를 찾아 이력을 거둡니다(에디터 Sync 탭의 클라이언트 원천). */
        static constexpr const utf8* kReportLinePrefix = "State hash history:";
        /** @brief 샘플을 한 줄(`kReportLinePrefix <tick>:<16자리 16진 해시>,…`)로 씁니다. 표준 입력 명령의 답이 로그 한 줄로 나가는 길(멀티 프로세스 재생)에 쓴다. */
        [[nodiscard]] static bool writeReportLine( const vector<StateHashSample>& listSample, string& outText );
        /**
         * @brief 한 줄 판에 실을 수 있는 샘플 수의 상한입니다. 샘플 하나는 가장 길 때 `<20자리 틱>:<16자리 해시>,` 로 38 바이트라 머리와 합쳐 `DebugReplyLine::kMaxBytes` 안에 든다.
         *          `state.hashline` 은 이보다 많이 묻으면 거절하고, Sync 탭은 이 수만큼 묻는다.
         */
        static constexpr uint32 kMaxReportLineSamples = 90;
        /**
         * @brief @p line 에서 `kReportLinePrefix` 를 찾아 뒤의 샘플을 읽습니다. 머리가 없으면 false 이고, 머리 뒤 첫 덩이의 항목이 하나라도 형식이 다르거나 줄이 `DebugReplyLine::kMaxBytes` 를 넘으면 목록을 비우고 false 입니다.
         * @details 샘플이 없으면 머리 뒤가 `-` 다(빈 이력도 답이다).
         */
        [[nodiscard]] static bool tryParseReportLine( string_view line, vector<StateHashSample>& outListSample );
        /** @brief `writeReport` 의 글을 되읽습니다. 빈 줄은 건너뛰고, 형식이 다른 줄이 하나라도 있으면 목록을 비우고 false 입니다. */
        [[nodiscard]] static bool parseReport( string_view text, vector<StateHashSample>& outListSample );
        /**
         * @brief 두 이력에서 틱이 같은데 해시가 다른 **가장 이른 틱**을 찾습니다. 한쪽에만 있는 틱은 건너뜁니다.
         * @return 찾았으면 true
         */
        [[nodiscard]] static bool findFirstDivergence( const vector<StateHashSample>& listLeft, const vector<StateHashSample>& listRight, uint64& outTick );

    private:
        vector<StateHashSample> _listSample; ///< 링 버퍼(틱 오름차순으로 쌓이고 가득 차면 `_head` 부터 덮는다)
        uint32                  _capacity{ 0 };
        uint32                  _head{ 0 }; ///< 가득 찼을 때 가장 오래된 칸
    };
} // namespace sw
