/**
 * @file Engine/Common/SubsystemCollection.h
 * @brief 서브시스템(`ISubsystem`)을 소유하고, 선언된 의존으로 위상 정렬한 순서로 초기화 · 역순 종료 · 역순 소멸합니다.
 * @details 언리얼 `FSubsystemCollectionBase` 와 같은 자리입니다. 순서의 진실은 서브시스템이 선언한 의존 하나이고, 추가한 순서는 뜻이 없습니다
 *          (준비된 것이 여럿이면 이름이 앞인 것이 먼저라 결정적입니다). 정렬은 같은 이름 · 모르는 의존 · 순환 · 대상 어긋남(서버에서 도는 것이
 *          클라이언트 전용에 의존)을 오류로 거절합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Common/ISubsystem.h"

namespace sw
{
    /** @brief 의존 그래프의 노드 하나입니다(이름과 먼저 서야 하는 이름들). */
    struct EngineInitNode
    {
        const utf8* _pName;           ///< 이름
        const utf8* _pDependencyText; ///< 먼저 서야 하는 이름들(쉼표 · 중괄호 · 공백으로 구분 — 기동 표는 `{ A, B }`)
    };
} // namespace sw

namespace sw
{
    /** @brief `SubsystemCollection::computeGraph` 의 결과입니다. 인덱스는 노드 목록의 자리입니다. */
    struct EngineInitGraph
    {
        vector<uint32>         _listOrder;      ///< 초기화 순서(위상 순서). 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다
        vector<vector<uint32>> _listDependency; ///< 노드마다 먼저 서야 하는 노드
    };
} // namespace sw

namespace sw
{
    /**
     * @class SubsystemCollection
     * @brief 서브시스템 목록입니다. 추가 → 정렬 → `initializeAll( target )` → `shutdownAll()` → `destroyAll()`(소멸자도 같다).
     * @details 자리(인덱스)는 추가한 순서이고, 순서 목록은 그 자리를 담습니다.
     */
    class SW_API SubsystemCollection
    {
    public:
        SubsystemCollection();
        /** @brief 남은 서브시스템을 `destroyAll` 과 같이 종료 · 역순 소멸합니다. */
        ~SubsystemCollection();

        SubsystemCollection( const SubsystemCollection& )            = delete;
        SubsystemCollection& operator=( const SubsystemCollection& ) = delete;

        /**
         * @brief 서브시스템을 넘겨받습니다. 정렬은 다음 `sort` · `initializeAll` 이 다시 합니다. 넘긴 객체를 돌려줍니다(소유는 목록).
         * @details 이 목록이 `ISubsystem*` 로 지우므로 정렬 할당 타입은 받지 않고(컴파일 오류), 기반 주소가 객체 주소와 다르면 단언합니다.
         */
        template <class TSubsystem>
        TSubsystem& add( unique_ptr<TSubsystem> subsystem )
        {
            static_assert( std::is_base_of_v<ISubsystem, TSubsystem>, "SubsystemCollection::add takes an ISubsystem" );
            static_assert( kUsesAlignedAllocation<TSubsystem> == false, "SubsystemCollection deletes through ISubsystem* - over-aligned subsystems are not supported" );
            TSubsystem& added = *subsystem;
            SW_ASSERT( static_cast<const void*>( static_cast<const ISubsystem*>( &added ) ) == static_cast<const void*>( &added ) );
            addInternal( unique_ptr<ISubsystem>( std::move( subsystem ) ) );
            return added;
        }

        /** @brief 의존으로 정렬합니다. 오류(같은 이름 · 모르는 의존 · 순환 · 대상 어긋남)면 false 이고 `getError` 에 적습니다. */
        [[nodiscard]] bool sort();

        /**
         * @brief 위상 순서로 `initializeSubsystem` 을 부릅니다. 각 초기화는 그 서브시스템의 메모리 태그 아래에서 돕니다.
         * @details `Failed` 면 거기서 멈추고 false 입니다. `SkipDependents` 면 그에 의존하는 서브시스템을 건너뜁니다.
         *          @p target 과 겹치지 않는 대상의 서브시스템은 부르지 않고 건너뜁니다(전용 서버의 RHI · 렌더러 · 플레이어 설정).
         * @return 정렬이 유효하고 실패한 서브시스템이 없으면 true 입니다.
         */
        [[nodiscard]] bool initializeAll( EngineInitTarget target );
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
        void shutdownDependentsOf( const ISubsystem& subsystem );
        /**
         * @brief `shutdownDependentsOf` 가 내린 서브시스템을 위상 순서로 다시 초기화합니다.
         * @return 모두 다시 섰으면 true 입니다. 실패한 것에서 멈추고, 그것과 나머지는 내린 채로 남습니다(종료 대상에서도 빠진다).
         */
        [[nodiscard]] bool restartStopped();

        /** @brief 서브시스템 수입니다(추가한 수). */
        uint32 getSubsystemCount() const { return static_cast<uint32>( _listSubsystem.size() ); }
        /** @brief @p index(추가한 자리)의 서브시스템입니다. `destroyAll` 뒤에는 없습니다. */
        ISubsystem& getSubsystem( uint32 index ) const { return *_listSubsystem[index]; }
        /** @brief 의존에서 계산한 초기화 순서(자리)입니다. 종료 · 소멸은 이 역순입니다. 정렬 전이나 오류면 빕니다. */
        const vector<uint32>& getInitializeOrder() const { return _listOrder; }
        /** @brief 지금 초기화돼 있는 서브시스템(자리, 초기화한 순서)입니다. */
        const vector<uint32>& getInitializedIndices() const { return _listInitialized; }
        /** @brief `shutdownDependentsOf` 가 내리고 아직 다시 세우지 않은 서브시스템(자리, 초기화 순서)입니다. */
        const vector<uint32>& getStoppedIndices() const { return _listStopped; }
        /** @brief 정렬 오류입니다. 없으면 빈 글입니다. */
        const string& getError() const { return _error; }

        /**
         * @brief 노드 목록을 위상 정렬합니다. 준비된 노드가 여럿이면 이름이 앞인 것이 먼저입니다(목록 순서는 보지 않습니다).
         * @return 같은 이름 · 모르는 의존 · 순환이면 false 이고 @p outError 에 무엇인지 적습니다.
         */
        [[nodiscard]] static bool computeGraph( const vector<EngineInitNode>& listNode, EngineInitGraph& outGraph, string& outError );

    private:
        void addInternal( unique_ptr<ISubsystem> subsystem );
        /** @brief @p index 의 서브시스템을 그 메모리 태그 아래에서 초기화합니다. */
        EngineInitResult initializeAt( uint32 index );

    private:
        vector<unique_ptr<ISubsystem>> _listSubsystem;   ///< 추가한 순서(자리)
        vector<uint32>                 _listOrder;       ///< 초기화 순서(자리)
        vector<vector<uint32>>         _listDependency;  ///< 자리마다 먼저 서야 하는 자리
        vector<uint32>                 _listInitialized; ///< 초기화한 자리(종료가 역순으로 돈다)
        vector<uint32>                 _listStopped;     ///< `shutdownDependentsOf` 가 내린 자리(초기화 순서)
        string                         _error;           ///< 정렬 오류
        bool                           _bSorted;         ///< 마지막 추가 뒤에 정렬했는가
    };
} // namespace sw
