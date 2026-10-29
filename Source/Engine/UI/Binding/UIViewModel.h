/**
 * @file UIViewModel.h
 * @brief 화면이 보여 주는 데이터(뷰모델)입니다. 게임 코드가 값을 넣고 바뀐 필드를 알립니다(UE5 MVVM 의 ViewModel 과 FieldNotify 에 해당).
 */
#pragma once
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct TypeInfo;

    class UIBindingSet;
} // namespace sw

namespace sw
{
    /**
     * @class UIViewModel
     * @brief 화면이 보여 주는 데이터입니다. 필드는 리플렉션 PROPERTY 이고, 바뀐 필드를 알리면 그 필드에 묶인 위젯 필드만 다음 바인딩 단계에서 갱신합니다.
     * @details 위젯은 뷰모델을 모르고 바인딩(문서의 `{bind:필드}`)만 안다. 세터 도우미 `setField` 는 값이 같으면 알리지 않는다.
     *          알림은 번호(단조 증가)로 남는다. 같은 프레임에 같은 필드를 여러 번 알려도 바인딩 단계는 한 번만 쓴다. 화면 여럿이 한 뷰모델을 공유할 수 있다.
     *
     *          파생 클래스는 `REFLECT()` 를 붙이고 `getTypeInfo()` 를 자기 `StaticType()` 으로 재정의한다(위젯과 같은 규칙이며 RTTI 는 쓰지 않는다).
     *          **수명**: 게임이 소유한다. 묶인 화면보다 먼저 지워지면 그 화면들의 바인딩이 이 뷰모델을 해제한다(지운 뒤 위젯 값은 그대로 남는다). 게임 스레드에서만 쓴다.
     */
    REFLECT( Abstract )
    class SW_API UIViewModel
    {
    public:
        REFLECT_BODY();

        UIViewModel();
        virtual ~UIViewModel();
        UIViewModel( const UIViewModel& )            = delete;
        UIViewModel& operator=( const UIViewModel& ) = delete;

        /** @brief 동적 리플렉션 타입입니다. 파생 클래스는 자기 `StaticType()` 을 반환하도록 재정의합니다. */
        virtual const TypeInfo* getTypeInfo() const;

        /** @brief 필드 @p field(PROPERTY 이름)가 바뀌었음을 알립니다. 묶인 위젯 필드는 다음 바인딩 단계에서 한 번 갱신됩니다. */
        void notifyFieldChanged( const hashed_string& field );
        /** @brief 값이 다르면 @p inoutField 에 넣고 @p field 를 알립니다(세터 도우미). 바뀌었으면 true 입니다. */
        template <typename ValueType>
        bool setField( ValueType& inoutField, const ValueType& value, const hashed_string& field )
        {
            if ( inoutField == value )
                return false;
            inoutField = value;
            notifyFieldChanged( field );
            return true;
        }

        /** @brief 마지막 알림의 번호입니다(알린 적 없으면 0). 알릴 때마다 오릅니다. */
        uint64 getChangeSerial() const { return _changeSerial; }
        /** @brief 필드 @p field 가 마지막으로 알려진 번호입니다(알린 적 없으면 0). */
        uint64 findFieldChangeSerial( const hashed_string& field ) const;

    private:
        friend class UIBindingSet;

        /** @struct FieldChange @brief 필드 하나의 마지막 알림 번호입니다. */
        struct FieldChange
        {
            hashed_string _field;
            uint64        _serial;
        };

        /** @brief 이 뷰모델을 보는 바인딩 집합을 등록하고 그 구독을 돌려줍니다(지울 때 알립니다). 바인딩 집합 하나에 구독 하나입니다. 구독이 사라지면 빠진다. */
        [[nodiscard]] Subscription registerObserver( UIBindingSet& bindingSet )
        {
            _listObserver.push_back( &bindingSet );
            return _observerSubscriptions.issue( &bindingSet );
        }

    private:
        /** @brief 바인딩 집합의 등록 구독이 풀렸습니다. 목록에서 뺍니다(주소로만 찾는다). */
        void removeObserver( UIBindingSet* pBindingSet ) { VectorUtil::removeFirst( _listObserver, pBindingSet ); }

        vector<FieldChange>                _listFieldChange; ///< 필드마다 한 줄입니다(중복 없이 필드 수만큼만 늘어납니다).
        vector<UIBindingSet*>              _listObserver;    ///< 이 뷰모델을 보는 바인딩 집합입니다(지울 때 해제하게 합니다).
        uint64                             _changeSerial;
        ItemSubscriptionList<UIBindingSet> _observerSubscriptions; ///< `_listObserver` 의 등록 구독(풀리면 `removeObserver`). 리플렉션 클래스라 기반을 더하지 않고 멤버로 둔다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다
    };
} // namespace sw
