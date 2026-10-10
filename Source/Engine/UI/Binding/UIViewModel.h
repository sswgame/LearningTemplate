/**
 * @file UIViewModel.h
 * @brief 화면이 보이는 데이터(뷰모델)입니다 — 게임 코드가 값을 넣고 바뀐 필드를 알립니다(UE5 MVVM 의 ViewModel · FieldNotify).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
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
     * @brief 화면이 보이는 데이터입니다. 필드는 리플렉션 PROPERTY 이고, 바뀐 필드를 알리면 그 필드에 묶인 위젯 칸만 다음 바인딩 단계에서 갱신됩니다.
     * @details 위젯은 뷰모델을 모르고 바인딩(문서의 `{bind:필드}`)만 압니다. 세터 도우미 `setField` 는 값이 같으면 알리지 않습니다.
     *          알림은 번호(단조 증가)로 남습니다 — 같은 프레임에 같은 필드를 여러 번 알려도 바인딩 단계는 한 번 씁니다. 화면 여럿이 한 뷰모델을 나눠 볼 수 있습니다.
     *
     *          파생은 `REFLECT()` 를 달고 `getTypeInfo()` 를 자기 `StaticType()` 으로 덮어씁니다(위젯과 같은 규칙 — RTTI 없음).
     *          **수명**: 게임이 소유합니다. 묶인 화면보다 먼저 지워지면 그 화면들의 바인딩이 이 뷰모델을 놓습니다(지운 뒤 값은 그대로 남는다). 게임 스레드만.
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

        /** @brief 동적 리플렉션 타입입니다. 파생은 자기 `StaticType()` 을 돌려주도록 덮어씁니다. */
        virtual const TypeInfo* getTypeInfo() const;

        /** @brief 필드 @p field(PROPERTY 이름)가 바뀌었다고 알립니다. 묶인 위젯 칸은 다음 바인딩 단계에서 한 번 갱신됩니다. */
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

        /** @brief 이 뷰모델을 보는 바인딩 집합을 적습니다(지울 때 알린다). */
        void registerObserver( UIBindingSet& bindingSet );
        /** @brief 바인딩 집합을 뗍니다. */
        void unregisterObserver( UIBindingSet& bindingSet );

    private:
        vector<FieldChange>   _listFieldChange; ///< 필드마다 한 줄(중복 없이 — 필드 수만큼만 자란다)
        vector<UIBindingSet*> _listObserver;    ///< 이 뷰모델을 보는 바인딩 집합(지울 때 놓게 한다)
        uint64                _changeSerial;
    };
} // namespace sw
