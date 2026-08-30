/**
 * @file ReflectionMacros.h
 * @brief 도메인 비의존 리플렉션 어노테이션 (Component / GameObject 모름).
 * @details
 * 사용 패턴 요약:
 *   - 일반 타입: REFLECT() + PROPERTY() / FUNCTION()
 *   - 개명 호환: REFLECT(Alias=…) / ENUM(ValueAlias="Old:New, …") / PROPERTY(Alias="hp, HitPoints")
 *   - ENUM(Flags): 비트 연산자 생성. ENUM(Invalid=…, Count=…): TypeRegistry::enumToString 센티널
 *   - 내 컨테이너: 타입에 REFLECT_CONTAINER(...) 한 번 → 필드는 PROPERTY()만
 *   - StaticType 필요: REFLECT_BODY()
 *   - std:: / int32 등: ReflectBuiltins.h (TypeInfo는 ReflectBuiltins.gen.cpp)
 *   - 엔진 컴포넌트: REFLECT_BODY() (Component 상속 시 자동 팩토리 등록)
 */
#pragma once
#include "Core/String/hashed_string.h"

namespace sw
{
	struct TypeInfo;
} // namespace sw

#if defined( __REFLECT_PARSER__ )
	/** @brief 타입 리플렉션 대상. 선택 플래그: Abstract, Static */
	#define REFLECT( ... )	__attribute__( ( annotate( "REFLECT;" #__VA_ARGS__ ) ) )
	#define PROPERTY( ... ) __attribute__( ( annotate( "PROPERTY;" #__VA_ARGS__ ) ) )
	#define FUNCTION( ... ) __attribute__( ( annotate( "FUNCTION;" #__VA_ARGS__ ) ) )
	#define ENUM( ... )		__attribute__( ( annotate( "ENUM;" #__VA_ARGS__ ) ) )

	/**
	 * @brief 컨테이너 타입 선언용. 파서가 타입 선언에서 읽어 Sequence/Map으로 인식한다.
	 * @details 사용처(필드)는 PROPERTY()만 쓰면 된다 (UPROPERTY + TArray와 같은 감각).
	 *   REFLECT_CONTAINER(Sequence)        → VectorWrapper
	 *   REFLECT_CONTAINER(Sequence, List)  → ListWrapper
	 *   REFLECT_CONTAINER(Map)             → MapWrapper
	 * std:: 등 수정 불가 타입은 ReflectBuiltins.h 에 등록한다.
	 */
	#define REFLECT_CONTAINER( ... ) __attribute__( ( annotate( "REFLECT_CONTAINER;" #__VA_ARGS__ ) ) )

	/**
	 * @brief GENERATED_BODY 역할 — .gen.cpp에 StaticType() 정의를 요청 (파서용 마커).
	 * @note 본문에 주석을 넣지 말 것. 주석 줄에 줄바꿈 이음(\)이 빠지면 매크로가 거기서 끊기고
	 *       나머지 줄이 네임스페이스 스코프로 새어 나간다.
	 */
	#define REFLECT_BODY()                                                               \
		void		__sw_reflect_body() __attribute__( ( annotate( "REFLECT_BODY" ) ) ); \
		const void* swReflectSelf() const
#else

	#define REFLECT( ... )
	#define PROPERTY( ... )
	#define FUNCTION( ... )
	#define ENUM( ... )
	#define REFLECT_CONTAINER( ... )

	/**
	 * @brief StaticType() 선언. 정의는 .gen.cpp에 emit된다.
	 * @note 본문에 주석을 넣지 말 것. 주석 줄에 줄바꿈 이음(\)이 빠지면 매크로가 거기서 끊기고
	 *       나머지 줄이 네임스페이스 스코프로 새어 나간다.
	 */
	#define REFLECT_BODY()                         \
		static const ::sw::TypeInfo* StaticType(); \
		auto						 swReflectSelf() const -> decltype( this )
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
