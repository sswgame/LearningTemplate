/**
 * @file CodeGenerator.h
 * @brief 헤더 하나에서 모은 타입 · 열거형으로 .gen.cpp / .gen.h 의 **내용**을 만듭니다(골격은 .tpl, 분기는 CodeEmit).
 * @details 파일에 쓰는 일(내용이 같으면 건너뛰기 · 이름 충돌 검사 · 스탬프)은 `ReflectionPipeline` 이 합니다. 여기는 DTO 를
 *          텍스트로 바꾸기만 하므로 파일 없이 불러 볼 수 있습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/FixedCodecGenerator.h"
#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserSession.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) generate — 파싱 메타 → .gen.cpp / .gen.h 텍스트
    // ------------------------------------------------------------------------------
    class CodeGenerator
    {
    public:
        /**
         * @param fixedCodec 계획을 마친(`FixedCodecGenerator::makePlan`) 고정 배치형 코덱 생성기입니다. 타입마다 코덱 등록과 멤버를 덧붙입니다.
         * @param moduleName 타입 · 열거형이 오르는 런타임 모듈 이름(`--module`, CMake 가 대상마다 정한다)입니다.
         */
        CodeGenerator( const ParsedHeader& header, const FixedCodecGenerator& fixedCodec, const string& sourceFilePath, const ParserSession& session,
                       const string& moduleName );

        /** @brief FQN을 C++ 식별자로 안전하게 바꿉니다. */
        static string sanitizeIdentifier( string_view fqn );

        /** @brief .gen.cpp 의 내용을 만듭니다. 리플렉트된 것이 없으면 한 줄짜리 빈 산출물입니다. */
        string makeSourceText() const;

        /** @brief .gen.h 의 내용(ENUM(Flags) 트레이트)을 만듭니다. 기반 정수 타입을 모르는 ENUM(Flags) 가 있으면 false 입니다. */
        bool makeHeaderText( string& outText ) const;

        /**
         * @brief 개발 전용 단위(`<이름>.gen.meta.cpp`)의 내용 — 에디터 메타데이터(표시 · 편집 필드, 슬라이더 범위)를 채우는 함수와 정적 등록입니다.
         * @details 배포 구성은 이 단위를 짓지 않는다(CMake `sw_add_reflection_step`) — 런타임 메타데이터 구조체의 배치가 구성마다 같다.
         *          언리얼 `WITH_METADATA` 가 같은 칸을 `#if` 로 가르는 자리를 빌드 단위로 가른다. 적은 것이 없으면 한 줄 주석짜리 단위다(산출물 목록은 헤더마다 고정).
         */
        string makeEditorMetadataText() const;

        /** @brief 마지막 `makeSourceText` 가 방출 함수 구역마다 낸 바이트입니다(`ReflectionParser.EmitBytes`). 구역 밖(이름공간 줄 등)은 세지 않는다. */
        const CodeEmitByteReport& getEmitBytes() const { return _emitBytes; }

    private:
        // ------------------------------------------------------------------------------
        // 2) emit — Type/Property/Method/Enum 골격
        // ------------------------------------------------------------------------------
        /** @brief 파일 상단 배너·include 를 출력합니다. */
        void emitFileHeader( CodeEmitBuffer& outBuffer ) const;
        /** @brief 이 헤더의 FUNCTION 가운데 타입 핸들 인자(`ParsedParameterInfo::_handleTargetTypeName`)가 있으면 true 입니다. */
        bool hasHandleArgument() const;
        /** @brief TypeRegistrar 본문을 출력합니다. */
        void emitTypeRegistrar( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo, size_t typeIndex ) const;
        /** @brief 파일 머리에 `PROPERTY()` 값 타입 검사 템플릿(`sw::generated::isPropertyValueAllowed`)을 한 번 냅니다(parser_config 의 금지 기반 목록이 비면 생략). */
        void emitPropertyValueCheck( CodeEmitBuffer& outBuffer ) const;
        /** @brief 서술자 표 앞에 둘 것(값 타입 검사 · 옛 이름 배열 · 중첩 컨테이너의 정적 객체)을 출력합니다. 이름은 @p propIndex 로 가릅니다. */
        void emitPropertyStatics( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, uint32 propIndex ) const;
        /** @brief 서술자 표(`PropertyDesc`) 한 줄을 출력합니다. 칸 차례는 엔진 `PropertyDesc` 와 같습니다. */
        void emitPropertyDesc( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, uint32 propIndex ) const;
        /** @brief 프로퍼티의 선언 타입 식(`decltype( T::m )`, 접근자면 반환 참조의 대상)입니다. */
        static string makePropertyDeclType( const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop );
        /** @brief 프로퍼티 메타데이터(필드 표 + 범위)를 채우기 함수 본문(`m.` 대상)으로 출력합니다. 필드 이름 꼴 범위는 @p typeInfo 의 필드를 읽는 함수가 됩니다. */
        void emitPropertyMetadata( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
        /** @brief 프로퍼티 · 이벤트 하나의 에디터 메타데이터 블록을 개발 전용 단위에 출력합니다(@p pAddFunction 은 `addProperty` · `addEvent`). */
        void emitPropertyEditorMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop, const utf8* pAddFunction ) const;
        /** @brief 타입 하나의 에디터 메타데이터 채우기 함수와 정적 등록을 출력합니다. 적은 것이 없으면 아무것도 쓰지 않고 false 입니다. */
        bool emitTypeEditorMetadata( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const;
        /** @brief 중첩 컨테이너 트리를 단계마다 정적 `const` 객체(`s_nested<색인>_<단계>`)로 출력합니다. */
        void emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop, uint32 propIndex ) const;
        /** @brief 컨테이너 칸의 래퍼 형식이 표(`ReflectBuiltins.xxx` CONTAINER 줄)의 종류 · 역할과 같은지 보는 `static_assert` 를 출력합니다. */
        void emitContainerTableCheck( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
        /** @brief 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 출력합니다. */
        void emitEventList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const;
        /** @brief 메서드 목록을 출력합니다. */
        void emitMethodList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const;
        /** @brief 메서드 호출용 invoker 람다를 출력합니다. */
        void emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedFunctionInfo& method,
                                const string& returnType, const string& callArgs ) const;
        /** @brief 메서드 표의 생성자 줄 끝 칸(`FunctionDesc::_pConstruct`)을 냅니다 — placement new 한 줄짜리 람다, 파서가 더한 기본 생성자는 `ReflectDefaultCtorOf`. */
        void emitConstructor( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedFunctionInfo& method, const string& callArgs ) const;
        /** @brief StaticType / getTypeInfo 접근자를 출력합니다. */
        void emitTypeInfoAccessors( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const;
        /** @brief EnumRegistrar 본문을 출력합니다. */
        void emitEnumRegistrar( CodeEmitBuffer& outBuffer, const ParsedEnumInfo& enumInfo ) const;
        /** @brief 틱 모드 등록 줄(`SW_REGISTER_TICK_TYPE( 타입, 모드 )`)을 냅니다(NEXT 5-4 W-R3 c · 5-3 1c). 대상은 `ParsedTypeInfo::requiresTickRegistration`. */
        void emitTickBatchRegistration( CodeEmitBuffer& outBuffer, const ParsedTypeInfo& typeInfo ) const;

        // ------------------------------------------------------------------------------
        // 3) maps — enumerator·식별자 표기
        // ------------------------------------------------------------------------------
        /**
         * @brief 등록기 특수화(`Registrar<열쇠>`)의 열쇠입니다. 보통 FQN 이고, 숨은 중첩 형식이면 `RegistrarKey_<ID>` 를 @p outKeyDeclaration 의 전방 선언과 함께 돌려줍니다.
         * @details 이름공간 범위의 특수화 머리 · 등록 변수는 바깥 형식의 friend 가 아니라 private 이름을 쓰면 C++17 에서 접근 오류다. 열쇠 구조체로 특수화하면 숨은 이름은
         *          `Registrar` 본문 안(바깥 형식의 `REFLECT_BODY()` 가 주는 template friend)에서만 쓰인다.
         */
        static string makeRegistrarKey( bool bHidden, const string& registrarName, const string& fqn, string& outKeyDeclaration );
        /** @brief 이름 또는 FQN으로 enumerator 를 찾습니다. */
        static const ParsedEnumeratorInfo* findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec );

        /** @brief 로드된 EmitTemplateStore 골격을 렌더해 버퍼에 붙입니다. */
        void appendTemplate( CodeEmitBuffer& outBuffer, EmitTemplate kind, const EmitTemplateStore::TemplateVars vars ) const;

    private:
        const ParsedHeader&        _header;
        const FixedCodecGenerator& _fixedCodec;
        const ParserSession&       _session;
        string                     _sourceFilePath;
        string                     _moduleName; ///< `--module` 그대로 - 경로 글자로 모듈을 추측하지 않는다(런타임 등록 · 내림이 이 이름을 쓴다)
        /// @brief 방출 바이트 계측입니다. 계측은 출력 글을 바꾸지 않아 `const` 방출 함수가 채운다(생성기 하나는 한 스레드가 쓴다).
        mutable CodeEmitByteReport _emitBytes;
    };
} // namespace sw
