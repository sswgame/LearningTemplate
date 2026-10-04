/**
 * @file ReflectionInvoke.h
 * @brief 타입을 모른 채 이름으로 함수를 부르고, 이벤트를 묶고 · 부릅니다(콘솔 명령 · 비주얼 스크립팅 · 기믹 배선 · 에디터).
 * @details 인자는 `ReflectValue`(타입 이름이 붙은 값) 목록입니다. 인자 자리마다 그 타입의 변환(`ReflectTypeOps`)이 값을 바꿔 넣으므로,
 *          글 "35" 를 `int32` 인자에, `int32` 를 `float32` 인자에 넘길 수 있습니다. 빠진 뒤쪽 인자는 기본 인자로 채웁니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    /** @brief 리플렉션 호출 · 이벤트 부르기의 결과입니다. */
    enum class ReflectCallResult : uint8
    {
        Ok,
        NotBound,         ///< 호출기 · 이벤트 표가 없다
        NullInstance,     ///< 정적 함수가 아닌데 인스턴스가 없다
        TooManyArguments, ///< 인자가 시그니처보다 많다
        MissingArgument,  ///< 기본 인자가 없는 인자를 빼먹었다
        ArgumentMismatch, ///< 인자를 그 타입으로 바꿀 수 없다
    };

    /** @brief 결과의 이름(로그 · 콘솔 출력용)입니다. */
    SW_API const utf8* toString( ReflectCallResult result );

    /** @brief 이벤트가 불릴 때 받는 쪽입니다. 인자는 이벤트 시그니처 순서대로, 각자 자기 타입 이름을 달고 옵니다. */
    using ReflectEventHandler = Delegate<void( const vector<ReflectValue>& listArg )>;
} // namespace sw

namespace sw
{
    /** @brief 이벤트 필드 타입 하나의 묶기 · 풀기 · 부르기 · 인자 표입니다. `ReflectEventOpsOf<필드 타입>::kOps` 가 만듭니다. */
    struct ReflectEventOps
    {
        DelegateHandle ( *_pBind )( void* pEvent, const ReflectEventHandler& handler );
        void ( *_pUnbind )( void* pEvent, const DelegateHandle& handle );
        ReflectCallResult ( *_pBroadcast )( void* pEvent, const vector<ReflectValue>& listArg );
        bool ( *_pIsBound )( const void* pEvent );
        const ReflectTypeOps* const* _ppParameterType; ///< 인자 자리마다 변환 표
        uint32                       _parameterCount;
    };

    /** @brief 이벤트로 받는 필드 타입입니다. `MulticastDelegate<void( Args... )>` 만 특수화합니다(그 밖의 타입은 코드젠이 이벤트로 수집하지 않는다). */
    template <typename TEvent>
    struct ReflectEventOpsOf;

    template <typename... Args>
    struct ReflectEventOpsOf<MulticastDelegate<void( Args... )>>
    {
        using EventType = MulticastDelegate<void( Args... )>;

        static constexpr uint32                kParameterCount                          = static_cast<uint32>( sizeof...( Args ) );
        static constexpr const ReflectTypeOps* kArrParameterType[sizeof...( Args ) + 1] = { &ReflectTypeOpsOf<std::decay_t<Args>>::kOps..., nullptr };

        /** @brief 받는 쪽을 붙입니다. 인자는 값으로 복사해 `ReflectValue` 로 넘깁니다. */
        static DelegateHandle bind( void* pEvent, const ReflectEventHandler& handler )
        {
            const auto adapter = [handler]( Args... args )
            {
                vector<ReflectValue> listArg;
                listArg.reserve( sizeof...( Args ) );
                ( listArg.push_back( ReflectValue::make<std::decay_t<Args>>( args ) ), ... );
                handler( listArg );
            };
            return static_cast<EventType*>( pEvent )->add( Delegate<void( Args... )>::create( adapter ) );
        }

        static void unbind( void* pEvent, const DelegateHandle& handle ) { static_cast<EventType*>( pEvent )->remove( handle ); }

        static bool isBound( const void* pEvent ) { return static_cast<const EventType*>( pEvent )->isBound(); }

        static ReflectCallResult broadcast( void* pEvent, const vector<ReflectValue>& listArg )
        {
            return broadcastWith( pEvent, listArg, std::index_sequence_for<Args...>{} );
        }

        static constexpr ReflectEventOps kOps{ &bind, &unbind, &broadcast, &isBound, kArrParameterType, kParameterCount };

