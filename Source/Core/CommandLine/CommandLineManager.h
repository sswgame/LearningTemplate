/**
 * @file CommandLineManager.h
 * @brief 실행할 때 받은 커맨드라인 인자를 파싱하고 보관하는 매니저입니다.
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
    // 1) CommandLineArgument — ArgumentList.xxx 에서 만드는 표준 키
    // ------------------------------------------------------------------------------
    /**
     * @enum CommandLineArgument
     * @brief 엔진이 정해 둔 표준 커맨드라인 인자의 열거형 식별자입니다.
     */
    enum class CommandLineArgument : uint8
    {
/** @brief ArgumentList.xxx 한 줄을 열거 멤버로 펼칩니다. */
#define SW_REGISTER_ARGUMENT( name, defaultValue, ... ) name,
#include "Core/Predefined/ArgumentList.xxx"

#undef SW_REGISTER_ARGUMENT
        Count /**< 등록된 인자의 총 개수 */
    };

    // ------------------------------------------------------------------------------
    // 2) CommandLineManager — initialize(등록) → parse → getArgument
    // ------------------------------------------------------------------------------
    /**
     * @class CommandLineManager
     * @brief 커맨드라인 인자를 파싱해 타입별(variant) 값으로 보관하는 매니저입니다.
     */
    class SW_API CommandLineManager final
    {
        /** @brief 인자 하나의 현재 값 · 기본값 · 파싱 여부입니다. */
        struct ArgumentInfo
        {
            using Value = std::variant<int32, bool, float32, string>;

            Value                  _value;
            Value                  _defaultValue;
            uint8                  _bUseDefaultValue : 1;
            uint8                  _bParsed          : 1;
            [[maybe_unused]] uint8 _reserved         : 6;

            /**
             * @brief 값을 비우고 플래그를 끕니다.
             * @details 헤더에 둡니다. `addArgument` 가 템플릿이라 호출하는 쪽 TU 에서 이 생성자를 찾기 때문입니다. .cpp 에 두면
             *          Engine.dll 에서 내보내야만 링크됩니다.
             */
            ArgumentInfo()
                : _value{}
                , _defaultValue{}
                , _bUseDefaultValue{ SW_FALSE }
                , _bParsed{ SW_FALSE }
                , _reserved{ 0 } {}

            /**
             * @brief `-key` 처럼 **값 없이** 적을 수 있는 인자인지 확인합니다.
             * @details 그럴 수 있는 것은 bool 뿐입니다. 값 없는 `-dx12` 는 "true" 라는 뜻이 되지만, 값 없는 `-WIDTH` 나
             *          `-gv_benchMeshes` 에는 그런 뜻이 없습니다. 예전에는 이것이 `_bMustHaveValue` 라는 별도 칸이었고,
             *          `GlobalVariableManager` 가 모든 전역 변수를 타입과 상관없이 "값이 없어도 됨" 으로 등록했습니다. 그래서 값을
             *          빠뜨린 `-gv_benchMeshes` 가 int32 자리에 bool 을 밀어 넣었고, 이후 `readValue` 의 `get_if<int32>` 가 nullptr 을
             *          반환해 **경고 한 줄 없이 아무 일도 일어나지 않았습니다.**
             */
            bool isFlagArgument() const { return std::holds_alternative<bool>( _defaultValue ); }
        };

    public:
        /** @brief 빈 인자 맵으로 둡니다. */
        CommandLineManager() = default;
        /** @brief 인자 맵을 해제합니다. */
        ~CommandLineManager() = default;
        /** @brief 복사를 금지합니다. */
        CommandLineManager( const CommandLineManager& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        CommandLineManager& operator=( const CommandLineManager& ) = delete;
        /** @brief 이동 생성을 금지합니다. */
        CommandLineManager( CommandLineManager&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        CommandLineManager& operator=( CommandLineManager&& ) = delete;

        /**
         * @brief ArgumentList.xxx 를 읽어 허용하는 커맨드라인 인자를 등록합니다.
         * @details 등록 순서는 그 파일의 줄 순서이고, `CommandLineArgument` 열거 멤버도 같은 파일에서 같은 순서로 만들어집니다.
         *          그래서 열거값이 곧 `_listArgument` 인덱스입니다. 그 일치를 줄마다 assert 하므로, 열거형으로 조회할 때는 이름을
         *          만들지도 해시하지도 않습니다. 표를 **먼저 비우므로 몇 번을 불러도 같은 표가 됩니다.** 다만 그 전에
         *          `addArgument` 로 넣어 둔 커스텀 인자는 함께 사라집니다.
         */
        void initialize();
        /** @brief 파싱 결과는 프로세스가 끝날 때까지 쓰므로 할 일이 없습니다. */
        void shutdown() {}

        /**
         * @brief main 함수가 받은 UTF-8 인자 목록을 파싱합니다(Linux · Mac 또는 표준 C++).
         * @param argc 인자 개수
         * @param pPpArgv 문자열 포인터 배열(UTF-8)
         */
        void parse( int32 argc, utf8* pPpArgv[] );

        /**
         * @brief main 함수가 받은 UTF-16 인자 목록을 파싱합니다(Windows).
         * @param argc 인자 개수
         * @param pPpArgv 와이드 문자열 포인터 배열(UTF-16)
         */
        void parse( int32 argc, utf16* pPpArgv[] );

        /**
         * @brief 문자열 키로 파싱된 인자 값을 찾습니다.
         * @tparam T 가져올 값의 타입(bool, 정수, 부동소수, sw::string, string_view)
         * @param key 등록된 이름이나 동의어(예: "WIDTH", "W"). 앞의 하이픈은 파싱할 때 떼어 내므로 **조회 키에는 붙이지
         *            않습니다.** `"--WIDTH"` 로는 찾지 못합니다.
         * @param outValue 찾은 값을 받을 변수
         * @return 키가 있고 타입 변환에 성공하면 true
         */
        template <typename T>
        bool getArgument( string_view key, T& outValue ) const;

        /**
         * @brief 미리 정의된 열거값으로 파싱된 인자 값을 빠르게 찾습니다.
         * @tparam T 가져올 값의 타입
         * @param argument 찾을 표준 인자의 열거값
         * @param outValue 찾은 값을 받을 변수
         * @return 키가 있고 타입 변환에 성공하면 true
         */
        template <typename T>
        bool getArgument( CommandLineArgument argument, T& outValue ) const;

        /**
         * @brief 그 인자가 **커맨드라인에 실제로 적혔는지** 반환합니다.
         * @details `getArgument` 로는 알 수 없습니다. 기본값이 있는 인자는 적지 않아도 true 를 반환하기 때문입니다. "설정 파일
         *          기본값보다 커맨드라인이 우선" 같은 판단에는 이 함수가 필요합니다. 실제로 `EngineConfig` 의 백엔드 기본값이
         *          `-gv_rhiBackend` 를 조용히 덮어쓰고 있었습니다.
         */
        bool isArgumentProvided( string_view key ) const;

        /** @brief 표준 인자가 커맨드라인에 실제로 적혔는지 반환합니다. */
        bool isArgumentProvided( CommandLineArgument argument ) const;

        /**
         * @brief 인자를 추가합니다.
         * @tparam T 저장 타입. **값을 반드시 적어야 하는지도 이것으로 정해집니다.** `bool` 이면 `-key` 만 적을 수 있고(그때
         *         true), 나머지는 `-key=value` 를 요구합니다(`ArgumentInfo::isFlagArgument`).
         * @param listSynonym 이 인자를 부르는 이름들. 하나라도 이미 쓰이고 있으면 **아무것도 넣지 않습니다.**
         * @param defaultValue `bUseDefaultValue` 가 켜져 있을 때 `getArgument` 가 반환할 값
         * @param bUseDefaultValue 인자를 주지 않아도 `getArgument` 가 true 를 반환할지 여부
         */
        template <typename T>
        void addArgument( const std::initializer_list<string_view>& listSynonym, T defaultValue, bool bUseDefaultValue );

        /**
         * @brief 아직 등록된 인자가 없어 보류해 둔 `gv_` 값을 찾습니다.
         * @details 커맨드라인은 모듈(EditorModule · SWGame)이 로드되기 **전에** 파싱됩니다. 그래서 모듈이 선언하는 전역 변수의
         *          값은 파싱 시점에 갈 곳이 없습니다. 버리는 대신 여기에 남겨 두고, 모듈이 나중에 등록할 때
         *          `GlobalVariableManager` 가 꺼내 씁니다.
         */
        bool findPendingGlobalValue( string_view name, string& outValue ) const;

        /** @brief 보류 중인 `gv_` 키 이름을 모두 반환합니다(오타 진단용). */
        vector<string> collectPendingGlobalNames() const;

    private:
        /** @brief 인자 한 줄(예: "--width=1280", "-fullscreen")을 파싱해 표에 반영합니다. */
        void parseArgumentLine( string_view argumentLine );

        /** @brief 등록된 이름이나 동의어로 인자를 찾습니다. 없으면 nullptr 입니다. */
        const ArgumentInfo* findArgument( string_view key ) const;

        /** @brief 열거값을 `_listArgument` 인덱스로 바로 씁니다(initialize 가 그 일치를 보장합니다). */
        const ArgumentInfo* findArgument( CommandLineArgument argument ) const;

        /** @brief 인자 값을 등록된 기본값의 타입에 맞춰 변환해 넣습니다. */
        static void setValue( ArgumentInfo& argument, string_view key, string_view newValue );

        /**
         * @brief 파싱된 값(없으면 기본값)을 요청한 타입으로 꺼냅니다.
         * @details 저장 타입은 넷(bool · int32 · float32 · string)뿐이므로, 요청 타입 T 를 그중 하나로 대응시킨 뒤 variant 에서
         *          한 번만 꺼냅니다.
         */
        template <typename T>
        static bool readValue( const ArgumentInfo& argument, T& outValue );

        /** @brief 보류표에 담을 키의 접두어입니다. 이것으로 시작하는 미등록 키만 남깁니다. */
        static constexpr auto kGlobalVariablePrefix = "gv_";

        vector<ArgumentInfo> _listArgument;
        /** @brief 이름 · 동의어 → `_listArgument` 인덱스입니다. 기본 해시가 transparent 라 string_view 로 할당 없이 조회됩니다. */
        unordered_map<string, uint32> _mapArgument;
        /** @brief 등록된 인자가 없어 보류해 둔 `gv_` 키 → 값입니다. 모듈이 나중에 선언할 때 꺼내 씁니다. */
        unordered_map<string, string> _mapPendingGlobal;
    };

    template <typename T>
    bool CommandLineManager::readValue( const ArgumentInfo& argument, T& outValue )
    {
        static_assert( std::is_same_v<T, bool> || std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, string> || std::is_same_v<T, string_view>,
                       "커맨드라인 값은 bool·정수·부동소수·string·string_view 로만 꺼낼 수 있습니다" );

        const ArgumentInfo::Value* pTargetValue{ nullptr };
        if ( argument._bParsed != SW_FALSE )
            pTargetValue = &argument._value;
        else if ( argument._bUseDefaultValue != SW_FALSE )
            pTargetValue = &argument._defaultValue;
        else
            return false;

        // 요청 타입 → 저장 타입. bool 을 먼저 걸러야 한다(bool 도 정수 타입이다).
        using StoredType = std::conditional_t<std::is_same_v<T, bool>, bool,
                                              std::conditional_t<std::is_integral_v<T>, int32,
                                                                 std::conditional_t<std::is_floating_point_v<T>, float32, string>>>;

        const StoredType* pStoredValue = std::get_if<StoredType>( pTargetValue );
        if ( pStoredValue == nullptr )
            return false;

        if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, string_view> )
            outValue = T{ *pStoredValue };
        else
            outValue = static_cast<T>( *pStoredValue );

        return true;
    }

    template <typename T>
    bool CommandLineManager::getArgument( string_view key, T& outValue ) const
    {
        const ArgumentInfo* pArgument = findArgument( key );
        if ( pArgument == nullptr )
            return false;

        return readValue( *pArgument, outValue );
    }

    template <typename T>
    bool CommandLineManager::getArgument( const CommandLineArgument argument, T& outValue ) const
    {
        const ArgumentInfo* pArgument = findArgument( argument );
        if ( pArgument == nullptr )
            return false;

        return readValue( *pArgument, outValue );
    }

    template <typename T>
    void CommandLineManager::addArgument( const std::initializer_list<string_view>& listSynonym, T defaultValue, const bool bUseDefaultValue )
    {
        // 먼저 모두 검사한다. 예전에는 겹치는 이름을 만난 자리에서 돌아갔고, 그 앞에서 이미 넣은 동의어들이 끝내 만들어지지
        // 않는 인덱스를 가리킨 채 남았다. 그 동의어로 조회하면 _listArgument 범위 밖을 읽는다.
        for ( string_view synonym : listSynonym )
        {
            if ( _mapArgument.find( synonym ) != _mapArgument.end() )
            {
                SW_LOG_ASSERT( false, "%#은 이미 사용 중입니다", synonym );
                return;
            }
        }

        ArgumentInfo argument{};
        argument._bUseDefaultValue = bUseDefaultValue ? SW_TRUE : SW_FALSE;
        argument._defaultValue     = std::move( defaultValue );

        const uint32 newArgumentIndex = static_cast<uint32>( _listArgument.size() );
        _listArgument.push_back( std::move( argument ) );
        for ( string_view synonym : listSynonym )
        {
            _mapArgument.emplace( string{ synonym }, newArgumentIndex );
        }
    }
} // namespace sw
