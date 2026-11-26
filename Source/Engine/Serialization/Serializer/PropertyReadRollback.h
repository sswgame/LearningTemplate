/**
 * @file PropertyReadRollback.h
 * @brief 소프트 읽기가 읽지 못한 필드를 읽기 전 값으로 되돌립니다(세 형식의 이어 읽기가 함께 씀).
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TextAddress.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    /**
     * @brief 소프트 읽기가 읽기 전 값으로 되돌린 칸 하나입니다(`PropertyReadRollback` 이 담는다). 범위 검사가 이 칸을 보지 않는다.
     * @details 값 주소만으로는 구조체 칸과 그 첫 필드(오프셋 0)를 가르지 못해 프로퍼티를 함께 든다.
     */
    struct RestoredPropertyValue
    {
        const void*         _pValue{ nullptr };
        const PropertyInfo* _pProperty{ nullptr };

        [[nodiscard]] bool operator==( const RestoredPropertyValue& other ) const { return _pValue == other._pValue && _pProperty == other._pProperty; }
    };

    /**
     * @class PropertyReadRollback
     * @brief 읽지 못한 필드를 넘기고 이어 읽는 읽기가 그 필드를 읽기 전 값으로 남기게 합니다. 세 형식(바이너리 태그 스트림, JSON, XML)의 소프트 읽기가 함께 씁니다.
     * @details 컨테이너 · 구조체는 원소와 필드를 채운 뒤 끝에서 실패하므로, 담지 않고 넘기면 읽다 만 값이 남는다. 그래서 필드를 읽기 전에
     *          `keepValueBeforeRead` 로 값을 이 빌드의 바이트(`PropertySerializer::writePropertyBinary`)로 담고, 읽기가 실패하면 `restoreValueBeforeRead` 로 되읽는다.
     *          되돌리는 단위는 실패를 받은 필드 하나다. 구조체 칸 안의 실패는 안쪽 읽기가 같은 규칙으로 그 안쪽 필드만 되돌린다.
     *          소유 포인터 목록은 담지 않는다. 되돌리면 팩토리가 컴포넌트를 새로 만들어 다른 객체가 된다. 그래서 그 목록의 읽기가 실패하면 되돌리지 않고 실패다.
     *          두 함수가 false 이면 부른 쪽은 객체째 실패한다(오류는 여기서 남긴다). 읽다 만 값이나 기본값으로 넘어가지 않는다.
     *          실패하면 객체째 버리는 엄격 읽기는 담지 않는다(담는 비용은 이어 읽는 읽기만 낸다).
     */
    class PropertyReadRollback
    {
    public:
        PropertyReadRollback();

        /** @brief @p prop 의 지금 값을 담습니다. 앞서 담은 값은 버립니다. 값을 바이트로 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool keepValueBeforeRead( const TypeInfo& ownerType, const PropertyInfo& prop, ObjectAddress instance, const SerializeContext& context );
        /**
         * @brief 마지막으로 담은 필드를 담은 값으로 되돌립니다. 그 필드의 읽기가 실패한 뒤에 부릅니다. @p instance 는 담을 때와 같은 객체입니다.
         * @return 소유 포인터 목록이거나(되돌릴 수 없다) 담은 바이트를 되읽지 못하면(코덱 결함) 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool restoreValueBeforeRead( MutableObjectAddress instance, const SerializeContext& context );
        /** @brief 지금까지 되돌린 칸입니다 — 소프트 읽기 끝의 범위 검사(`PropertyConstraints::isWithinDeclaredRanges`)가 뺀다. 되돌릴 때마다 여기 담기므로 형식이 따로 적지 않는다. */
        [[nodiscard]] const vector<RestoredPropertyValue>& getRestoredValues() const { return _listRestored; }

    private:
        vector<RestoredPropertyValue> _listRestored;
        vector<uint8>                 _bytes;
        const TypeInfo*               _pOwnerType;
        const PropertyInfo*           _pProperty;
        uint8                         _bOwnedPointerList : 1;
    };
} // namespace sw
