#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        hashed_string::AllocationInfo* s_pInstance = nullptr;
    } // namespace

    template <>
    hashed_string::AllocationInfo& hashed_string::getAllocationInfo() noexcept
    {
        return *s_pInstance;
    }

    void HashedStringPool::initialize() noexcept
    {
        SW_ASSERT( s_pInstance == nullptr );

        // 테이블의 수명은 `initialize` · `shutdown`(이름 풀 서브시스템)이 정한다 — 프로세스 종료의 정적 소멸에 맡기지 않는다. 함수 지역 static 객체로 두면
        // `exit()` 중 정적 소멸이 그것을 먼저 지우고, 뒤이어 도는 엔진 정리(`shutdown`)가 지워진 테이블을 읽는다. 그래서 정적 저장소에 한 번 세우고 소멸자는 부르지
        // 않는다(블록은 `shutdown` 의 `clear` 가 모두 돌려준다).
        alignas( hashed_string::AllocationInfo ) static uint8 s_arrStorage[sizeof( hashed_string::AllocationInfo )];
        static hashed_string::AllocationInfo&                 s_instance = *sw_placement_new( s_arrStorage ) hashed_string::AllocationInfo();

        // 함수 지역 static 은 **한 번만** 생성된다. shutdown 이 `clear()` 로 0번 청크와 미리 정의된 이름까지 돌려주므로, 두 번째
        // initialize 에서는 생성자가 돌지 않아 **빈 테이블**을 가리키게 된다. 그러면 `hashed_string( NameType_float3 )` 의
        // `c_str()` 이 nullptr 이 되고, 새로 intern 되는 첫 문자열이 0번(`NameType_None`)을 받아 기본 생성자와 같아진다. 둘 다
        // 조용히 틀린 답이다. 위의 단언은 Debug 전용이라 Shipping 에서는 막아 주지도 못한다. 그래서 저장소를 다시 생성한다.
        if ( s_instance._arrChunk[0].load( std::memory_order_acquire ) == nullptr )
            s_instance.initializeStorage();

        s_pInstance = &s_instance;
    }

    void HashedStringPool::shutdown() noexcept
    {
        // 단언만으로는 부족하다. Shipping 에서는 SW_ASSERT 가 사라지므로, initialize 전에 불리거나 두 번 불리면 nullptr 을
        // 역참조한다. shutdown 은 여러 번 불려도 안전해야 한다.
        if ( s_pInstance == nullptr )
            return;

        s_pInstance->clear();
        s_pInstance = nullptr;
    }

    HashedStringPool::ScopedPoolMemoryTag::ScopedPoolMemoryTag() noexcept
        : _previousTag{ MemoryTag::Unknown }
    {
        if constexpr ( kMemoryTagScopesEnabled )
        {
            _previousTag = Memory::getCurrentMemoryTag();
            Memory::setCurrentMemoryTag( MemoryTag::EngineMisc );
        }
    }

    HashedStringPool::ScopedPoolMemoryTag::~ScopedPoolMemoryTag() noexcept
    {
        if constexpr ( kMemoryTagScopesEnabled )
            Memory::setCurrentMemoryTag( _previousTag );
    }
} // namespace sw
