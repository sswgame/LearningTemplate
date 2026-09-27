/**
 * @file CodeGenerator.h
 * @brief 헤더 하나에서 모은 타입 · 열거형으로 .gen.cpp / .gen.h 의 **내용**을 만듭니다(골격은 .tpl, 분기는 CodeEmit).
 * @details 파일에 쓰는 일(내용이 같으면 건너뛰기 · 이름 충돌 검사 · 스탬프)은 `ReflectionPipeline` 이 합니다. 여기는 DTO 를
 *          텍스트로 바꾸기만 하므로 파일 없이 불러 볼 수 있습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/CodeEmit.h"
#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserSession.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) generate — 파싱 메타 → .gen.cpp / .gen.h 텍스트
    // ------------------------------------------------------------------------------
    class CodeGenerator
    {
    public:
        /**
         * @param sourceRoot 모듈 판별을 이 경로 기준 상대 경로로 합니다. 비면 전체 경로로 매칭합니다.
         */
        CodeGenerator( const ParsedHeader& header, const string& sourceFilePath, const ParserSession& session,
                       const string& sourceRoot = string{} );

        /** @brief .gen.cpp 의 내용을 만듭니다. 리플렉트된 것이 없으면 한 줄짜리 빈 산출물입니다. */
        string makeSourceText() const;

        /** @brief .gen.h 의 내용(ENUM(Flags) 트레이트)을 만듭니다. 기반 정수 타입을 모르는 ENUM(Flags) 가 있으면 false 입니다. */
        bool makeHeaderText( string& outText ) const;

    private:
        // ------------------------------------------------------------------------------
        // 2) emit — Type/Property/Method/Enum 골격
        // ------------------------------------------------------------------------------
        /** @brief 파일 상단 배너·include 를 출력합니다. */
        void emitFileHeader( CodeEmitBuffer& out ) const;
        /** @brief TypeRegistrar 본문을 출력합니다. */
        void emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
        /** @brief PropertyInfo 한 항목을 출력합니다. */
        void emitPropertyInfoEntry( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
        /** @brief 프로퍼티 메타데이터(필드 표 + 범위)를 출력합니다. */
        void emitPropertyMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop ) const;
        /** @brief 중첩 컨테이너 트리를 출력합니다. */
        void emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
        /** @brief 메서드 목록을 출력합니다. */
        void emitMethodList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const;
        /** @brief 메서드 호출용 invoker 람다를 출력합니다. */
        void emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo, const ParsedFunctionInfo& method,
                                const string& retType, const string& callArgs ) const;
        /** @brief ReflectTypeTraits 특수화를 출력합니다. */
        void emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
        /** @brief StaticType / getTypeInfo 접근자를 출력합니다. */
        void emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
        /** @brief ComponentFactoryRegistrar 를 출력합니다. */
        void emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
        /** @brief EnumRegistrar 본문을 출력합니다. */
        void emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const;
        /**
         * @brief 소스 파일 경로로부터 모듈 이름을 판별합니다(생성자에서 한 번).
         * @details parser_config 의 parsing.module_rules 를 위에서부터 적용합니다. 매칭은 sourceRoot 기준 상대 경로로
         *          하므로, 리포지토리를 어디에 두든 결과가 같습니다.
         */
        string makeModuleName( const string& sourceRoot ) const;

        // ------------------------------------------------------------------------------
        // 3) maps — enumerator·식별자 표기
        // ------------------------------------------------------------------------------
        /** @brief 이름 또는 FQN으로 enumerator 를 찾습니다. */
        static const ParsedEnumeratorInfo* findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec );

        /** @brief FQN을 C++ 식별자로 안전하게 바꿉니다. */
        static string sanitizeIdentifier( string_view fqn );

        /** @brief 로드된 EmitTemplateStore 골격을 렌더해 버퍼에 붙입니다. */
        void appendTemplate( CodeEmitBuffer& out, const string_view name, const EmitTemplateStore::TemplateVars vars ) const;

    private:
        const ParsedHeader&  _header;
        const ParserSession& _session;
        string               _sourceFilePath;
        string               _moduleName; ///< 타입 · 열거형 · 팩토리 등록마다 쓰던 것을 한 번만 판별한다
    };
} // namespace sw
