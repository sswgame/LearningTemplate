#include "pch.h"

#include "Engine/Serialization/Format/DurableRecordTypeList.h"

#include "Core/Registry/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct DurableRecordTypeInternal
        {
            /** @brief 오른 형식(올린 차례). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다(`FormatUpgradeSteps` 와 같다). */
            static SW_DEFINE_REGISTRATION_LIST( getTypes, const DurableRecordType, "DurableRecordTypes", RegistrationOrder::Insertion, true )
        };
    } // namespace

    DurableRecordType::DurableRecordType( StaticTypeFn pfnStaticType )
        : _pfnStaticType{ pfnStaticType }
    {
#if !defined( SW_SHIPPING )
        (void)DurableRecordTypeInternal::getTypes().add( this ); // 같은 형식을 두 TU 가 쓰면 정적 칸도 하나다(인라인 정적 칸)
#endif
    }

    DurableRecordType::~DurableRecordType()
    {
#if !defined( SW_SHIPPING )
        (void)DurableRecordTypeInternal::getTypes().remove( this );
#endif
    }

    uint32 DurableRecordType::getCount()
    {
        return DurableRecordTypeInternal::getTypes().getCount();
    }

    const DurableRecordType* DurableRecordType::getAt( uint32 index )
    {
        return DurableRecordTypeInternal::getTypes().getAt( index );
    }
} // namespace sw
