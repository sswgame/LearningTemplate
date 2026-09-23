#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Core/Common/Defines.h"
#include "Core/String/StringUtil.h"

SW_LOG_CALLER( "CommandLineManager" );

namespace sw
{
    // ============================================================================
    // initialize
    // 미리 정의된 커맨드라인 인자 표(ArgumentList.xxx)를 읽어, 모든 인자의 초기 정보와 동의어를 _mapArgument 에 등록한다.
    //
    // 줄마다 "지금 넣는 자리 == 그 줄의 열거값" 인지 확인한다. 이 일치 덕분에 findArgument(enum) 이 이름 없이 O(1)
    // 인덱싱으로 끝난다. 이 일치가 깨지는 경우는 하나뿐이다. initialize 를 두 번 부르거나, 그 전에 addArgument 를
    // 먼저 부르는 것이다.
    //
    // 그래서 표를 **먼저 비운다.** 줄마다 거는 assert 는 Debug 에서만 살아 있고, Shipping 에서 어긋난 표는 조용히
    // **다른 인자의 값을 돌려준다.** `getArgument(WIDTH)` 가 VSYNC 를 읽는 식이다. 비우고 시작하면 최악의 경우라도
    // "먼저 넣은 커스텀 인자가 사라진다" 로 끝나고, initialize 는 몇 번을 불러도 같은 표가 된다.
    // ============================================================================
    void CommandLineManager::initialize()
    {
        _listArgument.clear();
        _mapArgument.clear();
        _listArgument.reserve( static_cast<size_t>( CommandLineArgument::Count ) );

#define SW_REGISTER_ARGUMENT( name, defaultValue, useDefaultValue, ... )                      \
    SW_LOG_ASSERT( _listArgument.size() == static_cast<size_t>( CommandLineArgument::name ),  \
                   "인자 등록 순서가 CommandLineArgument 열거값과 어긋났습니다: %#", #name ); \
    addArgument( { #name, __VA_ARGS__ }, defaultValue, useDefaultValue );
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
    }

    // ============================================================================
    // parse (UTF-8)
    // 표준 C++ · Linux · macOS 의 UTF-8 argv 배열을 한 번만 훑으며 파싱한다.
    // 인자들을 커다란 문자열 하나로 합쳤다가 다시 나누지 않고, argv 원소마다 바로 string_view 로 잘라 파싱하므로
    // 할당이 없다.
    // ============================================================================
    void CommandLineManager::parse( const int32 argc, utf8* pPpArgv[] )
    {
        if ( argc < 2 || pPpArgv == nullptr )
            return;

        for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
        {
            if ( pPpArgv[argIndex] != nullptr && pPpArgv[argIndex][0] != '\0' )
            {
                parseArgumentLine( string_view{ pPpArgv[argIndex] } );
            }
        }
    }

    // ============================================================================
    // parse (UTF-16, Windows)
    // Windows(WinMain · wmain)의 UTF-16 argv 를 UTF-8 로 바꾼 뒤 파싱한다.
    // ============================================================================
    void CommandLineManager::parse( const int32 argc, utf16* pPpArgv[] )
    {
        if ( argc < 2 || pPpArgv == nullptr )
            return;

        for ( int32 argIndex = 1; argIndex < argc; ++argIndex )
        {
            if ( pPpArgv[argIndex] != nullptr && pPpArgv[argIndex][0] != L'\0' )
            {
                const string utf8Line = StringUtil::utf16ToUtf8( pPpArgv[argIndex] );
                parseArgumentLine( string_view{ utf8Line } );
            }
        }
    }

    // ============================================================================
    // parseArgumentLine
    // 인자 한 줄(예: "--width=1280", "-fullscreen", "r_vsync=1")을 해석해 표에 반영한다.
    //
    // 단계:
    // 1. 앞뒤 공백을 잘라 낸다
    // 2. '=' 가 있으면 키와 값으로 나눈다(없으면 bool 플래그로 본다)
    // 3. 키 앞의 하이픈('-', '--')을 떼어 순수한 키 이름을 얻는다
    // 4. 이종 조회로 _mapArgument 에서 인자 인덱스를 O(1) 로 찾는다
    // 5. 찾은 ArgumentInfo 에 타입에 맞게 값을 넣는다
    // ============================================================================
    void CommandLineManager::parseArgumentLine( string_view argumentLine )
    {
        // 1단계: 앞뒤 공백 자르기
        const string_view line = StringUtil::trim( argumentLine );
        if ( line.empty() )
            return;

        // 2단계: '=' 구분자 찾기
        const size_t eqPos     = line.find( '=' );
        const bool   bHasValue = ( eqPos != string_view::npos );

        const string_view rawKey   = bHasValue ? line.substr( 0, eqPos ) : line;
        const string_view valueStr = bHasValue ? line.substr( eqPos + 1 ) : string_view{};

        // 3단계: 앞의 하이픈('-') 떼기
        string_view cleanKey = rawKey;
        while ( cleanKey.empty() == false && cleanKey.front() == '-' )
        {
            cleanKey.remove_prefix( 1 );
        }

        // 4단계: 등록된 인자 표에서 찾기(하이픈을 뗀 키로 먼저 찾고, 없으면 원래 키로)
        auto iter = _mapArgument.find( cleanKey );
        if ( iter == _mapArgument.end() )
            iter = _mapArgument.find( rawKey );

        if ( iter == _mapArgument.end() )
        {
            // 모듈(EditorModule · SWGame)이 선언하는 전역 변수는 **이 시점에 아직 없다.** 커맨드라인은 모듈이 로드되기 전에
            // 파싱되기 때문이다. 버리면 그 스위치는 영영 적용되지 않으므로 보류표에 남겨 두고, 모듈이 나중에 등록할 때
            // GlobalVariableManager::registerVariable 이 꺼내 적용한다.
            // 표는 비우지 않는다. 핫 리로드로 모듈이 다시 올라와도 커맨드라인 값은 프로세스가 끝날 때까지 유효해야 한다.
            // 대신 오타를 놓치지 않도록, 모듈 로드가 끝난 뒤 아무도 가져가지 않은 키를 App 이 한 번 경고한다
            // (collectPendingGlobalNames).
            if ( cleanKey.rfind( kGlobalVariablePrefix, 0 ) == 0 )
            {
                // 값 없이 적은 `-gv_flag` 는 플래그다. setValue 와 같은 뜻으로 "true" 를 남긴다.
                _mapPendingGlobal[string{ cleanKey }] = bHasValue ? string{ valueStr } : string{ "true" };
                return;
            }

            SW_LOG_WARNING( "%#에 해당하는 Argument는 없습니다. 무시됩니다", string( rawKey ).c_str() );
            return;
        }

        ArgumentInfo& argument = _listArgument[iter->second];

        // 필수 값이 빠졌는지 검사한다. 값 없이 적어도 되는 것은 bool 뿐이다. `-dx12` 는 "true" 라는 뜻이지만 `-WIDTH` 나
        // `-gv_benchMeshes` 에는 그런 뜻이 없다. 예전에는 이 판단이 타입과 따로 노는 `_bMustHaveValue` 칸이었고, 그래서
        // 값을 빠뜨린 `-gv_benchMeshes` 가 int32 자리에 bool 을 밀어 넣은 뒤 **경고 한 줄 없이 아무 일도 하지 않았다**
        // (readValue 의 get_if 가 nullptr).
        if ( bHasValue == false && argument.isFlagArgument() == false )
        {
            SW_LOG_WARNING( "Value가 입력되지 않았습니다 : %#. 무시됩니다", string( rawKey ).c_str() );
            return;
        }

        // 5단계: 타입별로 값을 넣는다. 값 없이 적은 것은 플래그이므로 true 로 켠다.
        if ( bHasValue )
            setValue( argument, cleanKey, valueStr );
        else
            argument._value = true;

        argument._bParsed = SW_TRUE;
    }

    const CommandLineManager::ArgumentInfo* CommandLineManager::findArgument( string_view key ) const
    {
        SW_LOG_ASSERT( key.empty() == false, "들어온 값이 비어있으면 안됩니다" );

        const auto iter = _mapArgument.find( key );
        if ( iter == _mapArgument.end() )
            return nullptr;

        return &_listArgument[iter->second];
    }

    const CommandLineManager::ArgumentInfo* CommandLineManager::findArgument( const CommandLineArgument argument ) const
    {
        // ArgumentList.xxx 한 줄이 열거 멤버 하나와 _listArgument 원소 하나를 같은 순서로 만든다(initialize 가 줄마다
        // 그 일치를 assert 한다). 그래서 이름을 만들어 해시할 필요가 없다. Count 나 initialize 이전의 조회는 여기서
        // nullptr 로 걸러진다.
        const size_t argumentIndex = static_cast<size_t>( argument );
        if ( _listArgument.size() <= argumentIndex )
            return nullptr;

        return &_listArgument[argumentIndex];
    }

    bool CommandLineManager::isArgumentProvided( string_view key ) const
    {
        const ArgumentInfo* pArgument = findArgument( key );
        return pArgument != nullptr && pArgument->_bParsed != SW_FALSE;
    }

    bool CommandLineManager::isArgumentProvided( const CommandLineArgument argument ) const
    {
        const ArgumentInfo* pArgument = findArgument( argument );
        return pArgument != nullptr && pArgument->_bParsed != SW_FALSE;
    }

    bool CommandLineManager::findPendingGlobalValue( string_view name, string& outValue ) const
    {
        const auto iter = _mapPendingGlobal.find( name );
        if ( iter == _mapPendingGlobal.end() )
            return false;

        outValue = iter->second;
        return true;
    }

    vector<string> CommandLineManager::collectPendingGlobalNames() const
    {
        vector<string> listName;
        listName.reserve( _mapPendingGlobal.size() );
        for ( auto iter = _mapPendingGlobal.begin(); iter != _mapPendingGlobal.end(); ++iter )
            listName.push_back( iter->first );
        return listName;
    }

    /**
     * @brief 문자열 값을 대상 인자의 기본값 타입에 맞게 변환해 저장합니다.
     *
     * StringUtil::parse* 로 임시 문자열을 할당하지 않고 바로 변환합니다. 변환에 실패하면 0 이 조용히 들어가지 않도록
     * 경고를 남깁니다. `-WIDTH=abc` 가 아무 말 없이 폭 0 이 되면 창이 왜 뜨지 않는지 알 길이 없기 때문입니다.
     */
    void CommandLineManager::setValue( ArgumentInfo& argument, string_view key, string_view newValue )
    {
        if ( std::holds_alternative<bool>( argument._defaultValue ) )
        {
            argument._value = StringUtil::parseBool( newValue, false );
        }
        else if ( std::holds_alternative<int32>( argument._defaultValue ) )
        {
            int32 parsedValue{ 0 };
            if ( StringUtil::parseInt( newValue, parsedValue ) == false )
                SW_LOG_WARNING( "%#: 정수가 아닙니다 (%#). 0 으로 둡니다", string( key ).c_str(), string( newValue ).c_str() );
            argument._value = parsedValue;
        }
        else if ( std::holds_alternative<float32>( argument._defaultValue ) )
        {
            float32 parsedValue{ 0.0f };
            if ( StringUtil::parseFloat( newValue, parsedValue ) == false )
                SW_LOG_WARNING( "%#: 실수가 아닙니다 (%#). 0 으로 둡니다", string( key ).c_str(), string( newValue ).c_str() );
            argument._value = parsedValue;
        }
        else
            argument._value = string( newValue );
    }
} // namespace sw
