#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Input/Map/InputMap.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

#include <algorithm>

namespace sw
{
    ExperienceScope::ExperienceScope( SubsystemScope& gameScope, GlobalVariableManager& variables, const GamePackConfig& packConfig, InputMap* pInputMap,
                                      const ExperienceDefinition& definition )
        : _gameScope( gameScope )
        , _variables( variables )
        , _packConfig( packConfig )
        , _pInputMap( pInputMap )
        , _definition{ definition }
        , _listCleanup{}
        , _listHeldCatalog{}
        , _listLoadedCatalog{}
        , _before{}
        , _activated{}
        , _bActivated{ SW_FALSE }
        , _bDeactivated{ SW_FALSE }
    {
        _before = captureState();
    }

    ExperienceScope::~ExperienceScope()
    {
        (void)deactivate(); // 남은 것은 검사가 이미 오류로 알렸다
    }

    bool ExperienceScope::overrideGlobalVariable( string_view name, string_view value )
    {
        GlobalVariableInfo* const pVariable = _variables.findVariable( name );
        if ( pVariable == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceScope.UnknownGlobalVariable", "Experience '%#': global variable '%#' does not exist", _definition._name.c_str(), name );
            return false;
        }
        string previous = pVariable->getValueAsString();
        if ( pVariable->setValueFromString( value ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceScope.GlobalVariableRejected", "Experience '%#': global variable '%#' does not take '%#'", _definition._name.c_str(), name, value );
            return false;
        }
        // 이름으로 다시 찾는다 — 변수를 등록한 모듈이 그 사이 다시 올라왔으면 옛 칸을 건드리지 않는다.
        addCleanup( Delegate<void()>::create( [this, variableName = string( name ), previousValue = std::move( previous )]()
        {
            GlobalVariableInfo* const pRestored = _variables.findVariable( variableName );
            if ( pRestored != nullptr )
                (void)pRestored->setValueFromString( previousValue ); // 앞 값은 이 변수가 낸 글이라 받는다
        } ) );
        return true;
    }

    void ExperienceScope::addCleanup( Delegate<void()> cleanup )
    {
        _listCleanup.push_back( std::move( cleanup ) );
    }

    void ExperienceScope::finishActivation()
    {
        _activated  = captureState();
        _bActivated = SW_TRUE;
    }

    uint32 ExperienceScope::deactivate()
    {
        if ( _bDeactivated == SW_TRUE )
            return 0;
        if ( _bActivated == SW_FALSE )
            finishActivation(); // 켜기 도중에 진 범위 — 그때까지 건 것이 켠 몫이다
        // 되돌릴 일이 또 되돌릴 일을 더하지 않는다 — 그래도 끝에서부터 하나씩 빼며 부른다(도중에 늘어도 새 것부터 거둔다).
        while ( _listCleanup.empty() == false )
        {
            const Delegate<void()> cleanup = std::move( _listCleanup.back() );
            _listCleanup.pop_back();
            cleanup();
        }
        _listHeldCatalog.clear(); // 범위의 칸을 푼 뒤에 놓는다 — 찾는 쪽이 이제 없다
        _listLoadedCatalog.clear();
        _bDeactivated = SW_TRUE;
        return reportLeaks( captureState() );
    }

    ExperienceScope::StateSnapshot ExperienceScope::captureState() const
    {
        StateSnapshot snapshot;
        _gameScope.collectBoundTypes( snapshot._listServiceType );
        if ( _pInputMap != nullptr )
        {
            _pInputMap->collectLayerStack( snapshot._listInputLayer );
            _pInputMap->collectEnabledLayers( snapshot._listEnabledInputLayer );
        }
        vector<GlobalVariableInfo*> listVariable;
        _variables.collectVariables( listVariable );
        snapshot._listGlobalValue.reserve( listVariable.size() );
        for ( const GlobalVariableInfo* pVariable : listVariable )
            snapshot._listGlobalValue.push_back( GlobalValue{ pVariable->_name, pVariable->getValueAsString() } );
        // 이름 순 — 사이에 모듈이 변수를 더하거나 빼도 이름으로 맞춰 본다
        std::sort( snapshot._listGlobalValue.begin(), snapshot._listGlobalValue.end(), []( const GlobalValue& left, const GlobalValue& right )
        { return left._name < right._name; } );
        return snapshot;
    }

    uint32 ExperienceScope::reportLeaks( const StateSnapshot& after ) const
    {
        uint32 leakCount = 0;
        for ( const uint64 typeHash : _activated._listServiceType )
        {
            if ( VectorUtil::contains( _before._listServiceType, typeHash ) || VectorUtil::contains( after._listServiceType, typeHash ) == false )
                continue;
            SW_LOG_ERROR_ID( "ExperienceScope.LeakedService", "Experience '%#': a service of type hash %# is still bound in the game scope after leaving - it was bound outside ExperienceScope::bindService",
                             _definition._name.c_str(), typeHash );
            ++leakCount;
        }
        for ( const hashed_string& layer : _activated._listInputLayer )
        {
            if ( VectorUtil::contains( _before._listInputLayer, layer ) || VectorUtil::contains( after._listInputLayer, layer ) == false )
                continue;
            SW_LOG_ERROR_ID( "ExperienceScope.LeakedInputLayer", "Experience '%#': input layer '%#' is still pushed after leaving - it was pushed without a scope cleanup", _definition._name.c_str(),
                             layer.c_str() );
            ++leakCount;
        }
        for ( const hashed_string& layer : _activated._listEnabledInputLayer )
        {
            // 스택에 남은 것은 위에서 알렸다 — 여기는 push 없이 켠(`setLayerEnabled`) 레이어다.
            if ( VectorUtil::contains( _before._listEnabledInputLayer, layer ) || VectorUtil::contains( after._listEnabledInputLayer, layer ) == false ||
                 VectorUtil::contains( after._listInputLayer, layer ) )
                continue;
            SW_LOG_ERROR_ID( "ExperienceScope.LeakedEnabledInputLayer",
                             "Experience '%#': input layer '%#' is still enabled after leaving though it is not on the layer stack - it was enabled without a scope cleanup (use pushLayer/popLayer)",
                             _definition._name.c_str(), layer.c_str() );
            ++leakCount;
        }
        const auto findValue = []( const vector<GlobalValue>& listValue, const string& name ) -> const GlobalValue*
        {
            const auto it = std::lower_bound( listValue.begin(), listValue.end(), name, []( const GlobalValue& value, const string& key )
            { return value._name < key; } );
            return ( it != listValue.end() && it->_name == name ) ? &*it : nullptr;
        };
        for ( const GlobalValue& activated : _activated._listGlobalValue )
        {
            const GlobalValue* const pBefore = findValue( _before._listGlobalValue, activated._name );
            const GlobalValue* const pAfter  = findValue( after._listGlobalValue, activated._name );
            if ( pBefore == nullptr || pAfter == nullptr || pBefore->_value == activated._value || pAfter->_value == pBefore->_value )
                continue; // 켜기가 바꾸지 않았거나 되돌아왔다(켜는 사이 생기거나 사라진 변수는 모듈의 것이다)
            SW_LOG_ERROR_ID( "ExperienceScope.LeakedGlobalVariable", "Experience '%#': global variable '%#' is '%#' after leaving (was '%#' before entering) - it was set outside ExperienceScope::overrideGlobalVariable",
                             _definition._name.c_str(), activated._name.c_str(), pAfter->_value.c_str(), pBefore->_value.c_str() );
            ++leakCount;
        }
        return leakCount;
    }

    bool ExperienceScope::resolveCatalogPath( const utf8* pKind, string& outPath ) const
    {
        const hashed_string kind{ pKind };
        if ( VectorUtil::contains( _definition._listCatalog, kind ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceScope.CatalogNotDeclared", "Experience '%#': a kit loads catalog '%#' that is not in catalogs=\"...\" - declare it", _definition._name.c_str(), pKind );
            return false;
        }
        const expected<void> path = _packConfig.makeRequiredDataPath( kind.view(), outPath );
        if ( path.has_value() )
            return true;
        SW_LOG_ERROR_ID( "ExperienceScope.CatalogNoPath", "Experience '%#': catalog '%#' has no path: %# (%#)", _definition._name.c_str(), pKind, path.error().message(), path.error().detail() );
        return false;
    }

    void ExperienceScope::reportServiceConflict( uint64 typeHash ) const
    {
        SW_LOG_ERROR_ID( "ExperienceScope.ServiceConflict", "Experience '%#': a service of type hash %# is null or already bound in the game scope (or above) - two kits bind the same type",
                         _definition._name.c_str(), typeHash );
    }

    void ExperienceScope::reportCatalogFailure( const utf8* pKind, const string& path ) const
    {
        SW_LOG_ERROR_ID( "ExperienceScope.CatalogNotLoaded", "Experience '%#': catalog '%#' (%#) could not be loaded (see the messages above)", _definition._name.c_str(), pKind, path.c_str() );
    }
} // namespace sw
