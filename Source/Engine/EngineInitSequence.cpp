#include "pch.h"

#include "Engine/EngineInitSequence.h"

#include "Core/Common/Defines.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct EngineInitSequenceInternal
        {
            /** @brief 표 그대로의 이름 · 의존 칸입니다(줄 순서 = `EngineInitStep` 값). 의존 칸은 `{ A, B }` 를 글로 든다. */
            static constexpr EngineInitNode kArrStepNode[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... ) { #Name, #__VA_ARGS__ },
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepNode ) / sizeof( kArrStepNode[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                           "Startup node table must have one row per EngineInitStep" );

            /** @brief 표의 메모리 태그 칸입니다(줄 순서 = `EngineInitStep` 값). 단계 초기화를 부르는 동안 건다. */
            static constexpr MemoryTag kArrStepMemoryTag[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... ) MemoryTag::Tag,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepMemoryTag ) / sizeof( kArrStepMemoryTag[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                           "Startup memory tag table must have one row per EngineInitStep" );

            /** @brief 표의 대상 칸입니다(줄 순서 = `EngineInitStep` 값). */
            static constexpr EngineInitTarget kArrStepTarget[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... ) EngineInitTarget::Target,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepTarget ) / sizeof( kArrStepTarget[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                           "Startup target table must have one row per EngineInitStep" );
        };
    } // namespace
} // namespace sw

namespace sw
{
    // 표를 컴파일 때 본다: 의존 이름 오타 · 순환 · 대상 어긋남이 컴파일 오류다. 줄 순서는 보지 않는다(순서는 의존에서 계산한다).
    // 서브시스템(`ISubsystem`)의 의존은 같은 판정을 실행 시간에 받는다(`SubsystemCollection::sort`).
    namespace EngineInitTableCheck
    {
        enum Index : uint32
        {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) Name,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            Count
        };

        /** @brief 한 단계의 의존 칸입니다(자리). 의존이 `kMaxDependency` 를 넘으면 상수 평가가 실패해 컴파일 오류다. */
        struct DependencyRow
        {
            static constexpr uint32 kMaxDependency = 16;

            uint32 _count;
            uint32 _arrIndex[kMaxDependency];
        };

        constexpr DependencyRow makeDependencyRow( std::initializer_list<uint32> listDependency )
        {
            DependencyRow row{};
            for ( const uint32 dependency : listDependency )
            {
                row._arrIndex[row._count] = dependency;
                ++row._count;
            }
            return row;
        }

        /** @brief 표의 의존 칸(줄 순서)입니다. */
        constexpr DependencyRow kArrDependencyRow[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... ) makeDependencyRow( std::initializer_list<uint32> __VA_ARGS__ ),
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };

        /** @brief 의존이 모두 선 단계를 되풀이해 세운다(Kahn). 끝까지 서지 못한 단계가 있으면 순환이다. */
        constexpr bool isAcyclic()
        {
            bool   arrStarted[Count]{};
            uint32 startedCount = 0;
            bool   bProgress    = true;
            while ( bProgress )
            {
                bProgress = false;
                for ( uint32 stepIndex = 0; stepIndex < Count; ++stepIndex )
                {
                    if ( arrStarted[stepIndex] )
                        continue;
                    bool bReady = true;
                    for ( uint32 dependencyIndex = 0; dependencyIndex < kArrDependencyRow[stepIndex]._count; ++dependencyIndex )
                    {
                        if ( arrStarted[kArrDependencyRow[stepIndex]._arrIndex[dependencyIndex]] == false )
                            bReady = false;
                    }
                    if ( bReady )
                    {
                        arrStarted[stepIndex] = true;
                        ++startedCount;
                        bProgress = true;
                    }
                }
            }
            return startedCount == Count;
        }
        static_assert( isAcyclic(), "EngineInitStepList.xxx: the step dependencies form a cycle" );

        /** @brief 표의 대상 칸(줄 순서)입니다. 아래 검사가 의존의 대상을 자리로 찾는다. */
        constexpr uint8 kArrTargetMask[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... ) static_cast<uint8>( EngineInitTarget::Target ),
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };

        /** @brief @p selfMask 의 대상마다 의존이 모두 도는가 — 서버에서 도는 단계가 클라이언트 전용 단계에 의존하면 false 입니다. */
        constexpr bool areTargetsCovered( uint8 selfMask, std::initializer_list<uint32> listDependency )
        {
            for ( const uint32 dependency : listDependency )
            {
                if ( ( selfMask & static_cast<uint8>( ~kArrTargetMask[dependency] ) ) != 0 )
                    return false;
            }
            return true;
        }

// `{ A, B }` 의 쉼표는 매크로 인자를 가르므로 가변 인자로 받아 다시 붙인다(`__VA_ARGS__` = `{ A, B }`).
#define SW_ENGINE_STARTUP_STEP( Name, Tag, Target, ... )                                                                           \
    static_assert( areTargetsCovered( static_cast<uint8>( EngineInitTarget::Target ), std::initializer_list<uint32> __VA_ARGS__ ), \
                   "EngineInitStepList.xxx: step " #Name " runs on a target where one of its dependencies does not" );
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
    } // namespace EngineInitTableCheck
} // namespace sw

