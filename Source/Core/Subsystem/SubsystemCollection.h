/**
 * @file Core/Subsystem/SubsystemCollection.h
 * @brief 서브시스템(`Subsystem`)을 소유하고, 선언된 의존으로 위상 정렬한 순서로 초기화 · 역순 종료 · 역순 소멸합니다.
 * @details 언리얼 `FSubsystemCollectionBase` 와 같은 자리입니다. 순서의 진실은 서브시스템이 선언한 의존 하나이고, 추가한 순서는 뜻이 없습니다
 *          (준비된 것이 여럿이면 이름이 앞인 것이 먼저라 결정적입니다). 정렬은 같은 이름 · 모르는 의존 · 순환 · 호스트 어긋남(어느 호스트에서 도는 것이
 *          그 호스트에서 돌지 않는 것에 의존)을 오류로 거절합니다.
 *          이 목록은 로그를 남기지 않습니다 — 오류는 `getError` 의 글이고 알리는 것은 부른 쪽이다. 그래서 로거도 이 목록의 첫 서브시스템이 될 수 있다
 *          (의존이 없는 루트부터 초기화하므로 그 뒤 서브시스템의 로그는 정상이다). 정렬 자체의 오류(선언 오타 · 순환)는 로거가 서기 전이라
 *          부른 쪽이 표준 오류로 내야 한다 — 그 오류는 개발 때 `EngineInitSequenceTest` 가 실제 목록으로 잡는 프로그래머 오류라 이것으로 좁힐 뿐 없애지는 않는다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/Subsystem.h"

namespace sw
{
    /** @brief 의존 그래프의 노드 하나입니다(이름과 먼저 서야 하는 이름들). */
    struct SubsystemNode
    {
        const utf8*             _pName;           ///< 이름
        span<const utf8* const> _listDependency;  ///< 먼저 서야 하는 이름들(없으면 오류)
        span<const utf8* const> _listDependent{}; ///< 뒤에 서야 하는 이름들(역방향 의존, 없으면 건너뛴다)
    };
} // namespace sw

namespace sw
{
    /** @brief `SubsystemCollection::computeGraph` 의 결과입니다. 인덱스는 노드 목록의 자리입니다. */
    struct SubsystemGraph
    {
        vector<uint32>         _listOrder;      ///< 초기화 순서(위상 순서). 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 서야 하는 노드
    };
} // namespace sw

namespace sw
{
    /**
     * @class SubsystemCollection
     * @brief 서브시스템 목록입니다. 추가 → 정렬 → `initializeAll( host )` → `shutdownAll()` → `destroyAll()`(소멸자도 같다).
     * @details 자리(인덱스)는 추가한 순서이고, 순서 목록은 그 자리를 담습니다. 서브시스템은 넘긴 타입으로 소멸합니다(정렬 할당 타입도 받는다).
     */
    class SW_API SubsystemCollection
    {
    public:
        SubsystemCollection();
        /** @brief 남은 서브시스템을 `destroyAll` 과 같이 종료 · 역순 소멸합니다. */
        ~SubsystemCollection();

        SubsystemCollection( const SubsystemCollection& )            = delete;
        SubsystemCollection& operator=( const SubsystemCollection& ) = delete;

        /** @brief 서브시스템을 넘겨받습니다. 정렬은 다음 `sort` · `initializeAll` 이 다시 합니다. 넘긴 객체를 돌려줍니다(소유는 목록). */
        template <class TSubsystem>
        TSubsystem& add( unique_ptr<TSubsystem> subsystem )
        {
            static_assert( std::is_base_of_v<Subsystem, TSubsystem>, "SubsystemCollection::add takes a Subsystem" );
            TSubsystem& added                           = *subsystem;
            static_cast<Subsystem&>( added )._memoryTag = TSubsystem::kSubsystemMemoryTag; // 용도는 형식이 안다(`Subsystem::getSubsystemMemoryTag`)
            addInternal( Entry{ subsystem.release(), &destroyAs<TSubsystem> } );
            return added;
        }

        /** @brief 의존으로 정렬합니다. 오류(같은 이름 · 모르는 의존 · 순환 · 호스트 어긋남)면 false 이고 `getError` 에 적습니다. */
        [[nodiscard]] bool sort();

