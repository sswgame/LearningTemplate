/**
 * @file MemberCollector.h
 * @brief REFLECT 타입 하나의 멤버 — 베이스 · PROPERTY · FUNCTION · 이벤트 · 생성자 · BODY 마커 — 를 한 번의 자식 순회로 모읍니다.
 * @details 오류가 나도 순회를 멈추지 않습니다 — 한 번의 실행이 그 타입의 오류를 모두 알립니다. 오류 난 멤버는 수집 함수가 목록에
 *          넣기 전에 돌아오므로 반쯤 찬 DTO 가 코드젠에 들어가지 않고, `hasError` 가 헤더를 실패로 만듭니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief REFLECT 타입 하나의 멤버를 모으는 상태입니다. 만들고 `collect` → `validate` → `synthesizeDefaultConstructor` 순으로 부릅니다. */
    class MemberCollector
    {
    public:
        MemberCollector( ParsedTypeInfo& type, const ParserSession& session );

        /** @brief @p recordCursor 의 자식을 돌며 멤버를 모읍니다. */
        void collect( CXCursor recordCursor );
        /** @brief 멤버를 다 모은 뒤의 검증입니다. 메서드는 프로퍼티 뒤에 선언될 수 있어 이름으로 가리키는 애노테이션은 여기서 대조합니다. */
        void validate();

        [[nodiscard]] bool hasError() const noexcept { return _bHasError == SW_TRUE; }
        [[nodiscard]] bool isBodyFound() const noexcept { return _bBodyFound == SW_TRUE; }

        static bool isDerivedFromComponent( CXCursor cursor, const ParserConfig& config );

        /**
         * @brief 생성자를 하나도 선언하지 않은 타입(집합체 등)에 `$ctor()` 를 더합니다.
         * @details libclang 은 암시 생성자를 자식 커서로 내지 않아 `collectConstructor` 가 이 타입들을 놓친다. 그러면 `$ctor` 로 만들어 쓰는 곳(설정 창, 스키마 이행)이
         *          이 타입만 못 만든다. 기본 생성이 불가능한 타입(참조 멤버 · 기본 생성자 없는 베이스)인지는 파서가 알 수 없어, 생성 코드가 `if constexpr` 로 가린다.
         */
        void synthesizeDefaultConstructor();

    private:
        /**
         * @brief 베이스 클래스 FQN을 부모로 기록합니다. ParsedTypeInfo 는 부모 하나만 담습니다(단일 상속 체인).
         * @details PropertyInfo 의 오프셋 접근과 캐스트는 "리플렉션 부모는 항상 파생 객체의 byte offset 0 에 있다"는 전제로 동작합니다.
         *          비가상 베이스가 하나뿐이면 C++ ABI 가 offset 0 을 보장하지만, 두 번째 이후 베이스와 가상 베이스는 그렇지 않습니다.
         *          `castTo` 는 리플렉션이 파생이라고 답하고 C++ 상속 관계가 보이지 않으면 `reinterpret_cast` 로 내려가므로(`ReflectionCast.h`)
         *          그런 베이스를 거치면 포인터 보정 없이 잘못된 주소가 됩니다. 그래서 `REFLECT` 타입은 비가상 베이스를 하나까지만 받고,
         *          베이스가 `REFLECT()` 인지와 상관없이 둘째 베이스와 가상 베이스를 생성 오류로 막습니다. 인터페이스는 컴포넌트로 나눠 조합합니다.
         */
        void collectBase( CXCursor cursor );
        /** @brief 인자 이름 · 정규 타입 · 기본 인자를 채웁니다. */
        static void fillParameters( CXCursor cursor, ParsedFunctionInfo& method, const ParserSession& session );
        /** @brief 필드 타입이 리플렉션 이벤트(멀티캐스트 델리게이트)인지 봅니다. */
        static bool isEventType( CXType fieldType );
        /**
         * @brief 이벤트 필드 타입의 함수 타입(`void( int32, const string& )`)을 **적힌 꼴로** 찾습니다.
         * @details 정규 타입에서 꺼내면 인자 타입이 펼쳐진 철자(`sw::basic_string<char>`)라 정규 이름(`string`)이 되지 않는다. 별칭
         *          (`using ScoreEvent = …`)은 벗겨 적힌 템플릿 인자를 읽고, 그래도 못 찾으면 정규 타입으로 물러선다.
         */
        static CXType findEventFunctionType( CXType fieldType );
        /** @brief `PROPERTY()` 가 붙은 멀티캐스트 델리게이트 필드를 이벤트로 수집합니다. */
        void collectEvent( CXCursor cursor, const string& spelling, CXType fieldType );
        /** @brief 필드 선언에서 PROPERTY 메타를 수집합니다. 멀티캐스트 델리게이트면 이벤트로 수집합니다. */
        void collectField( CXCursor cursor );
        /**
         * @brief 값 참조를 돌려주는 메서드에 붙은 PROPERTY 를 "값이 객체 밖에 있는 프로퍼티" 로 수집합니다.
         * @details 값은 객체 안의 필드가 아니라 메서드가 돌려주는 자리에 있습니다(씬 컴포넌트의 로컬 TRS 는 트랜스폼 저장소의 칸).
         *          코드젠은 오프셋 대신 그 메서드를 부르는 접근자를 `PropertyInfo::_pValueAccessor` 에 넣습니다. 그래서 직렬화기 ·
         *          인스펙터 · 비교 도구는 바뀐 것 없이 그 자리를 읽고 씁니다.
         *
         *          모양을 여기서 막습니다. 인자 없는 비정적 메서드여야 하고, 돌려주는 것은 const 가 아닌 lvalue 참조여야 합니다 —
         *          값으로 돌려주면 쓸 자리가 없습니다. 비트필드 · 컨테이너는 받지 않습니다(컨테이너 래퍼 코드젠이 필드 이름을 씁니다).
         */
        void collectAccessorProperty( CXCursor cursor, const string& spelling );
        /** @brief 메서드 · 생성자에 붙은 애노테이션 셋을 한 번의 자식 순회로 봅니다. */
        struct MethodAnnotation;
        static CXChildVisitResult methodAnnotationVisitor( CXCursor cursor, CXCursor, CXClientData data );
        /** @brief REFLECT 타입은 사용자 · 암시 생성자를 자동 등록합니다(FUNCTION 불필요). 복사 · 이동 · 삭제된 것은 뺍니다. */
        void collectConstructor( CXCursor cursor, const string& functionSpelling );
        /** @brief FUNCTION 이 붙은 메서드를 수집합니다. */
        void collectMethod( CXCursor cursor, const string& functionSpelling );
        /** @brief 직접 선언한 재정의(`onTick` · `onSubTick` · `serializeState`)를 타입의 특성 비트로 모읍니다(`ParsedTypeInfo::_bOverridesTick` …). */
        void collectTraits();
        /** @brief 메서드의 이름 · 인자 타입을 적어 둡니다. FUNCTION 이 없어도 — `RepNotify` 처럼 애노테이션이 이름으로 가리킬 수 있다. */
        void recordMethodSignature( CXCursor cursor, const string& name );
        /** @brief 함수 꼴 멤버 하나 — 마커 · 생성자 · 메서드로 나눕니다. */
        void collectFunctionMember( CXCursor cursor, CXCursorKind kind );
        /**
         * @brief REFLECT 타입 멤버를 한 번의 자식 순회로 수집합니다.
         * @details 오류가 나도 순회를 멈추지 않는다 — 한 번의 실행이 그 타입의 오류를 모두 알린다. 오류 난 멤버는 수집 함수가 목록에
         *          넣기 전에 돌아오므로 반쯤 찬 DTO 가 코드젠에 들어가지 않고, `_bHasError` 가 헤더를 실패로 만든다.
         */
        static CXChildVisitResult memberCollectVisitor( CXCursor cursor, CXCursor, CXClientData data );
        struct ComponentSearch;
        /** @brief Component / SceneComponent 파생인지 베이스 체인을 검사합니다. */
        static CXChildVisitResult componentBaseVisitor( CXCursor cursor, CXCursor, CXClientData clientData );

    private:
        ParsedTypeInfo*               _pType;
        const ParserSession*          _pSession;
        vector<ParsedMethodSignature> _listMethodSignature; ///< FUNCTION 여부와 무관하게 이 타입이 선언한 메서드(애노테이션이 이름으로 가리킬 때 대조)
        uint32                        _baseCount;
        uint8                         _bSkipConstructors : 1; ///< Abstract / Static 타입은 생성할 수 없다(Unreal UCLASS(Abstract))
        uint8                         _bBodyFound        : 1;
        uint8                         _bHasError         : 1;
        uint8                         _bBusBody          : 1; ///< `kBusVersion` 멤버가 있다(서버 버스 본문 타입)
        uint8                         _bExported         : 1; ///< 레코드에 dllexport · dllimport · visibility 속성이 있다(모듈 내보내기 매크로)
        [[maybe_unused]] uint8        _reserved          : 3;
    };
} // namespace sw