    private:
        /** @brief 인자마다 그 타입으로 바꾼 뒤 부릅니다. 하나라도 못 바꾸면 부르지 않습니다. */
        template <size_t... Index>
        static ReflectCallResult broadcastWith( void* pEvent, const vector<ReflectValue>& listArg, std::index_sequence<Index...> )
        {
            if ( listArg.size() > sizeof...( Args ) )
                return ReflectCallResult::TooManyArguments;
            if ( listArg.size() < sizeof...( Args ) )
                return ReflectCallResult::MissingArgument;
            TaskValue arrValue[sizeof...( Args ) + 1];
            bool      bConverted = true;
            ( ( bConverted = bConverted && ReflectTypeOpsOf<std::decay_t<Args>>::convert( listArg[Index], arrValue[Index] ) ), ... );
            if ( bConverted == false )
                return ReflectCallResult::ArgumentMismatch;
            static_cast<EventType*>( pEvent )->broadcast( *arrValue[Index].template getPtr<std::decay_t<Args>>()... );
            return ReflectCallResult::Ok;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 리플렉션 함수 · 이벤트를 타입을 모른 채 다루는 길입니다.
     * @details 같은 규칙 하나: 인자 자리마다 `FunctionParameterInfo::_pType->_pConvert` 가 값을 바꿉니다(같은 타입은 그대로 · 숫자끼리 ·
     *          글 → 값 · 열거형 이름). 인자가 모자라면 기본 인자를, 기본 인자도 없으면 `MissingArgument` 입니다.
     */
    struct SW_API ReflectionInvoke
    {
        /**
         * @brief 함수를 부릅니다.
         * @param pInstance 정적 함수면 nullptr 이어도 됩니다
         * @param pOutResult 있으면 반환값을 담습니다(void 면 빈 값)
         */
        static ReflectCallResult call( const FunctionInfo& function, void* pInstance, const vector<ReflectValue>& listArg, ReflectValue* pOutResult = nullptr );
        /** @brief 인자를 글로 받아 부릅니다(콘솔 명령). 글마다 그 인자 타입으로 읽습니다. */
        static ReflectCallResult callWithText( const FunctionInfo& function, void* pInstance, const vector<string>& listArgText, ReflectValue* pOutResult = nullptr );
        /** @brief 타입 사슬에서 이름으로 함수를 찾아 부릅니다. 없으면 `NotBound` 입니다. */
        static ReflectCallResult callByName( const TypeInfo& type, void* pInstance, const hashed_string& functionName, const vector<ReflectValue>& listArg,
                                             ReflectValue* pOutResult = nullptr );
        /** @brief 인자를 그 함수의 C++ 타입대로 바꿔 묶습니다(`call` 의 앞 절반). 호출기를 직접 부를 때 씁니다. */
        static ReflectCallResult makeArguments( const FunctionInfo& function, const vector<ReflectValue>& listArg, TaskArgs& outArgs );

        /** @brief 이벤트에 받는 쪽을 붙이고 핸들을 돌려줍니다(풀 때 씁니다). 표가 없으면 빈 핸들입니다. */
        static DelegateHandle bindEvent( const EventInfo& event, void* pInstance, const ReflectEventHandler& handler );
        /** @brief 붙인 것을 뗍니다. */
        static void unbindEvent( const EventInfo& event, void* pInstance, const DelegateHandle& handle );
        /** @brief 이벤트를 부릅니다 — 인자는 이벤트 타입대로 바꿉니다. */
        static ReflectCallResult broadcastEvent( const EventInfo& event, void* pInstance, const vector<ReflectValue>& listArg );
        /** @brief 붙은 받는 쪽이 하나라도 있으면 true 입니다. */
        static bool isEventBound( const EventInfo& event, const void* pInstance );

        /**
         * @brief 이벤트 인자를 함수 인자로 넘길 수 있는지 봅니다 — 함수의 인자마다 같은 자리의 이벤트 인자가 있고 바꿀 수 있거나, 기본 인자가 있다.
         * @details 함수가 이벤트 인자 뒤쪽을 덜 받는 것은 됩니다(앞쪽부터 넘긴다). 타입 판정은 이름이 같거나, 둘 다 숫자 · bool 이거나, 받는 쪽이 글입니다.
         */
        static bool canBindEventToFunction( const EventInfo& event, const FunctionInfo& function );
        /**
         * @brief 이벤트가 불리면 @p pTarget 의 함수를 부르게 묶습니다(기믹 배선 · 비주얼 스크립팅). 함수는 부를 때마다 이름으로 찾습니다 —
         *        모듈을 다시 올려도 묶음이 옛 코드를 가리키지 않습니다.
         * @warning @p pTarget 의 수명은 부르는 쪽이 지킵니다. 먼저 사라지면 돌려받은 핸들로 떼십시오.
         * @return 넘길 수 없으면(`canBindEventToFunction`) 빈 핸들입니다
         */
        static DelegateHandle bindEventToFunction( const EventInfo& event, void* pSource, const TypeInfo& targetType, const hashed_string& functionName,
                                                   void* pTarget );
    };
} // namespace sw
