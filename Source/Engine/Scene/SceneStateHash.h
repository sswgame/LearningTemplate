/**
 * @file Engine/Scene/SceneStateHash.h
 * @brief 활성 씬의 상태 해시와 틱별 해시 이력입니다 — 두 프로세스의 상태가 처음 어긋난 틱을 찾는 데 씁니다(서버 디버깅 S7).
 * @details 해시는 이름이 있는 오브젝트를 이름 순으로 돌며 (이름, 활성, 월드 위치 비트)를 접습니다. 같은 씬을 같은 입력으로 돌린 두 프로세스는 같은 틱에 같은 값을 냅니다.
 *          오브젝트 ID 는 넣지 않습니다(프로세스마다 다를 수 있다). 값의 범위는 씬 트랜스폼까지입니다 — 키트별 상태(파괴 해시 · 롤백 상태 바이트)는 같은 이력에
 *          다른 이름의 해시로 더하는 자리가 이 이력의 확장입니다.
 *          이력은 프로세스에 하나이고 게임 스레드만 씁니다. 용량 0(기본)이면 꺼져 있어 서버는 해시를 계산하지 않습니다(`-gv_stateHashHistory=N`).
 *          개발 명령 `state.hash`(지금 값)와 `state.hashes <from> <count>`(이력)가 같은 줄 형식으로 답하고, `parseReport` 가 그 글을 되읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

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
    /** @struct SceneStateHash @brief 씬 상태 해시 계산입니다. */
    struct SW_API SceneStateHash
    {
        /** @brief 매니저의 오브젝트(이름 있는 것만)를 이름 순으로 접은 해시입니다. 오브젝트가 없으면 빈 해시 값입니다. */
        static uint64 compute( const GameObjectManager& manager );
    };
} // namespace sw

namespace sw
{
    /**
     * @class StateHashHistory
     * @brief 최근 N 틱의 해시 이력(링 버퍼)입니다. 서버가 틱마다 `record` 하고 개발 명령이 읽습니다.
     */
    class SW_API StateHashHistory
    {
    public:
        /** @brief 프로세스에 하나인 이력입니다. */
        static StateHashHistory& get();

        /** @brief 담을 틱 수를 정합니다(0 이면 끄고 비웁니다). */
        void   setCapacity( uint32 capacity );
        uint32 getCapacity() const { return _capacity; }
        bool   isRecording() const { return _capacity > 0; }

        /** @brief 틱 하나를 남깁니다. 가득 차면 가장 오래된 것을 덮습니다. 켜져 있지 않으면 아무 일도 없습니다. */
        void record( uint64 tick, uint64 hash );
        /** @brief 남은 샘플 중 가장 최근 것입니다(없으면 false). */
        [[nodiscard]] bool tryGetLatest( StateHashSample& outSample ) const;
        /** @brief @p fromTick 이후(포함)의 샘플을 틱 순으로 최대 @p count 개 채웁니다. */
        void collect( uint64 fromTick, uint32 count, vector<StateHashSample>& outListSample ) const;

        /** @brief 샘플을 한 줄에 하나(`<tick> <16자리 16진 해시>`)로 씁니다. */
        static void writeReport( const vector<StateHashSample>& listSample, string& outText );
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
