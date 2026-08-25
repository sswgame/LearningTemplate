/**
 * @file AstVisitor.h
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 * @details 수집 결과 타입은 ParsedReflection.h, 어노테이션 문자열 적용은 AnnotationApply.* 입니다.
 */
#pragma once

#include "ParsedReflection.h"

#include "Engine/EngineMinimal.h"

#include <clang-c/Index.h>

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) AstVisitor — CXTranslationUnit 순회, REFLECT/ENUM 수집
	// ------------------------------------------------------------------------------
	/**
	 * @brief libclang CXTranslationUnit을 순회하여 REFLECT, ENUM 등을 수집하는 클래스
	 */
	class AstVisitor
	{
	public:
		explicit AstVisitor( CXTranslationUnit translationUnit );

		/** @brief AST 트리를 방문하며 리플렉션 정보를 수집합니다. */
		void visit();

		const vector<ParsedTypeInfo>& getCollectedTypes() const { return _types; }
		const vector<ParsedEnumInfo>& getCollectedEnums() const { return _enums; }

		/** @brief 커서의 네임스페이스 포함 이름(FQN)을 만듭니다. */
		static string buildFullyQualifiedName( CXCursor cursor );

	private:
		static CXChildVisitResult visitCursor( CXCursor cursor, CXCursor parent, CXClientData clientData );
		void					  onStructDecl( CXCursor cursor );
		void					  onEnumDecl( CXCursor cursor );
		static bool				  hasAnnotation( CXCursor cursor, const string& prefix );
		static string			  getCursorSpelling( CXCursor cursor );

	private:
		CXTranslationUnit	   _translationUnit;
		vector<ParsedTypeInfo> _types;
		vector<ParsedEnumInfo> _enums;
	};
} // namespace sw
