#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/Defines.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

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

            static inline std::shared_mutex s_stringVarMutex;
        };
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
        {
            if ( _typeSize == 1 )
                return *static_cast<const uint8*>( _pData );
            if ( _typeSize == 2 )
                return *static_cast<const uint16*>( _pData );
            if ( _typeSize == 8 )
                return static_cast<int32>( *static_cast<const int64*>( _pData ) );
            return *static_cast<const int32*>( _pData );
        }
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
     * @brief 현재 값을 문자열로 바꿔 반환합니다.
     *
     * std::to_string() 은 힙 메모리를 할당하므로, 스택 기반 StringBuilder<constant::kMaxBuffer32> 로 임시 힙 할당 없이
     * 포맷합니다.
     */
    string GlobalVariableInfo::getValueAsString() const
    {
        if ( _pData == nullptr )
            return "";

        switch ( _type )
        {
            case GlobalVariableType::Boolean:
                return *static_cast<bool*>( _pData ) ? "true" : "false";
            case GlobalVariableType::Int32:
            case GlobalVariableType::Enum:
            {
                // 스택 버퍼로 정수를 바로 문자열로 바꾼다
                StringBuilder<constant::kMaxBuffer32> sb;
                sb.append( getValueAsInt() );
                return string( sb.view() );
            }
            case GlobalVariableType::Float:
            {
                // 스택 버퍼로 실수를 바로 문자열로 바꾼다
                StringBuilder<constant::kMaxBuffer32> sb;
                sb.append( *static_cast<float32*>( _pData ) );
                return string( sb.view() );
            }
            case GlobalVariableType::String:
            {
                std::shared_lock<std::shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
                return *static_cast<string*>( _pData );
            }
            default:
                break;
        }
        return "";
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
            std::unique_lock<std::shared_mutex> lock{ GlobalVariableInternal::s_stringVarMutex };
            *static_cast<string*>( _pData ) = string{ val };
        }
        if ( _onValueChanged.isBound() )
            _onValueChanged( this );
        return true;
    }

    /**
     * @brief 문자열을 파싱해 *_pData 에 값을 씁니다.
     *
     * 값이 바뀌면 등록된 변경 콜백(_onValueChanged)을 부릅니다. StringUtil::parse* 로 할당 없이 파싱합니다.
     */
    bool GlobalVariableInfo::setValueFromString( string_view strValue )
    {
        if ( _pData == nullptr )
            return false;

        switch ( _type )
        {
            case GlobalVariableType::Boolean:
            {
                const bool bVal = StringUtil::parseBool( strValue, false );
                return setValueAsBool( bVal );
            }
            case GlobalVariableType::Int32:
            case GlobalVariableType::Enum:
            {
                int32 val{ 0 };
                if ( StringUtil::parseInt( strValue, val ) )
                    return setValueAsInt( val );
                return false;
            }
            case GlobalVariableType::Float:
            {
                float32 val{ 0.0f };
                if ( StringUtil::parseFloat( strValue, val ) )
                    return setValueAsFloat( val );
                return false;
            }
            case GlobalVariableType::String:
            {
                return setValueAsString( strValue );
            }
            default:
                break;
        }
        return false;
    }

    /**
     * @brief 등록할 때 지정한 기본값(_defaultValue)으로 되돌립니다.
     */
    void GlobalVariableInfo::resetToDefault()
    {
        if ( _pData == nullptr )
            return;

        switch ( _type )
        {
            case GlobalVariableType::Boolean:
            {
                if ( std::holds_alternative<bool>( _defaultValue ) )
                    *static_cast<bool*>( _pData ) = std::get<bool>( _defaultValue );
                break;
            }
            case GlobalVariableType::Int32:
            case GlobalVariableType::Enum:
            {
                if ( std::holds_alternative<int32>( _defaultValue ) )
                {
                    const int32 val = std::get<int32>( _defaultValue );
                    if ( _type == GlobalVariableType::Enum )
                        GlobalVariableInternal::writeEnumValue( _pData, _typeSize, val );
                    else
                        *static_cast<int32*>( _pData ) = val;
                }
                break;
            }
            case GlobalVariableType::Float:
            {
                if ( std::holds_alternative<float32>( _defaultValue ) )
                    *static_cast<float32*>( _pData ) = std::get<float32>( _defaultValue );
                break;
            }
            case GlobalVariableType::String:
            {
                if ( std::holds_alternative<string>( _defaultValue ) )
                    *static_cast<string*>( _pData ) = std::get<string>( _defaultValue );
                break;
            }
            default:
                break;
        }

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
            if ( std::holds_alternative<int32>( info->_defaultValue ) )
                pCmdLineManager->addArgument<int32>( { info->_name }, std::get<int32>( info->_defaultValue ), true );
            else if ( std::holds_alternative<float32>( info->_defaultValue ) )
                pCmdLineManager->addArgument<float32>( { info->_name }, std::get<float32>( info->_defaultValue ), true );
            else if ( std::holds_alternative<bool>( info->_defaultValue ) )
                pCmdLineManager->addArgument<bool>( { info->_name }, std::get<bool>( info->_defaultValue ), true );
            else if ( std::holds_alternative<string>( info->_defaultValue ) )
                pCmdLineManager->addArgument<string>( { info->_name }, string( std::get<string>( info->_defaultValue ) ), true );
        }
    }

    /**
     * @brief 파싱된 커맨드라인 인자 값을 읽어 등록된 전역 변수에 바로 반영합니다.
     *
     * 문자열로 다시 바꿨다가 파싱하는 과정 없이, 원시 포인터(*_pData)에 타입별로 값을 넣은 뒤 변경 알림 콜백을 부릅니다.
     */
    void GlobalVariableManager::updateFromCommandLine( const CommandLineManager* pCmdLineManager )
    {
        if ( pCmdLineManager == nullptr )
            return;

        for ( auto& [name, info] : _mapVariable )
        {
            if ( info->_pData == nullptr )
                continue;
            if ( info->_type == GlobalVariableType::Int32 || info->_type == GlobalVariableType::Enum )
            {
                int32 val{ 0 };
                if ( pCmdLineManager->getArgument( name, val ) )
                {
                    if ( info->_type == GlobalVariableType::Enum )
                        GlobalVariableInternal::writeEnumValue( info->_pData, info->_typeSize, val );
                    else
                        *static_cast<int32*>( info->_pData ) = val;

                    if ( info->_onValueChanged.isBound() )
                        info->_onValueChanged( info.get() );
                }
            }
            else if ( info->_type == GlobalVariableType::Float )
            {
                float32 val{ 0.0f };
                if ( pCmdLineManager->getArgument( name, val ) )
                {
                    *static_cast<float32*>( info->_pData ) = val;
                    if ( info->_onValueChanged.isBound() )
                        info->_onValueChanged( info.get() );
                }
            }
            else if ( info->_type == GlobalVariableType::Boolean )
            {
                bool val{ false };
                if ( pCmdLineManager->getArgument( name, val ) )
                {
                    *static_cast<bool*>( info->_pData ) = val;
                    if ( info->_onValueChanged.isBound() )
                        info->_onValueChanged( info.get() );
                }
            }
            else if ( info->_type == GlobalVariableType::String )
            {
                string val;
                if ( pCmdLineManager->getArgument( name, val ) )
                {
                    *static_cast<string*>( info->_pData ) = std::move( val );
                    if ( info->_onValueChanged.isBound() )
                        info->_onValueChanged( info.get() );
                }
            }
        }
    }

    /**
     * @brief 새 전역 변수를 매니저에 등록합니다.
     *
     * std::unique_lock 을 잡아 동시 등록 경쟁을 막고, string_view 로 조회해 불필요한 string 을 만들지 않고 이미 있는지 확인합니다.
     */
    bool GlobalVariableManager::registerVariable( string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, string_view description, string_view enumType, string_view moduleName, uint32 typeSize )
    {
        if ( name.empty() || pData == nullptr )
            return false;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( _mapVariable.find( name ) != _mapVariable.end() )
        {
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

        const auto [iter, bInserted] = _mapVariable.emplace( strName, std::move( pInfo ) );

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
                // _onValueChanged 를 부른다. 예전에는 여기서 한 번 더 불러서 모듈 변수만 콜백이 **두 번** 왔다.
                if ( iter->second->setValueFromString( pendingValue ) == false )
                {
                    SW_LOG_WARNING( "-%#=%# : 값이 변수 타입과 맞지 않습니다. 무시됩니다",
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
            registerVariable( pCurrent->_name,
                              pCurrent->_type,
                              pCurrent->_pData,
                              pCurrent->_defaultValue,
                              pCurrent->_description,
                              pCurrent->_enumType,
                              moduleName,
                              pCurrent->_typeSize );
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
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        string                              strModule{ moduleName };
        for ( auto it = _mapVariable.begin(); it != _mapVariable.end(); )
        {
            if ( it->second->_moduleName == strModule )
                it = _mapVariable.erase( it );
            else
                ++it;
        }
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
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            for ( auto& [name, info] : _mapVariable )
            {
                if ( info->_pData == nullptr )
                    continue;

                switch ( info->_type )
                {
                    case GlobalVariableType::Boolean:
                    {
                        if ( std::holds_alternative<bool>( info->_defaultValue ) )
                            *static_cast<bool*>( info->_pData ) = std::get<bool>( info->_defaultValue );
                        break;
                    }
                    case GlobalVariableType::Int32:
                    case GlobalVariableType::Enum:
                    {
                        if ( std::holds_alternative<int32>( info->_defaultValue ) )
                        {
                            const int32 val = std::get<int32>( info->_defaultValue );
                            if ( info->_type == GlobalVariableType::Enum )
                                GlobalVariableInternal::writeEnumValue( info->_pData, info->_typeSize, val );
                            else
                                *static_cast<int32*>( info->_pData ) = val;
                        }
                        break;
                    }
                    case GlobalVariableType::Float:
                    {
                        if ( std::holds_alternative<float32>( info->_defaultValue ) )
                            *static_cast<float32*>( info->_pData ) = std::get<float32>( info->_defaultValue );
                        break;
                    }
                    case GlobalVariableType::String:
                    {
                        if ( std::holds_alternative<string>( info->_defaultValue ) )
                            *static_cast<string*>( info->_pData ) = std::get<string>( info->_defaultValue );
                        break;
                    }
                    default:
                        break;
                }

                if ( info->_onValueChanged.isBound() )
                    listPendingCallback.push_back( { info->_onValueChanged, info.get() } );
            }
        } // unique_lock 해제

        // 2단계: 락 밖에서 콜백을 부른다(재진입 안전)
        for ( auto& [delegate, pInfo] : listPendingCallback )
            delegate( pInfo );
    }

    /**
     * @brief 이름(string_view)으로 전역 변수 정보를 찾습니다.
     *
     * std::shared_lock 이라 여러 스레드가 동시에 읽을 수 있고, 이종 조회라 임시 string 을 만들지 않습니다.
     */
    GlobalVariableInfo* GlobalVariableManager::findVariable( string_view name )
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        const auto                          iter = _mapVariable.find( name );
        if ( iter != _mapVariable.end() )
            return iter->second.get();
        return nullptr;
    }

    /**
     * @brief 등록된 변수 이름 목록의 스냅샷을 반환합니다(스레드 안전).
     *
     * 패널 등에서 이 목록을 훑으며 findVariable 로 편집할 포인터를 얻는 용도입니다.
     */
    vector<string> GlobalVariableManager::collectVariableNames() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        vector<string>                      listName;
        listName.reserve( static_cast<uint32>( _mapVariable.size() ) );
        for ( const auto& [name, info] : _mapVariable )
            listName.push_back( name );
        return listName;
    }

    /**
     * @brief 등록된 변수 수를 반환합니다(스레드 안전).
     */
    uint32 GlobalVariableManager::getVariableCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        return static_cast<uint32>( _mapVariable.size() );
    }

    // ============================================================================
    // GlobalVariableRegistrar 구현부
    // ============================================================================

    GlobalVariableRegistrar::GlobalVariableRegistrar( const utf8* pName, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, string>& defaultValue, const utf8* pDescription, const utf8* pEnumType, const utf8* pModuleName, uint32 typeSize )
        : _name{ pName }
        , _type{ type }
        , _pData{ pData }
        , _defaultValue{ defaultValue }
        , _description{ pDescription }
        , _enumType{ pEnumType }
        , _moduleName{ pModuleName }
        , _typeSize{ typeSize }
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
