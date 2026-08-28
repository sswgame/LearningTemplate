/**
 * @file CodeGenerator.h
 * @brief 파싱된 타입/열거형 메타데이터로부터 .gen.cpp 생성 (골격=.tpl, 분기=CodeEmit)
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/CodeEmit.h"
#include "ReflectionParser/ParsedReflection.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) generate — 파싱 메타 → .gen.cpp / .gen.h
	// ------------------------------------------------------------------------------
	class CodeGenerator
	{
	public:
		/** @brief 파싱된 타입·열거형과 입출력 경로로 생성기를 구성합니다. */
		CodeGenerator(
			const vector<ParsedTypeInfo>& types,
			const vector<ParsedEnumInfo>& enums,
			const string&				  sourceFilePath,
			const string&				  outputDir );

		/** @brief .gen.cpp / .gen.h 를 생성합니다. */
		bool generate();
		/** @brief 생성된 .gen.cpp 경로를 반환합니다. */
		const string& getOutputFilePath() const { return _outputFilePath; }
		/** @brief 생성된 .gen.h 경로를 반환합니다. */
		const string& getOutputHeaderPath() const { return _outputHeaderPath; }

	private:
		// ------------------------------------------------------------------------------
		// 2) emit — Type/Property/Method/Enum 골격
		// ------------------------------------------------------------------------------
		/** @brief 파일 상단 배너·include 를 출력합니다. */
		void emitFileHeader( CodeEmitBuffer& out ) const;
		/** @brief TypeRegistrar 본문을 출력합니다. */
		void emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
		/** @brief PropertyInfo 한 항목을 출력합니다. */
		void emitPropertyInfoEntry( CodeEmit& e, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
		/** @brief 프로퍼티 메타데이터(카테고리·별칭 등)를 출력합니다. */
		void emitPropertyMetadata( CodeEmit& e, const ParsedPropertyInfo& prop ) const;
		/** @brief 중첩 컨테이너 트리를 출력합니다. */
		void emitNestedContainerTree( CodeEmit& e, const ParsedTypeInfo& typeInfo, const ParsedPropertyInfo& prop ) const;
		/** @brief 메서드 목록을 출력합니다. */
		void emitMethodList( CodeEmit& e, const ParsedTypeInfo& typeInfo ) const;
		/** @brief 메서드 호출용 invoker 람다를 출력합니다. */
		void emitMethodInvoker( CodeEmit& e, const ParsedTypeInfo& typeInfo, const ParsedFunctionInfo& method,
								const string& retType, const string& callArgs ) const;
		/** @brief ReflectTypeTraits 특화를 출력합니다. */
		void emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
		/** @brief StaticType / getTypeInfo 접근자를 출력합니다. */
		void emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
		/** @brief ComponentFactoryRegistrar 를 출력합니다. */
		void emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
		/** @brief EnumRegistrar 본문을 출력합니다. */
		void emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const;
		/** @brief 소스 파일 경로로부터 모듈 이름을 판별합니다. */
		string getModuleName() const;
		/** @brief 동반 .gen.h 를 씁니다. */
		bool emitGeneratedHeader() const;

		// ------------------------------------------------------------------------------
		// 3) maps — enumerator·식별자·ContainerKind 표기
		// ------------------------------------------------------------------------------
		/** @brief 이름 또는 FQN으로 enumerator 를 찾습니다. */
		static const ParsedEnumeratorInfo* findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec );

		/** @brief FQN을 C++ 식별자로 안전하게 바꿉니다. */
		static string sanitizeIdentifier( string_view fqn );
		/** @brief ContainerKind 의 C++ 표현식을 반환합니다. */
		static const utf8* containerKindExpr( ContainerKind kind );
		/** @brief 컨테이너 peel 멤버 이름을 반환합니다. */
		static const utf8* peelMember( ContainerKind kind );

		/** @brief 로드된 EmitTemplateStore 골격을 렌더해 버퍼에 붙입니다. */
		static void appendTemplate( CodeEmitBuffer& out, const string_view name,
									const unordered_map<string, string>& vars );
		static void appendTemplate( CodeEmitBuffer& out, const string_view name,
									std::initializer_list<std::pair<string_view, string_view>> vars );

	private:
		const vector<ParsedTypeInfo>& _listType;
		const vector<ParsedEnumInfo>& _listEnum;
		string						  _sourceFilePath;
		string						  _outputDir;
		string						  _outputFilePath;
		string						  _outputHeaderPath;
	};
} // namespace sw
