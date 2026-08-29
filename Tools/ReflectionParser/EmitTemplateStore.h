/**
 * @file EmitTemplateStore.h
 * @brief 이름 붙은 .tpl 골격을 로드하고 $VAR / ${VAR} 자리 표시자를 확장합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) emit — *.tpl 골격 로드·$VAR 확장
	// ------------------------------------------------------------------------------
	/**
	 * @class EmitTemplateStore
	 * @brief 파일 기반 emit 골격(registrar·traits 등). 제어 흐름은 C++에 둡니다.
	 */
	class EmitTemplateStore
	{
	public:
		EmitTemplateStore();
		~EmitTemplateStore() = default;

		/** @brief 프로세스 전역 템플릿 저장소를 반환합니다. */
		static EmitTemplateStore& instance();

		/** @brief 로드된 템플릿을 비웁니다. */
		void clear();
		/** @brief 절대 경로 디렉터리에서 *.tpl 을 로드합니다. */
		bool loadDirectory( const string_view absDir );
		/** @brief 템플릿이 로드되었는지 반환합니다. */
		bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

		/** @brief 이름에 해당하는 템플릿이 있는지 조회합니다. */
		bool has( const string_view name ) const;
		/** @brief 템플릿을 확장합니다. 모르는 변수는 빈 문자열, 없으면 빈 결과를 반환합니다. */
		string render( const string_view					name,
					   const unordered_map<string, string>& vars ) const;
		string render( const string_view									 name,
					   std::initializer_list<pair<string_view, string_view>> vars ) const;

		/** @brief 메모리 속 골격을 확장합니다(테스트·폴백용). */
		static string expand( const string_view					   tpl,
							  const unordered_map<string, string>& vars );
		static string expand( const string_view										tpl,
							  std::initializer_list<pair<string_view, string_view>> vars );

	private:
		unordered_map<string, string> _mapTemplate;
		uint8						  _bLoaded	: 1;
		[[maybe_unused]] uint8		  _reserved : 7;
	};
} // namespace sw
