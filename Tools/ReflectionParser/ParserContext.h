#pragma once

/**
 * @file ParserContext.h
 * @brief libclang CXIndex/CXTranslationUnit 래퍼 및 clang 인자 캐시
 */

#include "Core/CoreMinimal.h"
#include <clang-c/Index.h>

namespace sw::tool
{
	/** @brief parser/engine config에서 로드한 clang 공통 인자 (프로세스당 1회 캐시) */
	struct ParserClangConfig
	{
		std::vector<std::string> baseArgs;
		bool					 bLoaded = false;

		bool					 load();
		std::vector<std::string> buildArgs( const std::vector<std::string>& includePaths ) const;
	};

	class ParserContext
	{
	public:
		ParserContext();
		~ParserContext();

		ParserContext( const ParserContext& )			 = delete;
		ParserContext& operator=( const ParserContext& ) = delete;

		static bool						ensureSharedConfig();
		static CXIndex					getSharedIndex();
		static const ParserClangConfig& getSharedConfig();
		static void						shutdownShared();

		bool parse( const std::string& filePath, const std::vector<std::string>& includePaths );

		CXTranslationUnit getTranslationUnit() const { return _translationUnit; }

	private:
		CXTranslationUnit _translationUnit = nullptr;
	};
} // namespace sw::tool