        /**
         * @brief 위상 순서로 `initializeSubsystem` 을 부릅니다. 각 초기화는 그 서브시스템의 메모리 태그 아래에서 돕니다.
         * @details `Failed` 면 거기서 멈추고 false 입니다(`getError` 에 어느 것인지 적는다). `SkipDependents` 면 그에 의존하는 서브시스템을 건너뜁니다.
         *          @p host 와 겹치지 않는 호스트의 서브시스템은 부르지 않고 건너뜁니다(전용 서버의 RHI · 렌더러 · 플레이어 설정).
         * @return 정렬이 유효하고 실패한 서브시스템이 없으면 true 입니다.
         */
        [[nodiscard]] bool initializeAll( SubsystemHostMask host );
        /** @brief 초기화한 서브시스템을 역순으로 모두 멈추고(`stopSubsystem`), 다시 역순으로 모두 종료합니다(`shutdownSubsystem`). 두 번 불러도 됩니다. */
        void shutdownAll();
        /**
         * @brief 모든 서브시스템을 초기화 순서의 역순으로 소멸시키고 목록을 비웁니다. 아직 종료하지 않은 것이 있으면 먼저 `shutdownAll` 합니다.
         * @details 초기화한 것만이 아니라 **모두**입니다 — 실패한 것, 건너뛴 것, 닿지 못한 것도 소멸합니다. 정렬이 안 됐으면(오류) 추가의 역순입니다.
         */
        void destroyAll();

        /**
         * @brief @p subsystem 에 (간접으로라도) 의존하는 **초기화된** 서브시스템을 `shutdownAll` 과 같이 역순으로 멈춘 뒤 역순으로 종료합니다. @p subsystem 자신은 그대로 둡니다.
         * @details 기동 중에 하나를 갈아 끼울 때 씁니다(백엔드 교체: RHI 의 디바이스를 다시 만든다). 내린 것은 `restartStopped` 가 다시 세웁니다.
         */
        void shutdownDependents( const Subsystem& subsystem );
        /**
         * @brief `shutdownDependents` 가 내린 서브시스템을 위상 순서로 다시 초기화합니다.
         * @return 모두 다시 섰으면 true 입니다. 실패한 것에서 멈추고(`getError` 에 적는다), 그것과 나머지는 내린 채로 남습니다(종료 대상에서도 빠진다).
         */
        [[nodiscard]] bool restartStopped();

