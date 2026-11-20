#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    ExperienceScope::ExperienceScope( SubsystemScope& gameScope, GlobalVariableManager& variables, const GamePackConfig& packConfig, const ExperienceDefinition& definition )
        : _gameScope( gameScope )
        , _variables( variables )
        , _packConfig( packConfig )
        , _definition{ definition }
        , _listCleanup{}
        , _listHeldCatalog{}
        , _listLoadedCatalog{}
    {
    }

    ExperienceScope::~ExperienceScope()
    {
        deactivate();
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

    void ExperienceScope::deactivate()
    {
        // 되돌릴 일이 또 되돌릴 일을 더하지 않는다 — 그래도 끝에서부터 하나씩 빼며 부른다(도중에 늘어도 새 것부터 거둔다).
        while ( _listCleanup.empty() == false )
        {
            const Delegate<void()> cleanup = std::move( _listCleanup.back() );
            _listCleanup.pop_back();
            cleanup();
        }
        _listHeldCatalog.clear(); // 범위의 칸을 푼 뒤에 놓는다 — 찾는 쪽이 이제 없다
        _listLoadedCatalog.clear();
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
