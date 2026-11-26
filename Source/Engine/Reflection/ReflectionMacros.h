/**
 * @file ReflectionMacros.h
 * @brief 도메인에 의존하지 않는 리플렉션 어노테이션입니다(Component / GameObject 를 모릅니다).
 * @details
 * 사용 패턴 요약:
 *   - 일반 타입: REFLECT() + PROPERTY() / FUNCTION()
 *   - 이름 변경 창구: REFLECT(Alias=…) · ENUM(Alias=…) / ENUM(ValueAlias="Old:New, …") / ENUM(Removed="Old:Replacement") / PROPERTY(Alias="hp, HitPoints")
 *     실제 게임 데이터가 생긴 뒤 이름을 바꾸거나 지울 때만 씁니다. 그 전에는 이름을 바꾸고 데이터를 새 이름으로 다시 씁니다(엔진 · 게임 코드에 별칭이 없습니다).
 *     옛 이름은 읽기 호환용이라 고정 배치형 스키마 해시에 들어가지 않고, 선언 없이 지우거나 바꾼 이름은 읽기가 실패합니다.
 *   - 열거자 표시 메타: `Red ENUMERATOR( DisplayName = "빨강", Tooltip = "…", Hidden ),` (언리얼 UMETA)
 *   - ENUM(Flags): 비트 연산자를 켭니다. ENUM(Invalid=…, Count=…): TypeRegistry::enumToString 센티널. 끝 열거자 `Count` 는 적지 않아도 Count 센티널이다
 *   - 직접 만든 컨테이너: 타입에 REFLECT_CONTAINER(...) 한 번 → 필드는 PROPERTY() 만
 *   - StaticType 필요: REFLECT_BODY()
 *   - std:: / int32 등: ReflectBuiltins.xxx (TypeInfo 는 ReflectBuiltins.gen.cpp)
 *   - 엔진 컴포넌트: REFLECT_BODY() (Component 를 상속하면 팩토리가 자동 등록됩니다)
 *   - 상태 선언: 컴포넌트와 상태 칸(RuntimeState · SaveGame)을 가진 형식의 값 멤버는 PROPERTY · SW_CODEC_STATE · SW_TRANSIENT 중 하나를 적습니다.
 */
#pragma once
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/TypeLookupCache.h"

namespace sw
{
    struct TypeInfo;
    /**
     * @brief 리플렉트 형식의 숨은 멤버에 닿는 엔진 쪽 통로입니다. `REFLECT_BODY()` 가 이 템플릿을 통째로 친구로 연다.
     * @details 쓰는 쪽이 태그마다 특수화한다 — 일괄 틱(`TickAccessTag`, `TickBatchRegistry.h`).
     *          서브시스템이 늘어도 REFLECT_BODY 의 친구 줄이 늘지 않는다(boost.serialization · cereal 의 `access` 와 같은 꼴). 특수화는 한정 이름 호출
     *          (`comp.T::onTick( … )`)로 비가상 호출을 그대로 둔다 — 멤버 포인터로 넘기면 가상 디스패치가 된다.
     */
    template <typename TTag>
    struct ReflectAccess;

    /**
     * @brief 리플렉션 인터페이스의 포트 멤버(`REFLECT_INTERFACE( I, _port )`)에서 주인 객체로 돌아갑니다. @p portOffset 은 `offsetof( Owner, _port )` 입니다.
     * @details 포트가 주인 포인터를 들지 않는다 — 오프셋 하나로 되돌린다(리눅스 `container_of` · CppStyle 의 "nested port forwards to the owner").
     *          const 포트면 @p TOwner 도 const 로 준다: `getPortOwner<const Owner>( *this, offsetof( Owner, _port ) )`.
     */
    template <typename TOwner, typename TPort>
    TOwner& getPortOwner( TPort& port, size_t portOffset ) noexcept
    {
        static_assert( std::is_const_v<TPort> == false || std::is_const_v<TOwner>, "a const port returns a const owner" );
        using Byte = std::conditional_t<std::is_const_v<TPort>, const uint8, uint8>;
        return *reinterpret_cast<TOwner*>( reinterpret_cast<Byte*>( &port ) - portOffset );
    }
} // namespace sw

namespace sw::generated
{
    /**
     * @brief 생성 코드가 타입마다 특수화하는 등록기입니다. 선언만 여기 있고 정의는 .gen.cpp 가 냅니다.
     * @details 등록기는 `offsetof( Foo, _privateField )` 로 private 멤버를 만지므로 friend 여야 합니다.
     *          등록기를 타입으로 특수화되는 **템플릿**으로 두면 타입마다 이름을 만들 필요가 없어
     *          `template <typename> friend struct` 한 줄로 REFLECT_BODY() 안에 접힙니다(전방 선언도 필요 없습니다).
     */
    template <typename T>
    struct Registrar;
} // namespace sw::generated