namespace sw
{
    /**
     * @brief 표의 줄 하나를 `ISubsystem` 으로 보입니다. 본문은 순서가 기억한 호스트의 본문 표(`_pArrEntry`)에서 고릅니다.
     * @details 해제 본문(`destroy<단계>`)은 소멸자가 부릅니다 — 목록이 모든 종료 뒤에 역순으로 지우므로 표의 해제 순서가 그대로다.
     *          호스트를 받기 전(`initializeAll` 전)이거나 순서가 사라지는 중(`~EngineInitSequence`)이면 본문을 부르지 않습니다.
     */
    class EngineInitSequence::StepSubsystem final : public ISubsystem
    {
    public:
        StepSubsystem( const EngineInitSequence& sequence, EngineInitStep step )
            : _sequence{ sequence }
            , _step{ step }
        {
        }
        ~StepSubsystem() override
        {
            const EngineInitStepEntry* const pEntry = findEntry();
            if ( pEntry != nullptr && pEntry->_pDestroy != nullptr )
                pEntry->_pDestroy( _sequence._pHost );
        }

        EngineInitResult initializeSubsystem() override
        {
            const EngineInitStepEntry* const pEntry = findEntry();
            if ( pEntry == nullptr || pEntry->_pInitialize == nullptr )
                return EngineInitResult::Succeeded;
            return pEntry->_pInitialize( _sequence._pHost );
        }
        void shutdownSubsystem() override
        {
            const EngineInitStepEntry* const pEntry = findEntry();
            if ( pEntry != nullptr && pEntry->_pShutdown != nullptr )
                pEntry->_pShutdown( _sequence._pHost );
        }

        const utf8*      getSubsystemName() const override { return EngineInitSequence::getStepName( _step ); }
        const utf8*      getSubsystemDependencies() const override { return EngineInitSequenceInternal::kArrStepNode[static_cast<uint32>( _step )]._pDependencyText; }
        EngineInitTarget getSubsystemTarget() const override { return EngineInitSequence::getStepTarget( _step ); }
        MemoryTag        getSubsystemMemoryTag() const override { return EngineInitSequence::getStepMemoryTag( _step ); }

    private:
        const EngineInitStepEntry* findEntry() const { return _sequence._pArrEntry != nullptr ? &_sequence._pArrEntry[static_cast<uint32>( _step )] : nullptr; }

    private:
        const EngineInitSequence& _sequence;
        EngineInitStep            _step;
    };
} // namespace sw

namespace sw
{
    EngineInitSequence::EngineInitSequence()
        : _subsystems{}
        , _listOrder{}
        , _pArrEntry{ nullptr }
        , _pHost{ nullptr }
    {
        // 표의 줄을 줄 순서로 넣는다 — 목록의 자리 0 … Count-1 이 곧 `EngineInitStep` 값이다(`makeTableSteps`).
        for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( EngineInitStep::Count ); ++stepIndex )
        {
            _subsystems.add( make_unique<StepSubsystem>( *this, static_cast<EngineInitStep>( stepIndex ) ) );
        }
        refreshOrder();
    }

    EngineInitSequence::~EngineInitSequence()
    {
        // 표의 해제 본문은 `destroyAll` 로만 부른다 — 여기서는 호스트가 이미 사라졌을 수 있다(테스트의 지역 호스트). 서브시스템만 소멸한다.
        _pArrEntry = nullptr;
        _pHost     = nullptr;
        _subsystems.destroyAll();
    }

    void EngineInitSequence::destroyAll()
    {
        _subsystems.destroyAll();
    }

    void EngineInitSequence::refreshOrder()
    {
        _listOrder.clear();
        if ( _subsystems.sort() == false )
            return; // 오류는 `getError` 로 남고 `initializeAll` 이 알린다
        _listOrder = makeTableSteps( _subsystems.getInitializeOrder() );
    }

    vector<EngineInitStep> EngineInitSequence::makeTableSteps( const vector<uint32>& listIndex )
    {
        vector<EngineInitStep> listStep;
        listStep.reserve( listIndex.size() );
        for ( const uint32 index : listIndex )
        {
            if ( index < static_cast<uint32>( EngineInitStep::Count ) )
                listStep.push_back( static_cast<EngineInitStep>( index ) );
        }
        return listStep;
    }

    void EngineInitSequence::shutdownDependentsOf( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex < _subsystems.getSubsystemCount() )
            _subsystems.shutdownDependentsOf( _subsystems.getSubsystem( stepIndex ) );
    }

    const utf8* EngineInitSequence::getStepName( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineInitStep::Count ) )
            return "Unknown";
        return EngineInitSequenceInternal::kArrStepNode[stepIndex]._pName;
    }

    MemoryTag EngineInitSequence::getStepMemoryTag( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineInitStep::Count ) )
            return MemoryTag::Unknown;
        return EngineInitSequenceInternal::kArrStepMemoryTag[stepIndex];
    }

    EngineInitTarget EngineInitSequence::getStepTarget( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineInitStep::Count ) )
            return EngineInitTarget::All;
        return EngineInitSequenceInternal::kArrStepTarget[stepIndex];
    }

    vector<EngineInitNode> EngineInitSequence::makeStepNodes()
    {
        vector<EngineInitNode> listNode;
        listNode.reserve( static_cast<size_t>( EngineInitStep::Count ) );
        for ( const EngineInitNode& node : EngineInitSequenceInternal::kArrStepNode )
        {
            listNode.push_back( node );
        }
        return listNode;
    }
} // namespace sw
