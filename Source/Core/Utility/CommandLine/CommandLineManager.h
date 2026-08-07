#pragma once

/**
 * @file CommandLineManager.h
 * @brief 애플리케이션 실행 시 전달되는 커맨드라인 매개변수(CLI Arguments)를 파싱하고 캐싱하는 매니저 클래스
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	/**
	 * @enum CommandLineArgument
	 * @brief 시스템 내부적으로 정해진 표준 커맨드라인 인자들의 열거형 식별자
	 */
	enum class CommandLineArgument : uint8
	{
/** @brief SW_REGISTER_ARGUMENT 매크로 정의입니다. */
#define SW_REGISTER_ARGUMENT( name, hasValue, defaultValue, ... ) name,
#include "Core/Utility/Predefined/ArgumentList.xxx"
#undef SW_REGISTER_ARGUMENT
		Count /**< 등록된 인자의 총 개수 */
	};
} // namespace sw

namespace sw
{

	/**
	 * @struct StringHash
	 * @brief std::unordered_map 등에서 std::string_view를 키로 사용하여 검색할 수 있도록 지원하는 커스텀 해시 함수 객체 (C++14 이종 조회 지원)
	 */
	struct StringHash
	{
		using is_transparent = void;
		size_t operator()( std::string_view txt ) const
		{
			return std::hash<std::string_view>{}( txt );
		}
	};

	/**
	 * @class CommandLineManager
	 * @brief 명령줄 인자를 파싱하여 자료형별(Value Variant)로 저장/관리하는 시스템
	 */
	class CommandLineManager final
	{
		struct ArgumentInfo
		{
			using Value = std::variant<int32, bool, float32, std::string>;

			Value _value		= {};
			Value _defaultValue = {};
			uint8 _bMustHaveValue	: 1;
			uint8 _bUseDefaultValue : 1;
			uint8 _reserved			: 6;

			ArgumentInfo()
				: _bMustHaveValue( 0 )
				, _bUseDefaultValue( 0 )
				, _reserved( 0 )
			{
			}
		};

	public:
		explicit CommandLineManager()								   = default;
		~CommandLineManager()										   = default;
		CommandLineManager( const CommandLineManager& )			 = delete;
		CommandLineManager& operator=( const CommandLineManager& ) = delete;
		CommandLineManager( CommandLineManager&& )				 = delete;
		CommandLineManager& operator=( CommandLineManager&& )	 = delete;

	public:
		bool startup();
		void shutdown() {}
		/**
		 * @brief CommandList.xxx 파일을 참조하여 허용 가능한 커맨드라인 인자 목록을 동적 등록
		 */
		void initialize();

		/**
		 * @brief 메인 함수로부터 전달받은 UTF-8 인자 목록을 파싱 (Linux/Mac 또는 표준 C++)
		 * @param argc 인자 개수
		 * @param argv 문자열 포인터 배열 (UTF-8)
		 */
		void parse( int32 argc, utf8* argv[] );

		/**
		 * @brief 메인 함수로부터 전달받은 UTF-16 와이드 문자열 인자 목록을 파싱 (Windows API 환경)
		 * @param argc 인자 개수
		 * @param argv 와이드 문자열 포인터 배열 (UTF-16)
		 */
		void parse( int32 argc, utf16* argv[] );

		/**
		 * @brief 문자열 키(Key)를 이용해 파싱된 인자 값을 조회합니다.
		 * @tparam T 가져올 값의 타입 (bool, int32, float, std::string 등)
		 * @param key 조회할 커맨드라인 키 (예: "--width")
		 * @param outValue 조회된 값이 저장될 출력 변수
		 * @return 해당 키가 존재하고 타입 캐스팅이 성공하면 true 반환
		 */
		template <typename T>
		/**
		 * @brief getArgument 처리를 수행합니다.
		 */
		bool getArgument( const std::string_view& key, T& outValue ) const;

		/**
		 * @brief 사전 정의된 식별자(Enum)를 이용해 파싱된 인자 값을 빠르게 조회합니다.
		 * @tparam T 가져올 값의 타입
		 * @param argument 조회할 표준 열거형 인덱스
		 * @param outValue 조회된 값이 저장될 출력 변수
		 * @return 해당 키가 존재하고 타입 캐스팅이 성공하면 true 반환
		 */
		template <typename T>
		/**
		 * @brief getArgument 처리를 수행합니다.
		 */
		bool getArgument( const CommandLineArgument argument, T& outValue ) const;

