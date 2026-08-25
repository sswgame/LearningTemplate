#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionTypes.h"
namespace sw
{
	/**
	 * @enum PropertyWidgetType
	 * @brief 에디터 인스펙터가 프로퍼티를 렌더링할 때 사용하는 UI 위젯 유형
	 */
	enum class PropertyWidgetType : uint8
	{
		Default		 = 0, ///< 기본 텍스트/숫자 필드
		Slider		 = 1, ///< 슬라이더 바 (Min..Max)
		ColorPicker	 = 2, ///< RGB/RGBA 컬러 피커
		AssetPicker	 = 3, ///< 에셋 파일 탐색기 피커
		Multiline	 = 4, ///< 여러 줄 텍스트 입력창
		Checkbox	 = 5, ///< 불리언 체크박스
		EnumDropdown = 6, ///< 열거형 드롭다운
		Curve		 = 7, ///< 애니메이션/이징 커브 드로어
	};

	/**
	 * @class PropertyMetaHint
	 * @brief 리플렉션 프로퍼티 메타데이터로부터 UI 힌트 및 서식 정보를 파싱/제공하는 헬퍼
	 */
	class SW_API PropertyMetaHint
	{
	public:
		/**
		 * @brief PropertyMetadata를 기반으로 적합한 에디터 위젯 유형을 판별합니다.
		 */
		static PropertyWidgetType deduceWidgetType( const PropertyMetadata& meta, string_view typeName );

		/**
		 * @brief 슬라이더용 범위 값을 반환합니다.
		 */
		static bool getSliderRange( const PropertyMetadata& meta, float32& outMin, float32& outMax );

		/**
		 * @brief 에셋 피커가 허용할 확장자 필터를 반환합니다.
		 */
		static const utf8* getAssetFilter( const PropertyMetadata& meta );
	};
} // namespace sw