        /** @brief 서브시스템 수입니다(추가한 수). */
        uint32 getSubsystemCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief @p index(추가한 자리)의 서브시스템입니다. `destroyAll` 뒤에는 없습니다. */
        Subsystem& getSubsystem( uint32 index ) const { return *_listEntry[index]._pSubsystem; }
        /** @brief 의존에서 계산한 초기화 순서(자리)입니다. 종료 · 소멸은 이 역순입니다. 정렬 전이나 오류면 빕니다. */
        const vector<uint32>& getInitializeOrder() const { return _listOrder; }
        /** @brief 지금 초기화돼 있는 서브시스템(자리, 초기화한 순서)입니다. */
        const vector<uint32>& getInitializedIndices() const { return _listInitialized; }
        /** @brief 서브시스템 하나의 기동 또는 종료 시간입니다(시동 · 종료 요약 로그의 재료). */
        struct SubsystemTiming
        {
            const utf8* _pName;  ///< `getSubsystemName()` — 서브시스템이 사는 동안 유효한 상수 글
            int64       _micros; ///< 벽시계 마이크로초(기동: `initializeSubsystem`, 종료: `stopSubsystem` + `shutdownSubsystem`)
        };
        /** @brief 마지막 `initializeAll` 에서 초기화를 부른 서브시스템의 시간입니다(위상 순서). 건너뛴 것은 없습니다. */
        const vector<SubsystemTiming>& getInitTimings() const { return _listInitTiming; }
        /** @brief 마지막 `shutdownAll` 에서 내린 서브시스템의 시간입니다(내린 순서). `destroyAll` 이 비운다 — 그 전에 읽는다. */
        const vector<SubsystemTiming>& getShutdownTimings() const { return _listShutdownTiming; }
        /**
         * @brief 기동 · 종료 시간을 잴지 정합니다(기본은 재지 않음). 단조 시계(`MonotonicClock`)를 직접 읽는다.
         * @details 종료도 잰다 — 로그 없이 오래 걸린 종료(Debug 의 자식 클라이언트가 15 초 시한에 걸렸다)가 어느 서브시스템인지 줄 하나로 보인다.
         *          예전에는 Subsystem 층이 Time 층 아래라 위 층이 시계를 함수 포인터로 건넸다 — Time 을 티어 2 로 내려 우회를 없앴다.
         */
        void setTimingEnabled( bool bEnabled ) { _bTimingEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        /** @brief `shutdownDependents` 가 내리고 아직 다시 세우지 않은 서브시스템(자리, 초기화 순서)입니다. */
        const vector<uint32>& getStoppedIndices() const { return _listStopped; }
        /** @brief 정렬 오류 · 초기화 실패입니다. 없으면 빈 글입니다. */
        const string& getError() const { return _error; }

        /**
         * @brief 노드 목록을 위상 정렬합니다. 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다(목록 순서는 보지 않습니다).
         * @return 정렬한 그래프. 같은 이름(`ErrorKind::AlreadyExists`) · 모르는 의존(`NotFound`, 대상 `A -> B`) · 순환(`Cycle`, 대상은 남은 이름들)이면 오류입니다.
         */
        [[nodiscard]] static expected<SubsystemGraph> computeGraph( const vector<SubsystemNode>& listNode );

        /**
         * @brief 이 스레드에서 초기화 중인 서브시스템이 범위 조회 · 엔진 서비스 칸으로 @p target 을 얻었음을 알립니다(`SubsystemScope` 의 조회가 소유한 칸을 돌려줄 때, `engine::getSubsystem` · `findSubsystem` 이 소유자가 있는 칸을 읽을 때 부른다).
         * @details @p target 이 지금 초기화 중인 목록(가장 안쪽)의 것인데 그 서브시스템 자신도, 선언한 의존(간접 포함 — 역방향 의존 칸으로 생긴 것도)도 아니면 그 초기화는 진다:
         *          `initializeAt` 이 종료를 부르고 `Failed` 로 바꾸며 `getError` 에 두 이름을 적는다. 의존을 빼먹은 조회는 이름 순서가 우연히 맞으면 지나가다가
         *          서브시스템 하나를 더하는 순간 아직 서지 않은 객체를 읽는다 — 그 모양을 기동에서 바로 멈춘다(언리얼 `InitializeDependency` 가 맡는 자리).
         *          다른 목록(상위 범위 · 매단 자식)의 객체는 범위 수명이 지키므로 보지 않는다. 초기화 밖 · 다른 스레드의 조회는 아무 일도 하지 않는다.
         */
        static void recordInitializeLookup( const Subsystem& target );

        /**
         * @class HostedTaskScope
         * @brief 초기화 안에서 그 서브시스템의 것이 아닌 일(단위 테스트 실행 · 쿠킹 같은 헤드리스 작업)을 도는 동안 초기화 조회 기록을 내려 둡니다. 소멸이 되돌립니다.
         * @details 그 일의 조회는 초기화하는 서브시스템의 의존이 아니다 — 언리얼 커맨드릿처럼 엔진을 빌려 쓰는 다른 프로그램이다. 기록을 그대로 두면 테스트 본문이
         *          찾은 서브시스템(UI 등)이 `Headless` 의 빠진 의존으로 잡혀 기동이 졌다. 작업을 엔진 초기화 뒤로 옮기는 것이 상용 구조지만, 작업이 기동 단계 사이(의존 뒤 · 피의존 앞)에 서야 하는 지금은 이 경계로 가른다.
         */
        class HostedTaskScope
        {
        public:
            HostedTaskScope();
            ~HostedTaskScope();
            HostedTaskScope( const HostedTaskScope& )            = delete;
            HostedTaskScope& operator=( const HostedTaskScope& ) = delete;

        private:
            void* _pSaved; ///< 내려 둔 프레임(`SubsystemCollection.cpp` 의 내부 형식 — 헤더에 드러내지 않는다)
        };

    private:
        /** @brief @p dependencyIndex 가 @p index 의 의존(간접 포함)이면 true 입니다. */
        bool isDependencyOf( uint32 index, uint32 dependencyIndex ) const;

    private:
        /** @brief 서브시스템 하나와 그것을 넘긴 타입으로 소멸시키는 함수입니다. */
        struct Entry
        {
            Subsystem* _pSubsystem;
            void ( *_pDestroy )( Subsystem* pSubsystem );
        };

        template <class TSubsystem>
        static void destroyAs( Subsystem* pSubsystem )
        {
            default_delete<TSubsystem>{}( static_cast<TSubsystem*>( pSubsystem ) );
        }

        void addInternal( const Entry& entry );
        /** @brief @p index 의 서브시스템을 그 메모리 태그 아래에서 초기화합니다. */
        SubsystemInitResult initializeAt( uint32 index );

    private:
        vector<Entry>           _listEntry;          ///< 추가한 순서(자리). 소유한다 — `destroyAll` 이 `_pDestroy` 로 지운다
        vector<uint32>          _listOrder;          ///< 초기화 순서(자리)
        vector<vector<uint32>>  _listDependency;     ///< 자리마다 먼저 서야 하는 자리
        vector<uint32>          _listInitialized;    ///< 초기화한 자리(종료가 역순으로 돈다)
        vector<uint32>          _listStopped;        ///< `shutdownDependents` 가 내린 자리(초기화 순서)
        vector<SubsystemTiming> _listInitTiming;     ///< `initializeAll` 이 잰 초기화 시간(`getInitTimings`)
        vector<SubsystemTiming> _listShutdownTiming; ///< `shutdownAll` 이 잰 종료 시간(`getShutdownTimings`)
        string                  _error;              ///< 정렬 오류 · 초기화 실패
        uint8                   _bSorted        : 1; ///< 마지막 추가 뒤에 정렬했는가
        uint8                   _bTimingEnabled : 1; ///< 기동 · 종료 시간을 재는가(`setTimingEnabled`)
    };
} // namespace sw
