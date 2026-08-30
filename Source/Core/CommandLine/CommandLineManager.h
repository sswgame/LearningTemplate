/**
 * @file CommandLineManager.h
 * @brief 애플리케이션 실행 시 전달되는 커맨드라인 매개변수(CLI Arguments)를 파싱하고 캐싱하는 매니저 클래스
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) CommandLineArgument — ArgumentList.xxx 에서 생성되는 표준 키
	// ------------------------------------------------------------------------------
	/**
	 * @enum CommandLineArgument
	 * @brief 시스템 내부적으로 정해진 표준 커맨드라인 인자들의 열거형 식별자
	 */
	enum class CommandLineArgument : uint8
	{
/** @brief ArgumentList.xxx 한 줄을 열거 멤버로 펼칩니다. */
#define SW_REGISTER_ARGUMENT( name, hasValue, defaultValue, ... ) name,
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
		Count /**< 등록된 인자의 총 개수 */
	};

	/** @brief sw::string 및 string_view 키용 이종 해시 함수입니다. */
	struct StringHash
	{
		using is_transparent = void;
		/** @brief string_view 내용을 해시합니다. */
		size_t operator()( string_view txt ) const noexcept { return std::hash<string_view>{}( txt ); }
		size_t operator()( const string& txt ) const noexcept { return std::hash<string_view>{}( string_view{ txt } ); }
	};

	/** @brief sw::string 및 string_view 키용 이종 동등 비교자입니다. */
	struct StringEqual
	{
		using is_transparent = void;
		bool operator()( string_view lhs, string_view rhs ) const noexcept { return lhs == rhs; }
	};

	// ------------------------------------------------------------------------------
	// 2) CommandLineManager — initialize(등록) → parse → getArgument
	// ------------------------------------------------------------------------------
	/**
	 * @class CommandLineManager
	 * @brief 명령줄 인자를 파싱하여 자료형별(Value Variant)로 저장/관리하는 시스템
	 */
	class SW_API CommandLineManager final
	{
		/** @brief 한 인자의 현재값·기본값·필수 여부입니다. */
		struct SW_API ArgumentInfo
		{
			using Value = std::variant<int32, bool, float32, string>;

			Value				   _value;
			Value				   _defaultValue;
			uint8				   _bMustHaveValue	 : 1;
			uint8				   _bUseDefaultValue : 1;
			uint8				   _bParsed			 : 1;
			[[maybe_unused]] uint8 _reserved		 : 5;

			/** @brief 값을 비우고 플래그를 끕니다. */
			ArgumentInfo();
		};

	public:
		/** @brief 빈 인자 맵으로 둡니다. */
		explicit CommandLineManager() = default;
		/** @brief 인자 맵만 버립니다. */
		~CommandLineManager() = default;
		/** @brief 복사를 금지합니다. */
		CommandLineManager( const CommandLineManager& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		CommandLineManager& operator=( const CommandLineManager& ) = delete;
		/** @brief 이동 생성을 금지합니다. */
		CommandLineManager( CommandLineManager&& ) = delete;
		/** @brief 이동 대입을 금지합니다. */
		CommandLineManager& operator=( CommandLineManager&& ) = delete;

		/** @brief initialize 를 호출합니다. 실패하면 false 입니다. */
		bool startup();
		/** @brief 파싱 결과는 프로세스 수명과 같으므로 할 일이 없습니다. */
		void shutdown() {}
		/**
		 * @brief CommandList.xxx 파일을 참조하여 허용 가능한 커맨드라인 인자 목록을 동적 등록
		 */
		void initialize();

		/**
		 * @brief 메인 함수로부터 전달받은 UTF-8 인자 목록을 파싱 (Linux/Mac 또는 표준 C++)
		 * @param argc 인자 개수
		 * @param pPpArgv 문자열 포인터 배열 (UTF-8)
		 */
		void parse( int32 argc, utf8* pPpArgv[] );

		/**
		 * @brief 메인 함수로부터 전달받은 UTF-16 와이드 문자열 인자 목록을 파싱 (Windows API 환경)
		 * @param argc 인자 개수
		 * @param pPpArgv 와이드 문자열 포인터 배열 (UTF-16)
		 */
		void parse( int32 argc, utf16* pPpArgv[] );

		/**
		 * @brief 문자열 키(Key)를 이용해 파싱된 인자 값을 조회합니다.
		 * @tparam T 가져올 값의 타입 (bool, int32, float32, sw::string 등)
		 * @param key 조회할 커맨드라인 키 (예: "--width")
		 * @param outValue 조회된 값이 저장될 출력 변수
		 * @return 해당 키가 존재하고 타입 캐스팅이 성공하면 true 반환
		 */
		template <typename T>
		bool getArgument( string_view key, T& outValue ) const;

		/**
		 * @brief 사전 정의된 식별자(Enum)를 이용해 파싱된 인자 값을 빠르게 조회합니다.
		 * @tparam T 가져올 값의 타입
		 * @param argument 조회할 표준 열거형 인덱스
		 * @param outValue 조회된 값이 저장될 출력 변수
		 * @return 해당 키가 존재하고 타입 캐스팅이 성공하면 true 반환
		 */
		template <typename T>
		bool getArgument( CommandLineArgument argument, T& outValue ) const;

		/**
		 * @brief 인자를 추가합니다
		 */
		template <typename T>
		void addArgument( const std::initializer_list<string_view>& listSynonym, bool bMustHaveValue, T defaultValue, bool bUseDefaultValue );

	private:
		/** @brief 단일 인자 라인(예: "--width=1280" 또는 "-fullscreen")을 파싱하여 사전에 적용 */
		void parseArgumentLine( string_view argumentLine );

		/** @brief 단일 문자열로 연결된 커맨드라인 원시 텍스트를 내부 사전에 매핑하여 파싱 처리 */
		void parseInternal( string_view cmdLine );

		/** @brief 인자 값을 설정합니다. */
		void setValue( ArgumentInfo& argument, string_view newValue ) const;

		/**
		 * @brief 인자 enum을 문자열로 변환합니다
		 */
		static string_view argumentEnumToString( CommandLineArgument argument );

		static constexpr auto								   kLineDelim = ";";
		vector<ArgumentInfo>								   _listArgument;
		unordered_map<string, uint32, StringHash, StringEqual> _mapArgument;
	};

	template <typename T>
	bool CommandLineManager::getArgument( string_view key, T& outValue ) const
	{
		SW_LOG_ASSERT( key.empty() == false, "들어온 값이 비어있으면 안됩니다" );

		const auto iter = _mapArgument.find( key );
		if ( iter == _mapArgument.end() )
		{
			return false;
		}

		const uint32		argumentIndex = iter->second;
		const ArgumentInfo& argument	  = _listArgument[argumentIndex];

		const ArgumentInfo::Value* pTargetValue{ nullptr };
		if ( argument._bParsed != 0 )
		{
			pTargetValue = &argument._value;
		}
		else if ( argument._bUseDefaultValue != 0 )
		{
			pTargetValue = &argument._defaultValue;
		}
		else
		{
			return false;
		}

		if constexpr ( std::is_same_v<T, bool> )
		{
			const bool* pResult = std::get_if<bool>( pTargetValue );
			if ( pResult != nullptr )
			{
				outValue = *pResult;
				return true;
			}
		}
		else if constexpr ( std::is_integral_v<T> )
		{
			const int32* pResult = std::get_if<int32>( pTargetValue );
			if ( pResult != nullptr )
			{
				outValue = static_cast<T>( *pResult );
				return true;
			}
		}
		else if constexpr ( std::is_floating_point_v<T> )
		{
			const float32* pResult = std::get_if<float32>( pTargetValue );
			if ( pResult != nullptr )
			{
				outValue = static_cast<T>( *pResult );
				return true;
			}
		}
		else if constexpr ( std::is_same_v<T, string> )
		{
			const string* pResult = std::get_if<string>( pTargetValue );
			if ( pResult != nullptr )
			{
				outValue = *pResult;
				return true;
			}
		}
		else if constexpr ( std::is_same_v<T, string_view> )
		{
			const string* pResult = std::get_if<string>( pTargetValue );
			if ( pResult != nullptr )
			{
				outValue = string_view{ *pResult };
				return true;
			}
		}

		return false;
	}

	template <typename T>
	bool CommandLineManager::getArgument( const CommandLineArgument argument, T& outValue ) const
	{
		const string_view argumentName = argumentEnumToString( argument );
		return getArgument( argumentName, outValue );
	}

	template <typename T>
	void CommandLineManager::addArgument( const std::initializer_list<string_view>& listSynonym, const bool bMustHaveValue, T defaultValue, const bool bUseDefaultValue )
	{
		const uint32 newArgumentIndex = static_cast<uint32>( _listArgument.size() );
		for ( string_view synonym : listSynonym )
		{
			const auto iter = _mapArgument.find( synonym );
			if ( iter != _mapArgument.end() )
			{
				SW_LOG_ASSERT( false, "%#은 이미 사용 중입니다", synonym );
				return;
			}
			_mapArgument.emplace( string{ synonym }, newArgumentIndex );
		}

		ArgumentInfo argument{};
		argument._bMustHaveValue   = bMustHaveValue;
		argument._bUseDefaultValue = bUseDefaultValue;
		argument._defaultValue	   = std::move( defaultValue );
		_listArgument.push_back( argument );
	}
} // namespace sw
