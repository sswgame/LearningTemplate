#pragma once

/**
 * @file CodeGenerator.h
 * @brief 파싱된 타입/열거형 메타데이터로부터 .gen.cpp 생성
 */

#include "AstVisitor.h"
#include "Core/CoreMinimal.h"
#include "Core/Utility/String/StringBuilder.h"

namespace sw::tool
{
	using CodeEmitBuffer = StringBuilder<constant::kMaxBuffer8192>;

	class CodeGenerator
	{
	public:
		CodeGenerator(
			const std::vector<ParsedTypeInfo>& types,
			const std::vector<ParsedEnumInfo>& enums,
			const std::string&				   sourceFilePath,
			const std::string&				   outputDir );

		bool generate();

		const std::string& getOutputFilePath() const { return _outputFilePath; }

	private:
		void emitFileHeader( CodeEmitBuffer& out ) const;
		void emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const;
		void emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const;

		static std::string sanitizeIdentifier( const std::string& fqn );

	private:
		const std::vector<ParsedTypeInfo>& _types;
		const std::vector<ParsedEnumInfo>& _enums;
		std::string						   _sourceFilePath;
		std::string						   _outputDir;
		std::string						   _outputFilePath;
	};
} // namespace sw::tool
