#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/Defines.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct GlobalVariableInternal
        {
            static void writeEnumValue( void* pData, uint32 typeSize, int32 val )
            {
                if ( pData == nullptr )
                    return;
                if ( typeSize == 1 )
                    *static_cast<uint8*>( pData ) = static_cast<uint8>( val );
                else if ( typeSize == 2 )
                    *static_cast<uint16*>( pData ) = static_cast<uint16>( val );
                else if ( typeSize == 8 )
                    *static_cast<int64*>( pData ) = static_cast<int64>( val );
                else
                    *static_cast<int32*>( pData ) = val;
            }

            static int32 readEnumValue( const void* pData, uint32 typeSize )
            {
                if ( typeSize == 1 )
                    return *static_cast<const uint8*>( pData );
                if ( typeSize == 2 )
                    return *static_cast<const uint16*>( pData );
                if ( typeSize == 8 )
                    return static_cast<int32>( *static_cast<const int64*>( pData ) );
                return *static_cast<const int32*>( pData );
            }

            static inline shared_mutex s_stringVarMutex;
        };

        /**
         * @brief 타입 하나의 처리를 모은 처리 테이블의 행입니다. 모든 함수는 `_pData != nullptr` 인 변수만 받고, 변경 콜백은 부르지 않습니다(부르는 쪽이 부른다).
         * @details 프레임 경로가 아니다. 명령줄 적용 · 에디터 · 프리셋 · 기본값 되돌리기만 이 테이블을 거친다(변수 값은 C++ 변수로 직접 읽는다).
         */
        struct GlobalVariableTypeOps
        {
            GlobalVariableType _type;
            /** @brief 표시 · 프리셋용 타입 이름입니다(`GlobalVariableInfo::getTypeName`). */
            const utf8* _pName;
            /** @brief 현재 값을 문자열로 만듭니다. */
            string ( *_formatValue )( const GlobalVariableInfo& info );
            /** @brief 글을 파싱해 값을 씁니다. 읽지 못하면 값을 그대로 두고 false 입니다. */
            bool ( *_parseValue )( GlobalVariableInfo& info, string_view text );
            /** @brief `_defaultValue` 를 값에 씁니다. 기본값의 타입이 다르면 아무것도 하지 않습니다. */
            void ( *_writeDefault )( GlobalVariableInfo& info );
            /** @brief 명령줄 인자(`-gv_*`)로 등록합니다. */
            void ( *_addCommandLineArgument )( CommandLineManager& commandLine, const GlobalVariableInfo& info );
            /**
             * @brief 명령줄 인자 값을 씁니다. 썼으면 true 입니다.
             * @param outPendingText 지금 읽을 수 없어 나중에 다시 적용할 글입니다(enum 이름은 파서가 걸린 뒤에 읽는다). 없으면 비워 둡니다.
             */
            bool ( *_readCommandLineArgument )( const CommandLineManager& commandLine, GlobalVariableInfo& info, string& outPendingText );
        };

        /** @brief bool · int32 · float32 처럼 변수에 그대로 저장하는 타입의 처리입니다. */
        template <typename T>
        struct GlobalVariableScalarOps
        {
            static string formatValue( const GlobalVariableInfo& info )
            {
                const T value = *static_cast<const T*>( info._pData );
                if constexpr ( std::is_same_v<T, bool> )
                {
                    return value ? "true" : "false";
                }
                else
                {
                    // std::to_string 은 힙을 쓰므로 스택 버퍼로 바꾼다
                    StringBuilder<constant::kMaxBuffer32> sb;
                    sb.append( value );
                    return string( sb.view() );
                }
            }

            [[nodiscard]] static bool parseValue( GlobalVariableInfo& info, string_view text )
            {
                // 읽지 못하면 값은 그대로다 — "ture" · "enabled" 가 조용히 false 로 들어가지 않게.
                T    value{};
                bool bParsed{ false };
                if constexpr ( std::is_same_v<T, bool> )
                    bParsed = StringUtil::tryParseBool( text, value );
                else if constexpr ( std::is_same_v<T, int32> )
                    bParsed = StringUtil::parseInt( text, value );
                else
                    bParsed = StringUtil::parseFloat( text, value );
                if ( bParsed == false )
                    return false;
                *static_cast<T*>( info._pData ) = value;
                return true;
            }

            static void writeDefault( GlobalVariableInfo& info )
            {
                if ( std::holds_alternative<T>( info._defaultValue ) )
                    *static_cast<T*>( info._pData ) = std::get<T>( info._defaultValue );
            }

            static void addCommandLineArgument( CommandLineManager& commandLine, const GlobalVariableInfo& info )
            {
                const T defaultValue = std::holds_alternative<T>( info._defaultValue ) ? std::get<T>( info._defaultValue ) : T{};
                commandLine.addArgument<T>( { info._name }, defaultValue, true );
            }

            [[nodiscard]] static bool readCommandLineArgument( const CommandLineManager& commandLine, GlobalVariableInfo& info, string& /*outPendingText*/ )
            {
                T value{};
                if ( commandLine.getArgument( info._name, value ) == false )
                    return false;
                *static_cast<T*>( info._pData ) = value;
                return true;
            }
        };

        /** @brief `sw::string` 변수의 처리입니다. 읽기 · 쓰기 모두 `s_stringVarMutex` 를 잡는다(복사 도중 재할당된 버퍼를 읽지 않게). */
        struct GlobalVariableStringOps
        {
            static string formatValue( const GlobalVariableInfo& info )
            {
                ScopedSharedLock<shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
                return *static_cast<const string*>( info._pData );
            }

            [[nodiscard]] static bool parseValue( GlobalVariableInfo& info, string_view text )
            {
                UniqueLock<shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
                *static_cast<string*>( info._pData ) = string{ text };
                return true;
            }

            static void writeDefault( GlobalVariableInfo& info )
            {
                if ( std::holds_alternative<string>( info._defaultValue ) == false )
                    return;
                UniqueLock<shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
                *static_cast<string*>( info._pData ) = std::get<string>( info._defaultValue );
            }

            static void addCommandLineArgument( CommandLineManager& commandLine, const GlobalVariableInfo& info )
            {
                string defaultValue = std::holds_alternative<string>( info._defaultValue ) ? std::get<string>( info._defaultValue ) : string{};
                commandLine.addArgument<string>( { info._name }, std::move( defaultValue ), true );
            }

            [[nodiscard]] static bool readCommandLineArgument( const CommandLineManager& commandLine, GlobalVariableInfo& info, string& /*outPendingText*/ )
            {
                string value;
                if ( commandLine.getArgument( info._name, value ) == false )
                    return false;
                UniqueLock<shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
                *static_cast<string*>( info._pData ) = std::move( value );
                return true;
            }
        };

        /**
         * @brief enum 변수의 처리입니다. int32 로 읽고 쓰며 실제 크기(`_typeSize`)만큼만 씁니다.
         * @details 글은 열거자 이름(`Vulkan`)이다. 숫자(`2`)는 에디터 · 프리셋 · 사용자 설정이 값을 주고받는 안쪽 글(`formatValue` 도 숫자)이라
         *          `parseValue` 가 받지만, 사람이 치는 명령줄은 받지 않는다(`GlobalVariableInfo::setValueFromCommandLine`).
         */
        struct GlobalVariableEnumOps
        {
            static string formatValue( const GlobalVariableInfo& info )
            {
                StringBuilder<constant::kMaxBuffer32> sb;
                sb.append( GlobalVariableInternal::readEnumValue( info._pData, info._typeSize ) );
                return string( sb.view() );
            }

            [[nodiscard]] static bool parseValue( GlobalVariableInfo& info, string_view text )
            {
                // 이름은 enum 테이블을 가진 쪽이 건 파서가 바꾼다.
                int32 value{ 0 };
                bool  bParsed = StringUtil::parseInt( text, value );
                if ( bParsed == false )
                {
                    // 파서는 이 변수를 등록한 매니저의 것이다. 매니저에 등록하지 않은 변수는 숫자만 받는다.
                    const GlobalVariableEnumTextParser* pParser = info._pEnumTextParser;
                    bParsed                                     = pParser != nullptr && pParser->isBound() && ( *pParser )( info._enumType, text, value );
                }
                if ( bParsed == false )
                    return false;
                GlobalVariableInternal::writeEnumValue( info._pData, info._typeSize, value );
                return true;
            }

            static void writeDefault( GlobalVariableInfo& info )
            {
                if ( std::holds_alternative<int32>( info._defaultValue ) )
                    GlobalVariableInternal::writeEnumValue( info._pData, info._typeSize, std::get<int32>( info._defaultValue ) );
            }

            static void addCommandLineArgument( CommandLineManager& commandLine, const GlobalVariableInfo& info )
            {
                // 숫자 또는 열거자 이름이라 글로 받는다. 정수로 받으면 `Vulkan` 이 파싱 경고 한 줄과 함께 버려진다.
                commandLine.addArgument<string>( { info._name }, string{}, false );
            }

            [[nodiscard]] static bool readCommandLineArgument( const CommandLineManager& commandLine, GlobalVariableInfo& info, string& outPendingText )
            {
                // 이름은 파서가 있으면 바로 적용하고, 아직 없으면 `applyPendingEnumText` 까지 받아 둔다. 숫자는 적용하지 않고 같은 길로 보내
                // `setValueFromCommandLine` 이 거절 이유와 함께 알린다(명령줄은 열거자 이름만 받는다).
                string text;
                if ( commandLine.getArgument( info._name, text ) == false || text.empty() )
                    return false;
                int32 number{ 0 };
                if ( StringUtil::parseInt( text, number ) == false && parseValue( info, text ) )
                    return true;
                outPendingText = std::move( text );
                return false;
            }
        };

        /** @brief 처리 구조체(`Ops`) 하나를 테이블 행으로 만듭니다. */
        template <typename Ops>
        constexpr GlobalVariableTypeOps makeGlobalVariableTypeRow( GlobalVariableType type, const utf8* pName )
        {
            return GlobalVariableTypeOps{ type, pName, &Ops::formatValue, &Ops::parseValue, &Ops::writeDefault, &Ops::addCommandLineArgument, &Ops::readCommandLineArgument };
        }

        /** @brief 타입 처리 테이블입니다. 행 순서는 `GlobalVariableType` 열거자 순서와 같다(아래 `static_assert`). */
        struct GlobalVariableTypeTable
        {
            static constexpr uint32 kTypeCount = static_cast<uint32>( GlobalVariableType::Count );

            static constexpr GlobalVariableTypeOps kArrRow[] = {
                makeGlobalVariableTypeRow<GlobalVariableScalarOps<bool>>( GlobalVariableType::Boolean, "Bool" ),
                makeGlobalVariableTypeRow<GlobalVariableScalarOps<int32>>( GlobalVariableType::Int32, "Int32" ),
                makeGlobalVariableTypeRow<GlobalVariableScalarOps<float32>>( GlobalVariableType::Float, "Float" ),
                makeGlobalVariableTypeRow<GlobalVariableStringOps>( GlobalVariableType::String, "String" ),
                makeGlobalVariableTypeRow<GlobalVariableEnumOps>( GlobalVariableType::Enum, "Enum" ),
            };

            static constexpr bool isInTypeOrder()
            {
                for ( uint32 rowIndex = 0; rowIndex < kTypeCount; ++rowIndex )
                {
                    if ( kArrRow[rowIndex]._type != static_cast<GlobalVariableType>( rowIndex ) )
                        return false;
                }
                return true;
            }

            static bool isValidType( GlobalVariableType type ) { return static_cast<uint32>( type ) < kTypeCount; }

            /** @brief @p type 의 행입니다. 등록이 범위 밖 타입을 거절하므로 등록된 변수의 타입은 늘 범위 안이다. */
            static const GlobalVariableTypeOps& getRow( GlobalVariableType type ) { return kArrRow[static_cast<uint32>( type )]; }
        };
        static_assert( std::size( GlobalVariableTypeTable::kArrRow ) == GlobalVariableTypeTable::kTypeCount, "GlobalVariableType needs one row per enumerator" );
        static_assert( GlobalVariableTypeTable::isInTypeOrder(), "GlobalVariableTypeTable rows must follow GlobalVariableType order" );
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GlobalVariableManager" );

    // ============================================================================
    // GlobalVariableInfo 구현부
    // ============================================================================

    /**
     * @brief 현재 값을 Boolean 으로 반환합니다.
     * @return 타입이 Boolean 이면 실제 값, 아니면 false
     */
    bool GlobalVariableInfo::getValueAsBool() const
    {
        if ( _pData != nullptr && _type == GlobalVariableType::Boolean )
            return *static_cast<bool*>( _pData );
        return false;
    }

    /**
     * @brief 현재 값을 32비트 정수(Int32 또는 Enum)로 반환합니다.
     * @return 타입이 Int32 또는 Enum 이면 실제 값, 아니면 0
     */
    int32 GlobalVariableInfo::getValueAsInt() const
    {
        if ( _pData == nullptr )
            return 0;

        if ( _type == GlobalVariableType::Int32 )
            return *static_cast<const int32*>( _pData );

        if ( _type == GlobalVariableType::Enum )
            return GlobalVariableInternal::readEnumValue( _pData, _typeSize );
        return 0;
    }

    /**
     * @brief 현재 값을 32비트 부동소수점(Float)으로 반환합니다.
     * @return 타입이 Float 이면 실제 값, 아니면 0.0f
     */
    float32 GlobalVariableInfo::getValueAsFloat() const
    {
        if ( _pData != nullptr && _type == GlobalVariableType::Float )
            return *static_cast<float32*>( _pData );
        return 0.0f;
    }

    /**
     * @brief 현재 값을 문자열로 바꿔 반환합니다. 타입별 변환은 처리 테이블의 행이 맡습니다.
     */
    string GlobalVariableInfo::getValueAsString() const
    {
        if ( _pData == nullptr )
            return "";
        return GlobalVariableTypeTable::getRow( _type )._formatValue( *this );
    }

    string GlobalVariableInfo::getTypeName() const
    {
        // enum 타입 이름은 enum 변수에만 있다(`GlobalVariableRegistrar` 가 다른 타입에는 비워 둔다).
        if ( _enumType.empty() == false )
            return _enumType;
        return GlobalVariableTypeTable::getRow( _type )._pName;
    }

    /**
     * @brief *_pData 에 Boolean 값을 씁니다.
     */
    bool GlobalVariableInfo::setValueAsBool( bool val )
    {
        if ( _pData == nullptr || _type != GlobalVariableType::Boolean )
            return false;

        *static_cast<bool*>( _pData ) = val;
        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    /**
     * @brief *_pData 에 Int32 또는 Enum 값을 씁니다.
     */
    bool GlobalVariableInfo::setValueAsInt( int32 val )
    {
        if ( _pData == nullptr )
            return false;

        if ( _type == GlobalVariableType::Int32 )
            *static_cast<int32*>( _pData ) = val;
        else if ( _type == GlobalVariableType::Enum )
            GlobalVariableInternal::writeEnumValue( _pData, _typeSize, val );
        else
            return false;

        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    /**
     * @brief *_pData 에 Float 값을 씁니다.
     */
    bool GlobalVariableInfo::setValueAsFloat( float32 val )
    {
        if ( _pData == nullptr || _type != GlobalVariableType::Float )
            return false;

        *static_cast<float32*>( _pData ) = val;
        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    /**
     * @brief *_pData 에 String 값을 씁니다.
     */
    bool GlobalVariableInfo::setValueAsString( string_view val )
    {
        if ( _pData == nullptr || _type != GlobalVariableType::String )
            return false;

        {
            UniqueLock<shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
            *static_cast<string*>( _pData ) = string{ val };
        }
        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    /**
     * @brief 문자열을 파싱해 *_pData 에 값을 씁니다.
     *
     * 값이 바뀌면 등록된 변경 콜백(_onValueChanged)을 부릅니다. 타입별 파싱은 처리 테이블의 행이 맡고, 읽지 못하면 값은 그대로입니다.
     */
    bool GlobalVariableInfo::setValueFromString( string_view strValue )
    {
        if ( _pData == nullptr )
            return false;
        if ( GlobalVariableTypeTable::getRow( _type )._parseValue( *this, strValue ) == false )
            return false;

        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    bool GlobalVariableInfo::setValueFromCommandLine( string_view strValue )
    {
        // 사람이 쓰는 명령줄은 열거자 이름만 받는다. 숫자는 열거자를 더하거나 순서를 바꾸면 뜻이 달라지고, 변수의 크기보다 큰 수는 조용히 잘려 다른 열거자가 된다.
        int32 number{ 0 };
        if ( _type == GlobalVariableType::Enum && StringUtil::parseInt( strValue, number ) )
        {
            SW_LOG_ERROR( "-%#=%# : %# takes an enumerator name of %#, not a number (a number changes meaning when enumerators are added or reordered)",
                          _name.c_str(), string{ strValue }.c_str(), _name.c_str(), _enumType.c_str() );
            return false;
        }
        return setValueFromString( strValue );
    }

    /**
     * @brief 등록할 때 지정한 기본값(_defaultValue)으로 되돌립니다.
     */
    void GlobalVariableInfo::resetToDefault()
    {
        if ( _pData == nullptr )
            return;

        GlobalVariableTypeTable::getRow( _type )._writeDefault( *this );

        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
    }

    // ============================================================================
    // GlobalVariableManager 구현부
    // ============================================================================

    /**
     * @brief 등록된 모든 전역 변수를 CommandLineManager 의 허용 인자로 등록합니다.
     *
     * 그래서 커맨드라인에서 -gv_myVar=123 처럼 값을 넘길 수 있습니다.
     */
    void GlobalVariableManager::registerToCommandLine( CommandLineManager* pCmdLineManager )
    {
        if ( pCmdLineManager == nullptr )
            return;

        // 파싱이 끝난 뒤 등록되는 변수(모듈이 선언한 것)가 보류값을 꺼낼 수 있도록 파서를 잡아 둔다.
        _pCmdLineManager = pCmdLineManager;

        for ( const auto& [name, info] : _mapVariable )
        {
            GlobalVariableTypeTable::getRow( info->_type )._addCommandLineArgument( *pCmdLineManager, *info );
        }
    }

    /**
     * @brief 파싱된 커맨드라인 인자 값을 읽어 등록된 전역 변수에 바로 반영합니다.
     *
     * 문자열로 다시 바꿨다가 파싱하는 과정 없이 타입별 행이 원시 포인터(*_pData)에 값을 넣고, 여기서 변경 알림 콜백을 부릅니다.
     * 지금 읽을 수 없는 글(파서가 아직 없는 enum 이름)은 `applyPendingEnumText` 까지 받아 둡니다.
     */
    void GlobalVariableManager::updateFromCommandLine( const CommandLineManager* pCmdLineManager )
    {
        if ( pCmdLineManager == nullptr )
            return;

        for ( auto& [name, info] : _mapVariable )
        {
            if ( info->_pData == nullptr )
                continue;

            string pendingText;
            if ( GlobalVariableTypeTable::getRow( info->_type )._readCommandLineArgument( *pCmdLineManager, *info, pendingText ) )
            {
                if ( info->_onValueChanged.isBound() )
                    info->_onValueChanged( info.get() );
            }
            else if ( pendingText.empty() == false )
            {
                _mapPendingEnumText[name] = std::move( pendingText );
            }
        }
    }

    /**
     * @brief 새 전역 변수를 매니저에 등록합니다.
     *
     * std::unique_lock 을 잡아 동시 등록 경쟁을 막고, string_view 로 조회해 불필요한 string 을 만들지 않고 이미 있는지 확인합니다.
     */
    bool GlobalVariableManager::registerVariable( string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, string_view description, string_view enumType, string_view moduleName, uint32 typeSize, bool bTestOnly )
    {
        if ( name.empty() || pData == nullptr )
            return false;
        if ( GlobalVariableTypeTable::isValidType( type ) == false )
        {
            SW_LOG_WARNING( "Variable %# has no valid type (%#).", string( name ).c_str(), static_cast<uint32>( type ) );
            return false;
        }

        UniqueLock<shared_mutex> lock{ _mutex };
        if ( _mapVariable.find( name ) != _mapVariable.end() )
        {
            lock.unlock(); // 잠금은 잎이다 — 로그는 놓은 뒤에 쓴다
            SW_LOG_WARNING( "Variable %# is already registered.", string( name ).c_str() );
            return false;
        }

        string strName{ name };

        // 맵이 아니라 **이 객체**가 주소의 주인이다. 맵이 재할당돼도 findVariable 이 내준 포인터가 유효하려면, 값이 밀집
        // 배열 안에 있으면 안 된다.
        unique_ptr<GlobalVariableInfo> pInfo = make_unique<GlobalVariableInfo>();
        pInfo->_name                         = strName;
        pInfo->_type                         = type;
        pInfo->_pData                        = pData;
        pInfo->_defaultValue                 = defaultValue;
        pInfo->_description                  = string{ description };
        pInfo->_enumType                     = string{ enumType };
        pInfo->_moduleName                   = string{ moduleName };
        pInfo->_typeSize                     = typeSize;
        pInfo->_bTestOnly                    = bTestOnly;
        pInfo->_pEnumTextParser              = type == GlobalVariableType::Enum ? &_enumTextParser : nullptr;

        const auto [iter, bInserted] = _mapVariable.emplace( strName, std::move( pInfo ) );
        _registrationGeneration.fetch_add( 1, std::memory_order_release );

        // 모듈(EditorModule · SWGame)이 선언한 변수는 커맨드라인 파싱이 **이미 끝난 뒤** 여기로 온다. 그 값은
        // CommandLineManager 의 보류표에 남아 있으므로 등록 직후 꺼내 적용한다. 이것이 없으면 `-gv_editorPanelDump=25` 같은
        // 모듈 스위치가 조용히 무시된다. 엔진 자체의 변수는 registerToCommandLine 전에 등록되므로 여기에 걸리지 않고
        // updateFromCommandLine 이 맡는다. 잠금 안이므로 매니저의 setValueFromString(다시 잠근다)이 아니라 info 쪽을 직접 부른다.
        if ( _pCmdLineManager != nullptr && bInserted )
        {
            string pendingValue;
            if ( _pCmdLineManager->findPendingGlobalValue( strName, pendingValue ) )
            {
                // 값이 타입에 맞지 않으면 여기서 알려 준다. 그러지 않으면 값을 빠뜨린 `-gv_editorPanelDump`(보류표에 "true" 로
                // 남는다)가 int 변수에 닿아 조용히 실패하고, 사용자에게는 "스위치가 아무 일도 하지 않는다" 로만 보인다.
                //
                // 변경 알림은 여기서 따로 부르지 않는다. setValueFromString 이 거치는 setValueAs* 네 함수가 모두 이미
                // _onValueChanged 를 부른다(여기서 한 번 더 부르면 모듈 변수만 콜백이 **두 번** 온다).
                if ( iter->second->setValueFromCommandLine( pendingValue ) == false )
                {
                    SW_LOG_WARNING( "-%#=%# : the value does not match the variable type. Ignored",
                                    strName.c_str(), pendingValue.c_str() );
                }
            }
        }

        return true;
    }

    /**
     * @brief DLL · 모듈이 로드될 때 정적 초기화로 연결된 체인(pHead)의 변수들을 한꺼번에 등록합니다.
     */
    void GlobalVariableManager::registerPendingVariables( string_view moduleName, const GlobalVariableRegistrar* pHead )
    {
        const GlobalVariableRegistrar* pCurrent = pHead;
        while ( pCurrent != nullptr )
        {
            // 이름 중복 · 모르는 형식은 registerVariable 이 경고로 남긴다 — 나머지 변수는 이어서 등록한다.
            (void)registerVariable( pCurrent->_name,
                                    pCurrent->_type,
                                    pCurrent->_pData,
                                    pCurrent->_defaultValue,
                                    pCurrent->_description,
                                    pCurrent->_enumType,
                                    moduleName,
                                    pCurrent->_typeSize,
                                    pCurrent->_bTestOnly );
            pCurrent = pCurrent->_pNext;
        }
    }

    /**
     * @brief 특정 모듈(예: 언로드되는 SWGame.dll, EditorModule.dll)에 속한 전역 변수들을 한꺼번에 해제합니다.
     *
     * 모듈을 언로드할 때 댕글링 포인터에 접근하지 않도록 반드시 불러야 합니다(핫 리로드 안전성).
     */
    void GlobalVariableManager::unregisterVariablesByModule( string_view moduleName )
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        string                   strModule{ moduleName };
        for ( auto it = _mapVariable.begin(); it != _mapVariable.end(); )
        {
            if ( it->second->_moduleName == strModule )
                it = _mapVariable.erase( it );
            else
                ++it;
        }
        // 지운 것이 없어도 올린다 — 언로드는 드물고, 캐시한 포인터를 버리게 하는 쪽이 안전하다.
        _registrationGeneration.fetch_add( 1, std::memory_order_release );
    }

    /**
     * @brief 이름으로 변수를 찾아 문자열 값을 파싱해 설정합니다.
     */
    bool GlobalVariableManager::setValueFromString( string_view name, string_view strValue )
    {
        GlobalVariableInfo* pInfo = findVariable( name );
        if ( pInfo != nullptr )
            return pInfo->setValueFromString( strValue );
        return false;
    }

    /**
     * @brief 특정 변수를 기본값으로 되돌립니다.
     */
    bool GlobalVariableManager::resetToDefault( string_view name )
    {
        GlobalVariableInfo* pInfo = findVariable( name );
        if ( pInfo != nullptr )
        {
            pInfo->resetToDefault();
            return true;
        }
        return false;
    }

    /**
     * @brief 등록된 모든 변수를 기본값으로 되돌립니다.
     */
    void GlobalVariableManager::resetAllToDefault()
    {
        // 1단계: unique_lock 안에서 값만 직접 되돌리고, 부를 콜백 목록을 모은다
        vector<pair<GlobalVariableChangedDelegate, GlobalVariableInfo*>> listPendingCallback;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            for ( auto& [name, info] : _mapVariable )
            {
                if ( info->_pData == nullptr )
                    continue;

                GlobalVariableTypeTable::getRow( info->_type )._writeDefault( *info );

                if ( info->_onValueChanged.isBound() )
                    listPendingCallback.push_back( { info->_onValueChanged, info.get() } );
            }
        } // unique_lock 해제

        // 2단계: 락 밖에서 콜백을 부른다(재진입 안전)
        for ( auto& [delegate, pInfo] : listPendingCallback )
        {
            delegate( pInfo );
        }
    }

    /**
     * @brief 이름(string_view)으로 전역 변수 정보를 찾습니다.
     *
     * std::shared_lock 이라 여러 스레드가 동시에 읽을 수 있고, 이종 조회라 임시 string 을 만들지 않습니다.
     */
    void GlobalVariableManager::setEnumTextParser( const GlobalVariableEnumTextParser& parser )
    {
        _enumTextParser = parser;
    }

    bool GlobalVariableManager::applyPendingEnumText()
    {
        bool bAllApplied{ true };
        for ( const auto& [name, text] : _mapPendingEnumText )
        {
            GlobalVariableInfo* pInfo = findVariable( name );
            if ( pInfo == nullptr || pInfo->setValueFromCommandLine( text ) == false )
            {
                SW_LOG_ERROR( "-%#=%# : not applied, %# has no enumerator of that name", name.c_str(), text.c_str(), pInfo != nullptr ? pInfo->_enumType.c_str() : "the variable" );
                bAllApplied = false;
            }
        }
        _mapPendingEnumText.clear();
        return bAllApplied;
    }

    GlobalVariableInfo* GlobalVariableManager::findVariable( string_view name )
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        const auto                     iter = _mapVariable.find( name );
        if ( iter != _mapVariable.end() )
            return iter->second.get();
        return nullptr;
    }

    void GlobalVariableManager::collectVariables( vector<GlobalVariableInfo*>& outListVariable ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        outListVariable.clear();
        outListVariable.reserve( _mapVariable.size() );
        for ( const auto& [name, pInfo] : _mapVariable )
        {
            outListVariable.push_back( pInfo.get() );
        }
    }

    /**
     * @brief 등록된 변수 수를 반환합니다(스레드 안전).
     */
    uint32 GlobalVariableManager::getVariableCount() const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        return static_cast<uint32>( _mapVariable.size() );
    }

    // ============================================================================
    // GlobalVariableRegistrar 구현부
    // ============================================================================

    GlobalVariableRegistrar::GlobalVariableRegistrar( const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType, const utf8* pModuleName, uint32 typeSize, bool bTestOnly )
        : _name{ pName }
        , _type{ type }
        , _pData{ pData }
        , _defaultValue{ defaultValue }
        , _description{ pDescription }
        , _enumType{ pEnumType }
        , _moduleName{ pModuleName }
        , _typeSize{ typeSize }
        , _bTestOnly{ bTestOnly }
        , _pNext{ getHead() }
    {
        getHead() = this;
    }

    GlobalVariableRegistrar*& GlobalVariableRegistrar::getHead()
    {
        static GlobalVariableRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }
} // namespace sw