#if defined( __REFLECT_PARSER__ )
    /**
     * @brief 리플렉션 대상 타입을 표시합니다. 선택 플래그: Abstract, Static
     * @details 형식 앞 매크로(REFLECT · ENUM · REFLECT_CONTAINER)는 파서 모드에서 **표식 선언**입니다. 형식 키워드 앞의 GNU 속성은 clang 이 형식
     *          선언에서 버린다(`-Wignored-attributes`) — 파서는 표식을 바로 다음 형제 선언과 짝짓는다(`AnnotationLookup`). `static_assert` 는
     *          이름공간 · 클래스 범위 어디서나 이름 없이 놓이고, `template` 앞이면 짝이 클래스 템플릿이라 그 자리에서 오류로 알린다.
     *          멤버 · 열거자 매크로(PROPERTY · FUNCTION · ENUMERATOR)는 선언에 붙는 속성 그대로다.
     */
    #define REFLECT( ... )  static_assert( true, "REFLECT;" #__VA_ARGS__ );
    #define PROPERTY( ... ) __attribute__( ( annotate( "PROPERTY;" #__VA_ARGS__ ) ) )
    #define FUNCTION( ... ) __attribute__( ( annotate( "FUNCTION;" #__VA_ARGS__ ) ) )
    #define ENUM( ... )     static_assert( true, "ENUM;" #__VA_ARGS__ );
    /**
     * @brief 되살리지 않는 값 멤버입니다(캐시 · 이번 프레임 값 · 다시 세우는 리소스 · 바깥이 다시 넣는 값). 저장 · 스트리밍 · 핫 리로드가 싣지 않아도 같은 게임이 이어지는 것만 적습니다.
     * @details 컴포넌트와 상태 칸(`RuntimeState` · `SaveGame`)을 가진 형식에서 선언이 없는 값 멤버는 생성 오류다 — 새 상태가 조용히 빠지지 않는다. 언리얼 `UPROPERTY( Transient )` ·
     *          유니티 `[NonSerialized]` 자리이되 리플렉션 칸을 만들지 않는다. 래퍼 형식(`Transient<T>`)이 아니라 애노테이션인 까닭: 비트필드를 감쌀 수 없고, 컴파일에서 빈 글이라 크기 · 접근 비용이 같다.
     */
    #define SW_TRANSIENT __attribute__( ( annotate( "SW_TRANSIENT" ) ) )
    /** @brief 이 형식(이나 기반)의 `serializeState` · `serializeGameState` 가 싣는 값 멤버입니다. 그 함수가 없는 형식에 적으면 생성 오류입니다. */
    #define SW_CODEC_STATE __attribute__( ( annotate( "SW_CODEC_STATE" ) ) )
    /** @brief 열거자 하나의 표시 메타입니다(언리얼 `UMETA`). 열거자 이름 뒤에 적습니다: `Red ENUMERATOR( DisplayName = "빨강", Hidden ),` */
    #define ENUMERATOR( ... ) __attribute__( ( annotate( "ENUMERATOR;" #__VA_ARGS__ ) ) )

    /**
     * @brief 컨테이너 타입을 선언할 때 씁니다. 파서가 타입 선언에서 읽어 Sequence/Map 으로 인식합니다.
     * @details 쓰는 쪽(필드)은 PROPERTY() 만 쓰면 됩니다(UPROPERTY + TArray 와 같은 감각).
     *   REFLECT_CONTAINER(Sequence)        → VectorWrapper
     *   REFLECT_CONTAINER(Sequence, Deque) → DequeWrapper
     *   REFLECT_CONTAINER(Map)             → MapWrapper
     * std:: 등 수정할 수 없는 타입은 ReflectBuiltins.xxx 에 등록합니다.
     */
    #define REFLECT_CONTAINER( ... ) static_assert( true, "REFLECT_CONTAINER;" #__VA_ARGS__ );

    /**
     * @brief 컴포넌트가 리플렉션 인터페이스 @p InterfaceType 을 포트 멤버 @p PortMember 로 내보냅니다(`TypeInfo::_ownInterfaces` 한 칸, `GameObject::findInterface<I>()` 가 찾는다).
     * @details 언리얼 `UINTERFACE` 다중 상속 대신 포트 멤버다 — 기반은 하나(CppStyle), 포트는 주인에게 넘긴다(`SW_CONTAINER_OF`). 파서 모드에서는 이름 있는 표식 함수로
     *          선언해 같은 형식 안의 여러 줄을 멤버 수집이 차례로 읽는다. 생성 코드가 포트가 그 인터페이스를 잇는지(`std::is_base_of_v`) 컴파일에서 본다.
     */
    #define REFLECT_INTERFACE( InterfaceType, PortMember ) void __sw_reflect_interface_##PortMember() __attribute__( ( annotate( "REFLECT_INTERFACE;" #InterfaceType "," #PortMember ) ) )

    /**
     * @brief GENERATED_BODY 와 같은 역할입니다. .gen.cpp 에 StaticType() 정의를 요청합니다(파서용 마커).
     * @note 본문에 주석을 넣지 마십시오. 주석 줄에 줄 이음(\)이 빠지면 매크로가 거기서 끊기고
     *       나머지 줄이 네임스페이스 스코프로 새어 나갑니다.
     */
    #define REFLECT_BODY()                                                               \
        template <typename>                                                              \
        friend struct ::sw::generated::Registrar;                                        \
        template <typename>                                                              \
        friend struct ::sw::ReflectAccess;                                               \
        void        __sw_reflect_body() __attribute__( ( annotate( "REFLECT_BODY" ) ) ); \
        const void* swReflectSelf() const
