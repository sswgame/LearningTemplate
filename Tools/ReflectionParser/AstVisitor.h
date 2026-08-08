#pragma once

/**
 * @file AstVisitor.h
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 */

#include "Core/CoreMinimal.h"
#include <clang-c/Index.h>

namespace sw::tool
{
	struct ParsedPropertyInfo
	{
		std::string name;
		std::string typeName;
		std::string alias;
		std::string category;
		std::string displayName;
		std::string tooltip;
		bool		readOnly	= false;
		bool		hasRange	= false;
		float		minRange	= 0.0f;
		float		maxRange	= 1.0f;
		bool		isContainer	  = false;
		std::string containerKind = "None";
		std::string containerType;
		std::string elementTypeName;
		std::string keyTypeName;
	};

	struct ParsedFunctionInfo
	{
		std::string				 name;
		std::string				 returnTypeName;
		std::vector<std::string> paramTypeNames;
	};

	struct ParsedTypeInfo
	{
		std::string						name;
		std::string						fullyQualifiedName;
		std::string						parentFQN;
		std::vector<ParsedPropertyInfo> properties;
		std::vector<ParsedFunctionInfo> methods;
	};

	struct ParsedEnumeratorInfo
	{
		std::string name;
		int64		value = 0;
	};

	struct ParsedEnumInfo
	{
		std::string						  name;
		std::string						  fullyQualifiedName;
		bool							  isBitFlag = false;
		std::vector<ParsedEnumeratorInfo> enumerators;
	};

	class AstVisitor
	{
	public:
		explicit AstVisitor( CXTranslationUnit tu );

		void visit();

		const std::vector<ParsedTypeInfo>& getCollectedTypes() const { return _types; }
		const std::vector<ParsedEnumInfo>& getCollectedEnums() const { return _enums; }

		static std::string buildFullyQualifiedName( CXCursor cursor );

	private:
		static CXChildVisitResult visitCursor( CXCursor cursor, CXCursor parent, CXClientData clientData );

		void onStructDecl( CXCursor cursor );
		void onEnumDecl( CXCursor cursor );

		static bool		   hasAnnotation( CXCursor cursor, const std::string& prefix );
		static std::string getCursorSpelling( CXCursor cursor );

	private:
		CXTranslationUnit			_translationUnit;
		std::vector<ParsedTypeInfo> _types;
		std::vector<ParsedEnumInfo> _enums;
	};
} // namespace sw::tool
