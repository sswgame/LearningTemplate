#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Core/Common/Defines.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

SW_LOG_CALLER( "CommandLineManager" );

namespace sw
{
	/**
	 * @brief 기본 인자 정보 생성자 (기본 상태 초기화)
	 */
	CommandLineManager::ArgumentInfo::ArgumentInfo()
		: _value{}
		, _defaultValue{}
		, _bMustHaveValue{ SW_FALSE }
		, _bUseDefaultValue{ SW_FALSE }
		, _bParsed{ SW_FALSE }
		, _reserved{ 0 } {}

	// ============================================================================
	// @function initialize
	// @brief 미리 정의된 커맨드라인 식별자 매크로 테이블(ArgumentList.xxx)을 읽어들여
	//        모든 가능한 인자들의 초기화 정보 및 별칭(Synonym)들을 _mapArgument 사전에 등록합니다.
	// ============================================================================
	void CommandLineManager::initialize()
	{
#define SW_REGISTER_ARGUMENT( name, mustHaveValue, defaultValue, useDefaultValue, ... ) \
	addArgument( { #name, __VA_ARGS__ }, mustHaveValue, defaultValue, useDefaultValue );
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
	}

	// ============================================================================
	// @function parse (UTF-8 버전)
	// @brief 표준 C++ / Linux / macOS 환경의 UTF-8 argv 배열을 1-Pass로 순회 파싱합니다.
	//
	// [초심자 가이드 / 성능 최적화]:
	// 인자들을 불필요하게 단일 거대 문자열로 합쳤다가 다시 분할하지 않고,
	// 각 argv 요소를 즉시 std::string_view로 슬라이싱하여 파싱함으로써 0-Alloc 파싱을 수행합니다.
	// ============================================================================
	void CommandLineManager::parse( const int32 argc, utf8* ppArgv[] )
	{
		if ( argc < 2 || ppArgv == nullptr )
			return;

		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			if ( ppArgv[argIndex] != nullptr && ppArgv[argIndex][0] != '\0' )
			{
				parseArgumentLine( string_view{ ppArgv[argIndex] } );
			}
		}
	}

	// ============================================================================
	// @function parse (UTF-16 Windows 버전)
	// @brief Windows OS WinMain/wmain 환경의 UTF-16 argv를 UTF-8로 변환한 뒤 파싱합니다.
	// ============================================================================
	void CommandLineManager::parse( const int32 argc, utf16* ppArgv[] )
	{
		if ( argc < 2 || ppArgv == nullptr )
			return;

		for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
		{
			if ( ppArgv[argIndex] != nullptr && ppArgv[argIndex][0] != L'\0' )
			{
				const string utf8 = StringUtil::utf16ToUtf8( ppArgv[argIndex] );
				parseArgumentLine( string_view{ utf8 } );
			}
		}
	}

	// ============================================================================
	// @function parseArgumentLine
	// @brief 단일 인자 텍스트(예: "--width=1280", "-fullscreen", "r_vsync=1")를 해석하여 사전에 적용합니다.
	//
	// [파싱 알고리즘 단계]:
	// 1. 문자열 앞뒤의 불필요한 공백을 트리밍
	// 2. '=' 문자가 있는지 확인하여 Key와 Value로 분리 (없으면 boolean 플래그로 간주하여 value="true")
	// 3. Key 앞부분의 하이픈('-', '--') 접두사를 제거(remove_prefix)하여 순수 키 이름 도출
	// 4. 이종 검색(Heterogeneous Lookup)을 통해 _mapArgument에서 인자 인덱스를 O(1)로 조회
	// 5. 해당 ArgumentInfo에 타입에 맞게 값을 설정
	// ============================================================================
	void CommandLineManager::parseArgumentLine( string_view argumentLine )
	{
		string_view line = argumentLine;
		// 1단계: 앞뒤 공백 트리밍
		while ( line.empty() == false && ( line.front() == ' ' || line.front() == '\t' || line.front() == '\r' || line.front() == '\n' ) )
			line.remove_prefix( 1 );
		while ( line.empty() == false && ( line.back() == ' ' || line.back() == '\t' || line.back() == '\r' || line.back() == '\n' ) )
			line.remove_suffix( 1 );

		if ( line.empty() )
			return;

		// 2단계: '=' 구분자 탐색
		const size_t eqPos	   = line.find( '=' );
		const bool	 bHasValue = ( eqPos != string_view::npos );

		string_view rawKey	 = bHasValue ? line.substr( 0, eqPos ) : line;
		string_view valueStr = bHasValue ? line.substr( eqPos + 1 ) : string_view{ "true" };

		// 3단계: 선행 하이픈('-') 제거
		string_view cleanKey = rawKey;
		while ( cleanKey.empty() == false && cleanKey.front() == '-' )
		{
			cleanKey.remove_prefix( 1 );
		}

		// 4단계: 등록된 인자 사전에서 조회 (하이픈 제거 키 우선 검색 후 원본 키 폴백)
		auto iter = _mapArgument.find( cleanKey );
		if ( iter == _mapArgument.end() )
			iter = _mapArgument.find( rawKey );

		if ( iter == _mapArgument.end() )
		{
			SW_LOG_WARNING( "%#에 해당하는 Argument는 없습니다. 무시됩니다", string( rawKey ).c_str() );
			return;
		}

		const uint32  argumentIndex = iter->second;
		ArgumentInfo& argument		= _argumentList[argumentIndex];

		// 필수 값 누락 검사
		const bool bHasNoValue = ( argument._bMustHaveValue != 0 && bHasValue == false );
		if ( bHasNoValue )
		{
			SW_LOG_WARNING( "Value가 입력되지 않았습니다 : %#. 무시됩니다", string( rawKey ).c_str() );
			return;
		}

		// 5단계: 타입별 값 대입
		if ( bHasValue )
			setValue( argument, valueStr );
		else
			argument._value = true;

		argument._bParsed = 1;
	}

	// ============================================================================
	// @function parseInternal
	// @brief 세미콜론(';') 등으로 구분된 배치 명령줄 텍스트를 분할하여 순차 처리합니다.
	// ============================================================================
	void CommandLineManager::parseInternal( string_view cmdLine )
	{
		const string_splitter lineSplitter{ cmdLine, { kLineDelim } };
		for ( string_view argumentLine : lineSplitter.getSplitList() )
		{
			parseArgumentLine( argumentLine );
		}
	}

	/**
	 * @brief 문자열 값을 대상 인자의 기본값 타입에 맞추어 변환하고 저장합니다.
	 *
	 * [초심자 가이드]:
	 * std::from_chars 및 StringUtil::strnicmp를 사용하여 임시 문자열 힙 할당 없이 즉시 타입 변환을 수행합니다.
	 */
	void CommandLineManager::setValue( ArgumentInfo& argument, string_view newValue ) const
	{
		if ( std::holds_alternative<bool>( argument._defaultValue ) )
		{
			const bool bVal = ( newValue == "1" ||
								StringUtil::strnicmp( newValue.data(), "true", static_cast<uint32>( newValue.size() ) ) == 0 ||
								StringUtil::strnicmp( newValue.data(), "yes", static_cast<uint32>( newValue.size() ) ) == 0 ||
								StringUtil::strnicmp( newValue.data(), "on", static_cast<uint32>( newValue.size() ) ) == 0 );
			argument._value = bVal;
		}
		else if ( std::holds_alternative<int32>( argument._defaultValue ) )
		{
			int32 val{ 0 };
			auto [ptr, ec]	= std::from_chars( newValue.data(), newValue.data() + newValue.size(), val );
			argument._value = ( ec == std::errc{} ) ? val : 0;
		}
		else if ( std::holds_alternative<float32>( argument._defaultValue ) )
		{
			float32 val{ 0.0f };
			auto [ptr, ec] = std::from_chars( newValue.data(), newValue.data() + newValue.size(), val );
			if ( ec != std::errc{} )
			{
				const string valStr( newValue );
				val = static_cast<float32>( StringUtil::atof( valStr.c_str() ) );
			}
			argument._value = val;
		}
		else
			argument._value = string( newValue );
	}

	/**
	 * @brief CommandLineArgument 열거형 값을 문자열 이름으로 변환합니다.
	 */
	string_view CommandLineManager::argumentEnumToString( const CommandLineArgument argument )
	{
		switch ( argument )
		{
#define SW_REGISTER_ARGUMENT( name, mustHaveValue, defaultValue, ... ) \
	case CommandLineArgument::name:                                    \
		return #name;
#include "Core/Predefined/ArgumentList.xxx"
#undef SW_REGISTER_ARGUMENT

			case CommandLineArgument::Count:
				break;
		}
		SW_LOG_ASSERT( false, "도달하면 안됩니다" );
		return {};
	}
} // namespace sw
