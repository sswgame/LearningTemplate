/**
 * @file CommandLineManager.cpp
 * @brief 커맨드라인 인자 파싱 구현
 */
#include "pch.h"
#include "CommandLineManager.h"
#include "Core/Utility/GlobalVariable/GlobalVariableManager.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/String/StringBuilder.h"
#include "Core/Common/CommonDefines.h"

namespace sw
{
	CommandLineManager::ArgumentInfo::ArgumentInfo()
		: _bMustHaveValue{ 0 }
		, _bUseDefaultValue{ 0 }
		, _reserved{ 0 }
	{
	}

	// ============================================================================
	// @function initialize
	// @brief 미리 정의된 커맨드라인 식별자 매크로 테이블(ArgumentList.xxx)을 읽어들여
	//        모든 가능한 인자들의 초기화 정보를 _arguments 맵에 등록합니다.
	// ============================================================================
	void CommandLineManager::initialize()
	{
#define SW_REGISTER_ARGUMENT( name, mustHaveValue, defaultValue, useDefaultValue, ... ) \
	addArgument( { #name, __VA_ARGS__ }, mustHaveValue, defaultValue, useDefaultValue );
#include "Core/Utility/Predefined/ArgumentList.xxx"
#undef SW_REGISTER_ARGUMENT
	}
	// ============================================================================
	// @function parse
	// @brief 메인 함수로부터 전달받은 UTF-8 인자 목록을 파싱 (Linux/Mac 또는 표준 C++)
	// ============================================================================
	void CommandLineManager::parse( const int32 argc, utf8* argv[] )
	{
		if ( argc < 2 )
			return;

		sw::StringBuilder<constant::kMaxBuffer1024> builder;
		for ( int32 index = 1; index < argc; ++index )
		{
			if ( argv[index] != nullptr )
			{
				builder.append( reinterpret_cast<const utf8*>( argv[index] ) );
				builder.append( kLineDelim );
			}
		}

		const std::string utf8	  = StringUtil::localeToUtf8( builder.view() );
		const std::string trimmed = StringUtil::trim( utf8 );
		parseInternal( trimmed );
	}

	// ============================================================================
	// @function parse
	// @brief Windows OS에서 전달되는 UTF-16 환경의 argc/argv를 UTF-8로 변환한 뒤 파서에 전달합니다.
	// ============================================================================
	void CommandLineManager::parse( const int32 argc, utf16* argv[] )
	{
		if ( argc < 2 )
			return;

		std::wstring rawStr;
		rawStr.reserve( 1024 );
		for ( int32 index = 1; index < argc; ++index )
		{
			if ( argv[index] != nullptr )
			{
				rawStr.append( reinterpret_cast<const utf16*>( argv[index] ) );
				rawStr.append( L"\n" );
			}
		}

		const std::string locale  = StringUtil::utf16ToLocale( rawStr );
		const std::string utf8	  = StringUtil::localeToUtf8( locale );
		const std::string trimmed = StringUtil::trim( utf8 );
		parseInternal( trimmed );
	}

	// ============================================================================
	// @function parseInternal
	// @brief 줄바꿈 기준으로 인자 목록을 분리(Split)한 뒤, "=" 문자를 기준으로 Key-Value 구조를 추출해 맵을 업데이트합니다.
	// ============================================================================
	void CommandLineManager::parseInternal( const std::string& cmdLine )
	{
		const string_splitter				 lineSplitter{ cmdLine, { kLineDelim } };
		const std::vector<std::string_view>& argumentList = lineSplitter.getSplitList();

		for ( const std::string_view& argumentLine : argumentList )
		{
			if ( argumentLine.empty() )
				continue;

			const string_splitter argumentSplitter{ argumentLine, { "=" } };
			const uint32		  splitCount = argumentSplitter.getCount();
			SW_LOG_ASSERT( splitCount > 0 && splitCount < 3, "인자 형식이 올바르지 않습니다." );

			const std::vector<std::string_view>& argumentPair = argumentSplitter.getSplitList();

			const std::string key{ argumentPair[0] };
			std::string_view  cleanKeyView = argumentPair[0];
			while ( cleanKeyView.empty() == false && cleanKeyView.front() == '-' )
			{
				cleanKeyView.remove_prefix( 1 );
			}
			std::string cleanKey{ cleanKeyView };

			const bool		  bHasValue = ( splitCount == 2 );
			const std::string valueStr	= bHasValue ? std::string( argumentPair[1] ) : "true";

			auto iter = _mapArgument.find( cleanKey );
			if ( iter == _mapArgument.end() )
				iter = _mapArgument.find( key );

			if ( iter == _mapArgument.end() )
			{
				SW_LOG_WARNING( "%#에 해당하는 Argument는 없습니다. 무시됩니다", key.c_str() );
				continue;
			}

			const uint32  arugmentIndex = iter->second;
			ArgumentInfo& argument		= _argumentList[arugmentIndex];

			const bool bHasNoValue = ( argument._bMustHaveValue && bHasValue == false );
			if ( bHasNoValue )
			{
				SW_LOG_WARNING( "Value가 입력되지 않았습니다 : %#. 무시됩니다", key );
				continue;
			}

			if ( bHasValue )
			{
				const std::string value{ argumentPair[1] };
				setValue( argument, value );
			}
			else
			{
				argument._value = true;
			}
		}
	}

	void CommandLineManager::setValue( ArgumentInfo& argument, const std::string& newValue ) const
	{
		if ( std::holds_alternative<bool>( argument._defaultValue ) )
			argument._value = StringUtil::strnicmp( newValue.c_str(), "True", StringUtil::strlen( "True" ) ) == 0 ||
							  StringUtil::strnicmp( newValue.c_str(), "1", StringUtil::strlen( "1" ) ) == 0;
		else if ( std::holds_alternative<int32>( argument._defaultValue ) )
			argument._value = std::stoi( newValue );
		else if ( std::holds_alternative<float32>( argument._defaultValue ) )
			argument._value = std::stof( newValue );
		else
			argument._value = newValue;
	}

	std::string_view CommandLineManager::argumentEnumToString( const CommandLineArgument argument )
	{
		switch ( argument )
		{
#define SW_REGISTER_ARGUMENT( name, mustHaveValue, defaultValue, ... ) \
	case CommandLineArgument::name:                                    \
		return #name;
#include "Core/Utility/Predefined/ArgumentList.xxx"
#undef SW_REGISTER_ARGUMENT

			case CommandLineArgument::Count:
				break;
		}
		SW_LOG_ASSERT( false, "도달하면 안됩니다" );
		return {};
	}
} // namespace sw
