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