		template <typename T>
		/**
		 * @brief addArgument 처리를 수행합니다.
		 */
		void addArgument( const std::initializer_list<std::string_view>& synonymList, bool bMustHaveValue, T&& defaultValue, const bool bUseDefaultValue );

	private:
		/** @brief 단일 문자열로 연결된 커맨드라인 원시 텍스트를 내부 사전에 매핑하여 파싱 처리 */
		void parseInternal( const std::string& cmdLine );

		void setValue( ArgumentInfo& argument, const std::string& newValue ) const;
		/**
		 * @brief argumentEnumToString 처리를 수행합니다.
		 */
		static std::string_view argumentEnumToString( const CommandLineArgument argument );

	private:
		static constexpr const utf8*										 kLineDelim = ";";
		std::vector<ArgumentInfo>											 _argumentList;
		std::unordered_map<std::string, uint32, StringHash, std::equal_to<>> _mapArgument;
	};

	template <typename T>
	bool CommandLineManager::getArgument( const std::string_view& key, T& outValue ) const
	{
		SW_LOG_ASSERT( key.empty() == false, "들어온 값이 비어있으면 안됩니다" );

		const auto iter = _mapArgument.find( key );
		if ( iter == _mapArgument.end() )
		{
			SW_LOG_ASSERT( false, "%#가 존재하지 않습니다.", key );
			return false;
		}
		const uint32		argumentIndex = iter->second;
		const ArgumentInfo& argument	  = _argumentList[argumentIndex];

		const ArgumentInfo::Value* targetValue = &argument._value;
		if ( argument._bUseDefaultValue == false )
		{
			if ( argument._value == argument._defaultValue )
				return false;
		}
		else
		{
			if ( argument._value == ArgumentInfo::Value{} )
				targetValue = &argument._defaultValue;
		}

		if constexpr ( std::is_same_v<T, bool> )
		{
			const bool* result = std::get_if<bool>( targetValue );
			if ( result == nullptr )
				return false;

			outValue = *result;
			return true;
		}

		if constexpr ( std::is_integral_v<T> )
		{
			const int32* result = std::get_if<int32>( targetValue );
			if ( result == nullptr )
				return false;

			outValue = static_cast<T>( *result );
			return true;
		}

		if constexpr ( std::is_floating_point_v<T> )
		{
			const float32* result = std::get_if<float32>( targetValue );
			if ( result == nullptr )
				return false;

			outValue = static_cast<float32>( *result );
			return true;
		}

		if constexpr ( std::is_same_v<T, std::string> )
		{
			const std::string* result = std::get_if<std::string>( targetValue );
			if ( result != nullptr )
			{
				outValue = static_cast<std::string>( *result );
				return true;
			}
		}

		return false;
	}

	template <typename T>
	bool CommandLineManager::getArgument( const CommandLineArgument argument, T& outValue ) const
	{
		const std::string_view argumentName = argumentEnumToString( argument );
		return getArgument( argumentName, outValue );
	}

	template <typename T>
	void CommandLineManager::addArgument( const std::initializer_list<std::string_view>& synonymList, const bool bMustHaveValue, T&& defaultValue, const bool bUseDefaultValue )
	{
		const uint32 newArgumentIndex = static_cast<uint32>( _argumentList.size() );
		for ( const std::string_view& synonym : synonymList )
		{
			const auto iter = _mapArgument.find( synonym );
			if ( iter != _mapArgument.end() )
			{
				SW_LOG_ASSERT( false, "%#은 이미 사용 중입니다", synonym );
				return;
			}
			_mapArgument.emplace( std::string{ synonym }, newArgumentIndex );
		}

		ArgumentInfo argument{};
		argument._bMustHaveValue   = bMustHaveValue;
		argument._bUseDefaultValue = bUseDefaultValue;
		argument._defaultValue	   = std::forward<T>( defaultValue );
		_argumentList.push_back( argument );
	}
} // namespace sw
