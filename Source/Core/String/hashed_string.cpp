#include "pch.h"

#include "Core/CoreMinimal.h"

namespace sw
{
    namespace
    {
        hashed_string::AllocationInfo*  s_pInstance     = nullptr;
        hashed_wstring::AllocationInfo* s_pInstanceWide = nullptr;
    } // namespace

    template <>
    hashed_string::AllocationInfo& hashed_string::getAllocationInfo() noexcept
    {
        return *s_pInstance;
    }

    template <>
    hashed_wstring::AllocationInfo& hashed_wstring::getAllocationInfo() noexcept
    {
        return *s_pInstanceWide;
    }

    void HashedStringPool::initialize() noexcept
    {
        SW_ASSERT( s_pInstance == nullptr && s_pInstanceWide == nullptr );

        static hashed_string::AllocationInfo  s_instance;
        static hashed_wstring::AllocationInfo s_instanceWide;

        // 함수 지역 static 은 **한 번만** 생성된다. shutdown 이 `clear()` 로 0번 청크와 사전 정의
        // 이름까지 돌려주므로, 두 번째 initialize 에서는 생성자가 돌지 않아 **빈 테이블**을 가리키게
        // 된다 — `hashed_string( NameType_float3 )` 의 `c_str()` 이 nullptr 이고, 새로 intern 되는
        // 첫 문자열이 0번(`NameType_None`)을 받아 기본 생성자와 같아진다. 둘 다 조용한 오답이다.
        // 위 단정은 Debug 전용이라 Shipping 에서는 막아 주지도 못한다. 그래서 저장소를 다시 세운다.
        if ( s_instance._arrChunk[0].load( std::memory_order_acquire ) == nullptr )
            s_instance.initializeStorage();
        if ( s_instanceWide._arrChunk[0].load( std::memory_order_acquire ) == nullptr )
            s_instanceWide.initializeStorage();

        s_pInstance     = &s_instance;
        s_pInstanceWide = &s_instanceWide;
    }

    void HashedStringPool::shutdown() noexcept
    {
        // 단정만으로는 부족하다 — Shipping 에서 SW_ASSERT 는 사라지므로 initialize 전에 또는 두 번
        // 불리면 널을 역참조한다. shutdown 은 여러 번 불려도 안전해야 한다.
        if ( s_pInstance == nullptr || s_pInstanceWide == nullptr )
            return;

        s_pInstance->clear();
        s_pInstanceWide->clear();
        s_pInstance     = nullptr;
        s_pInstanceWide = nullptr;
    }
} // namespace sw
