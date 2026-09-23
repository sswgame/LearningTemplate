/**
 * @file ReflectionMacros.h
 * @brief 도메인에 의존하지 않는 리플렉션 어노테이션입니다(Component / GameObject 를 모릅니다).
 * @details
 * 사용 패턴 요약:
 *   - 일반 타입: REFLECT() + PROPERTY() / FUNCTION()
 *   - 이름 변경 호환: REFLECT(Alias=…) / ENUM(ValueAlias="Old:New, …") / PROPERTY(Alias="hp, HitPoints")
 *   - ENUM(Flags): 비트 연산자를 켭니다. ENUM(Invalid=…, Count=…): TypeRegistry::enumToString 센티널
 *   - 직접 만든 컨테이너: 타입에 REFLECT_CONTAINER(...) 한 번 → 필드는 PROPERTY() 만
 *   - StaticType 필요: REFLECT_BODY()
 *   - std:: / int32 등: ReflectBuiltins.xxx (TypeInfo 는 ReflectBuiltins.gen.cpp)
 *   - 엔진 컴포넌트: REFLECT_BODY() (Component 를 상속하면 팩토리가 자동 등록됩니다)
 */
#pragma once
#include "Core/String/hashed_string.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::generated
{
    /**
     * @brief 생성 코드가 타입마다 특수화하는 등록기입니다. 선언만 여기 있고 정의는 .gen.cpp 가 냅니다.
     * @details 등록기는 `offsetof( Foo, _privateField )` 로 private 멤버를 만지므로 friend 여야 합니다.
     *          예전에는 등록기 이름이 타입마다 달라서(`sw_Foo_Registrar`) 헤더마다 두 줄을 손으로 적었습니다.
     *          클래스 앞의 전방 선언 블록과 클래스 안의 friend 선언입니다. 인자 없는 REFLECT_BODY() 는
     *          그 이름을 만들어 낼 수 없고, 한정된 friend 선언은 이름을 새로 도입하지 않아 전방 선언을
     *          없앨 수도 없었습니다. 등록기를 타입으로 특수화되는 **템플릿**으로 두면 이름이 필요 없어져
     *          `template <typename> friend struct` 한 줄로 REFLECT_BODY() 안에 접힙니다.
     */
    template <typename T>
    struct Registrar;
} // namespace sw::generated

#if defined( __REFLECT_PARSER__ )
    /** @brief 리플렉션 대상 타입을 표시합니다. 선택 플래그: Abstract, Static */
    #define REFLECT( ... )  __attribute__( ( annotate( "REFLECT;" #__VA_ARGS__ ) ) )
    #define PROPERTY( ... ) __attribute__( ( annotate( "PROPERTY;" #__VA_ARGS__ ) ) )
    #define FUNCTION( ... ) __attribute__( ( annotate( "FUNCTION;" #__VA_ARGS__ ) ) )
    #define ENUM( ... )     __attribute__( ( annotate( "ENUM;" #__VA_ARGS__ ) ) )

    /**
     * @brief 컨테이너 타입을 선언할 때 씁니다. 파서가 타입 선언에서 읽어 Sequence/Map 으로 인식합니다.
     * @details 쓰는 쪽(필드)은 PROPERTY() 만 쓰면 됩니다(UPROPERTY + TArray 와 같은 감각).
     *   REFLECT_CONTAINER(Sequence)        → VectorWrapper
     *   REFLECT_CONTAINER(Sequence, List)  → ListWrapper
     *   REFLECT_CONTAINER(Map)             → MapWrapper
     * std:: 등 수정할 수 없는 타입은 ReflectBuiltins.xxx 에 등록합니다.
     */
    #define REFLECT_CONTAINER( ... ) __attribute__( ( annotate( "REFLECT_CONTAINER;" #__VA_ARGS__ ) ) )

    /**
     * @brief GENERATED_BODY 와 같은 역할입니다. .gen.cpp 에 StaticType() 정의를 요청합니다(파서용 마커).
     * @note 본문에 주석을 넣지 마십시오. 주석 줄에 줄 이음(\)이 빠지면 매크로가 거기서 끊기고
     *       나머지 줄이 네임스페이스 스코프로 새어 나갑니다.
     */
    #define REFLECT_BODY()                                                               \
        template <typename>                                                              \
        friend struct ::sw::generated::Registrar;                                        \
        void        __sw_reflect_body() __attribute__( ( annotate( "REFLECT_BODY" ) ) ); \
        const void* swReflectSelf() const
#else

    #define REFLECT( ... )
    #define PROPERTY( ... )
    #define FUNCTION( ... )
    #define ENUM( ... )
    #define REFLECT_CONTAINER( ... )

    /**
     * @brief StaticType() 을 선언합니다. 정의는 .gen.cpp 에 생성됩니다.
     * @note 본문에 주석을 넣지 마십시오. 주석 줄에 줄 이음(\)이 빠지면 매크로가 거기서 끊기고
     *       나머지 줄이 네임스페이스 스코프로 새어 나갑니다.
     */
    #define REFLECT_BODY()                         \
        template <typename>                        \
        friend struct ::sw::generated::Registrar;  \
        static const ::sw::TypeInfo* StaticType(); \
        auto                         swReflectSelf() const -> decltype( this )
#endif

namespace sw
{

    class IPropertyObserver
    {
    public:
        /** @brief 가상 소멸. */
        virtual ~IPropertyObserver() = default;
        /** @brief 프로퍼티가 바뀌면 호출됩니다. */
        virtual void onPropertyChanged( const hashed_string& propertyName ) = 0;
    };

} // namespace sw