#else

    #define REFLECT( ... )
    #define PROPERTY( ... )
    #define FUNCTION( ... )
    #define ENUM( ... )
    #define ENUMERATOR( ... )
    #define SW_TRANSIENT
    #define SW_CODEC_STATE
    #define REFLECT_CONTAINER( ... )
    #define REFLECT_INTERFACE( InterfaceType, PortMember ) static_assert( true, "REFLECT_INTERFACE" )

    /**
     * @brief StaticType() 을 인라인으로 정의합니다. 적중 경로는 클래스의 캐시 칸(`swStaticTypeCache`)을 세대 비교 하나로 읽고, 놓치면 .gen.cpp 의 `swFindStaticType` 이 찾습니다.
     * @details 캐스트(`castTo<T>`)가 매번 생성 .cpp 로 가는 호출(게임 타입이면 DLL 경계)을 치르지 않는다 — 언리얼 `StaticClass()` 와 같은 자리. 핫 리로드 · 등록 · 해제는
     *          세대를 올려 다음 한 번만 다시 찾는다(`TypeLookupCache`). 모듈마다 칸의 사본이 생겨도 저마다 같은 답을 찾는다.
     * @note 본문에 주석을 넣지 마십시오. 주석 줄에 줄 이음(\)이 빠지면 매크로가 거기서 끊기고
     *       나머지 줄이 네임스페이스 스코프로 새어 나갑니다.
     */
    #define REFLECT_BODY()                                                                           \
        template <typename>                                                                          \
        friend struct ::sw::generated::Registrar;                                                    \
        template <typename>                                                                          \
        friend struct ::sw::ReflectAccess;                                                           \
        static const ::sw::TypeInfo* StaticType()                                                    \
        {                                                                                            \
            const ::sw::TypeInfo* pCachedType = nullptr;                                             \
            return swStaticTypeCache.tryGetCached( pCachedType ) ? pCachedType : swFindStaticType(); \
        }                                                                                            \
        static const ::sw::TypeInfo*              swFindStaticType();                                \
        static inline const ::sw::TypeLookupCache swStaticTypeCache{};                               \
        auto                                      swReflectSelf() const -> decltype( this )
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

// 편집기에서만 인자 키 완성 · 오타 오류를 준다 — clangd(.clangd 가 SW_REFLECT_INTELLISENSE 를 정의)만 읽는다. 빌드와 생성기는 읽지 않는다.
// Visual Studio IntelliSense(EDG, __INTELLISENSE__)는 읽지 않는다 — 풀이가 MS 호환 모드의 dependent base 찾기 · `_Pragma( "clang diagnostic" )` ·
// static_assert 안의 제네릭 람다 지역 클래스에 기대는데 EDG 가 받는지는 사람이 VS 를 열어 봐야만 알 수 있다(오류 목록이 자동화로 안 나온다).
// 그쪽은 매크로가 빈 것으로 보인다(언리얼 UPROPERTY 가 IntelliSense 에 빈 매크로인 것과 같다).
#if !defined( __REFLECT_PARSER__ ) && defined( SW_REFLECT_INTELLISENSE )
    #include "Engine/Reflection/ReflectionIntellisense.h"
#endif
