#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Core/Common/Defines.h"
#include "Core/String/StringUtil.h"

SW_LOG_CALLER( "CommandLineManager" );

namespace sw
{
    // ============================================================================
    // @function initialize
    // @brief 미리 정의된 커맨드라인 식별자 매크로 테이블(ArgumentList.xxx)을 읽어들여
    //        모든 가능한 인자들의 초기화 정보 및 별칭(Synonym)들을 _mapArgument 사전에 등록합니다.
    //
    // 한 줄마다 "지금 넣는 자리 == 그 줄의 열거값" 을 확인한다. 이 일치가 findArgument(enum) 을
    // 이름 없는 O(1) 인덱싱으로 만들어 주는 근거이고, 깨지는 경우는 하나뿐이다 —
    // initialize 를 두 번 부르거나, 그 전에 addArgument 를 먼저 부르는 것.
    //
    // 그래서 표를 **먼저 비운다.** 줄마다 거는 assert 는 Debug 에서만 살아 있고, Shipping 에서
    // 어긋난 표는 조용히 **다른 인자의 값을 돌려준다** — `getArgument(WIDTH)` 가 VSYNC 를 읽는
    // 식이다. 비우고 시작하면 최악이 "먼저 넣은 커스텀 인자가 사라진다" 로 끝나고, initialize 는
    // 몇 번을 불러도 같은 표가 된다.
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
    // @function parse (UTF-8 버전)
    // @brief 표준 C++ / Linux / macOS 환경의 UTF-8 argv 배열을 1-Pass로 순회 파싱합니다.
    //
    // [초심자 가이드 / 성능 최적화]:
    // 인자들을 불필요하게 단일 거대 문자열로 합쳤다가 다시 분할하지 않고,
    // 각 argv 요소를 즉시 std::string_view로 슬라이싱하여 파싱함으로써 0-Alloc 파싱을 수행합니다.
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
    // @function parse (UTF-16 Windows 버전)
    // @brief Windows OS WinMain/wmain 환경의 UTF-16 argv를 UTF-8로 변환한 뒤 파싱합니다.
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
    // @function parseArgumentLine
    // @brief 단일 인자 텍스트(예: "--width=1280", "-fullscreen", "r_vsync=1")를 해석하여 사전에 적용합니다.
    //
    // [파싱 알고리즘 단계]:
    // 1. 문자열 앞뒤의 불필요한 공백을 트리밍
    // 2. '=' 문자가 있는지 확인하여 Key와 Value로 분리 (없으면 boolean 플래그로 간주)
    // 3. Key 앞부분의 하이픈('-', '--') 접두사를 제거(remove_prefix)하여 순수 키 이름 도출
    // 4. 이종 검색(Heterogeneous Lookup)을 통해 _mapArgument에서 인자 인덱스를 O(1)로 조회
    // 5. 해당 ArgumentInfo에 타입에 맞게 값을 설정
    // ============================================================================
    void CommandLineManager::parseArgumentLine( string_view argumentLine )
    {
        // 1단계: 앞뒤 공백 트리밍
        const string_view line = StringUtil::trim( argumentLine );
        if ( line.empty() )
            return;

        // 2단계: '=' 구분자 탐색
        const size_t eqPos     = line.find( '=' );
        const bool   bHasValue = ( eqPos != string_view::npos );

        const string_view rawKey   = bHasValue ? line.substr( 0, eqPos ) : line;
        const string_view valueStr = bHasValue ? line.substr( eqPos + 1 ) : string_view{};

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
            // 모듈(EditorModule·SWGame)이 선언하는 전역 변수는 **이 시점에 아직 없다** — 커맨드라인은
            // 모듈이 로드되기 전에 파싱된다. 버리면 그 스위치는 영영 먹지 않으므로 보류표에 남겨 두고,
            // 모듈이 늦게 등록할 때 GlobalVariableManager::registerVariable 이 꺼내 적용한다.
            // 표는 비우지 않는다 — 핫 리로드로 모듈이 다시 올라와도 커맨드라인 값은 프로세스 수명
            // 내내 유효해야 한다. 대신 오타를 놓치지 않도록, 모듈 로드가 끝난 뒤 아무도 가져가지 않은
            // 키를 App 이 한 번 경고한다(collectPendingGlobalNames).
            if ( cleanKey.rfind( kGlobalVariablePrefix, 0 ) == 0 )
            {
                // 값 없이 적은 `-gv_flag` 는 플래그다 — setValue 와 같은 뜻으로 "true" 를 남긴다.
                _mapPendingGlobal[string{ cleanKey }] = bHasValue ? string{ valueStr } : string{ "true" };
                return;
            }

            SW_LOG_WARNING( "%#에 해당하는 Argument는 없습니다. 무시됩니다", string( rawKey ).c_str() );
            return;
        }

        ArgumentInfo& argument = _listArgument[iter->second];

        // 필수 값 누락 검사. 값 없이 적어도 되는 것은 bool 뿐이다 — `-dx12` 는 "true" 라는 뜻이지만
        // `-WIDTH` 나 `-gv_benchMeshes` 에는 그런 뜻이 없다. 예전에는 이 판단이 타입과 따로 노는
        // `_bMustHaveValue` 칸이었고, 그래서 값을 빠뜨린 `-gv_benchMeshes` 가 int32 자리에 bool 을
        // 밀어 넣은 뒤 **경고 한 줄 없이 아무 일도 하지 않았다**(readValue 의 get_if 가 nullptr).
        if ( bHasValue == false && argument.isFlagArgument() == false )
        {
            SW_LOG_WARNING( "Value가 입력되지 않았습니다 : %#. 무시됩니다", string( rawKey ).c_str() );
            return;
        }

        // 5단계: 타입별 값 대입. 값 없이 적은 것은 플래그이므로 true 로 켠다.
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
        // ArgumentList.xxx 한 줄이 열거 멤버 하나와 _listArgument 원소 하나를 같은 순서로 만든다
        // (initialize 가 줄마다 그 일치를 assert 한다). 그래서 이름을 만들어 해시할 일이 없다.
        // Count 나 initialize 이전의 조회는 여기서 nullptr 로 걸린다.
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
     * @brief 문자열 값을 대상 인자의 기본값 타입에 맞추어 변환하고 저장합니다.
     *
     * [초심자 가이드]:
     * StringUtil::parse*를 사용하여 임시 문자열 힙 할당 없이(0-Allocation) 즉시 타입 변환을 수행합니다.
     * 변환에 실패하면 0 이 조용히 들어가지 않도록 경고를 남긴다 — `-WIDTH=abc` 가 아무 말 없이
     * 폭 0 이 되면 창이 왜 안 뜨는지 알 길이 없다.
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
