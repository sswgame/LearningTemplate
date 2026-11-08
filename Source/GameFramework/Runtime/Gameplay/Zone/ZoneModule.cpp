#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/LogOnce.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityActions.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

#include <cmath>

namespace sw
{
    namespace
    {
        struct ZoneModuleInternal
        {
            static constexpr const utf8* kBareCondition = "zoneValue"; ///< 식의 그냥 이름(`target.FireResist`)을 받는 조건
            static constexpr const utf8* kTargetPrefix  = "target.";   ///< 대상 속성을 읽는 이름의 머리

            /** @brief 등록 목록입니다(함수 정적 — 다른 TU · DLL 의 정적 등록자가 먼저 쓴다). 워커가 읽으므로 고치고 읽을 때 목록의 주인 잠금(`getOwnerMutex`)을 잡는다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const ZoneModuleProcessor, "ZoneModuleRegistry", RegistrationOrder::Insertion, true )

                static const ZoneSample* findSample( const GameplayContext& context )
            {
                return context.find<ZoneSampleSlot>();
            }

            static float64 readDepth01( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_zoneDepth01 ) : std::nan( "" );
            }

            static float64 readDistanceToCenter( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_distanceToCenter ) : std::nan( "" );
            }

            static float64 readImmersion( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_immersion ) : std::nan( "" );
            }

            static float64 readTimeInside( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_timeInside ) : std::nan( "" );
            }

            static float64 readImpactSpeed( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_impactSpeed ) : std::nan( "" );
            }

            static float64 readContactNormal( const GameplayContext& context, const VocabularyNoParams& /*args*/ )
            {
                const ZoneSample* pSample = findSample( context );
                return pSample != nullptr ? static_cast<float64>( pSample->_contactNormalUp ) : std::nan( "" );
            }

            /** @brief 그냥 이름 — `target.<속성>` 은 대상의 어빌리티 속성(current)이고, 없으면 값이 없음(NaN — 어떤 비교에도 거짓)입니다. */
            static float64 readZoneValue( const GameplayContext& context, const ConditionNameArg& args )
            {
                const ZoneSample* pSample = findSample( context );
                const string_view name    = args._name.view();
                const bool        bTarget = StringUtil::startsWith( name, kTargetPrefix );
                if ( pSample == nullptr || pSample->_pTarget == nullptr || bTarget == false )
                    return std::nan( "" );
                const AbilityComponent* pAbility = pSample->_pTarget->getComponent<AbilityComponent>();
                if ( pAbility == nullptr )
                    return std::nan( "" );
                const optional<float32> value = pAbility->getAttributeValue( hashed_string( name.substr( string_view( kTargetPrefix ).size() ) ) );
                return value.has_value() ? static_cast<float64>( *value ) : std::nan( "" );
            }

            /** @brief `x,y,z` 를 읽습니다. 셋이 아니거나 수가 아니면 false 입니다. */
            static bool parseVector( string_view text, float3& outVector )
            {
                const vector<string_view> listToken = StringUtil::split( text, ",", StringSplitOptions{ true, false } );
                float32                   arrValue[3]{};
                if ( listToken.size() != 3 )
                    return false;
                for ( size_t index = 0; index < 3; ++index )
                {
                    if ( StringUtil::parseFloat( listToken[index], arrValue[index] ) == false )
                        return false;
                }
                outVector = float3{ arrValue[0], arrValue[1], arrValue[2] };
                return true;
            }

            /** @brief `;` 로 나눈 동작 목록을 읽고 카탈로그 참조를 봅니다. 하나라도 틀리면 false 입니다. */
            static bool compileActions( string_view text, const string& sourceName, const AbilityCatalog* pCatalog, vector<GameActionCall>& outListCall )
            {
                outListCall.clear();
                bool bOk = true;
                for ( const string_view token : StringUtil::split( text, ";", StringSplitOptions{ true, true } ) )
                {
                    GameActionCall call;
                    if ( GameActionCall::compileText( token, sourceName, call ) == false || AbilityActions::validateReferences( call, pCatalog, sourceName ) == false )
                    {
                        bOk = false;
                        continue;
                    }
                    outListCall.push_back( call );
                }
                return bOk;
            }

            /** @brief 형식 @p kind 대로 @p text 를 @p outParam 에 해석합니다. */
            static bool compileParam( ZoneParamKind kind, string_view text, const string& sourceName, const AbilityCatalog* pCatalog, ZoneCompiledParam& outParam )
            {
                outParam._kind = kind;
                outParam._text = string( text );
                switch ( kind )
                {
                    case ZoneParamKind::Number:
                        return ConditionExpr::compile( text, sourceName, outParam._expr, kBareCondition );
                    case ZoneParamKind::Text:
                        return true;
                    case ZoneParamKind::Vector:
                        if ( parseVector( text, outParam._vector ) )
                            return true;
                        SW_LOG_ERROR_ID( "Zone.VectorInvalid", "%#: '%#' is not x,y,z", sourceName, text );
                        return false;
                    case ZoneParamKind::Actions:
                        return compileActions( text, sourceName, pCatalog, outParam._listCall );
                }
                return false;
            }

            /** @brief 모듈 하나를 처리기 표로 해석합니다. */
            static bool compileModule( const ZoneModuleDef& def, const string& profileName, const AbilityCatalog* pCatalog, ZoneCompiledModule& outModule )
            {
                StringBuilder<constant::buffer::kMax256> sourceNameBuilder;
                sourceNameBuilder.appendFormat( "%# module '%#'", profileName, def._name.c_str() );
                const string sourceName{ sourceNameBuilder.view() };
                outModule._name       = def._name;
                outModule._kind       = def._kind;
                outModule._pProcessor = ZoneModuleRegistry::find( def._kind.view() );
                outModule._target     = def._target == ZoneTargetRole::Inherit ? ZoneTargetRole::Occupant : def._target;
                if ( outModule._pProcessor == nullptr )
                {
                    SW_LOG_ERROR_ID( "Zone.ModuleKindUnknown", "%#: kind '%#' has no processor (known: %#)", sourceName, def._kind.c_str(), ZoneModuleRegistry::joinNames() );
                    return false;
                }
                bool bOk = ConditionExpr::compile( def._condition, sourceName, outModule._condition, kBareCondition );

                vector<ZoneParamSpec> listSpec;
                outModule._pProcessor->collectParamSpecs( listSpec );
                for ( const ZoneParamSpec& spec : listSpec )
                {
                    ZoneCompiledParam   param;
                    const ZoneParamDef* pDef = def.findParam( hashed_string( spec._pName ) );
                    param._name              = hashed_string( spec._pName );
                    if ( pDef == nullptr && spec._pDefault == nullptr )
                    {
                        SW_LOG_ERROR_ID( "Zone.ParamMissing", "%#: kind '%#' needs parameter '%#'", sourceName, def._kind.c_str(), spec._pName );
                        bOk = false;
                        continue;
                    }
                    bOk = compileParam( spec._kind, pDef != nullptr ? string_view( pDef->_value ) : string_view( spec._pDefault ), sourceName, pCatalog, param ) && bOk;
                    outModule._listParam.push_back( std::move( param ) );
                }
                // 표 밖의 이름 — 할 일 모듈이면 동작이 읽는 값(식), 아니면 오타다.
                for ( const ZoneParamDef& paramDef : def._listParam )
                {
                    const bool bDeclared = std::any_of( listSpec.begin(), listSpec.end(), [&paramDef]( const ZoneParamSpec& spec )
                    { return paramDef._name == hashed_string( spec._pName ); } );
                    if ( bDeclared )
                        continue;
                    if ( outModule._pProcessor->supportsActionParams() == false )
                    {
                        SW_LOG_ERROR_ID( "Zone.ParamUnknown", "%#: kind '%#' has no parameter '%#'", sourceName, def._kind.c_str(), paramDef._name.c_str() );
                        bOk = false;
                        continue;
                    }
                    ZoneCompiledParam param;
                    param._name        = paramDef._name;
                    param._bExtraValue = SW_TRUE;
                    bOk                = compileParam( ZoneParamKind::Number, paramDef._value, sourceName, pCatalog, param ) && bOk;
                    outModule._listParam.push_back( std::move( param ) );
                }
                if ( def._listCase.empty() == false && outModule._pProcessor->supportsCaseList() == false )
                {
                    SW_LOG_ERROR_ID( "Zone.CasesNotAccepted", "%#: kind '%#' does not take cases", sourceName, def._kind.c_str() );
                    bOk = false;
                }
                for ( const ZoneCaseDef& caseDef : def._listCase )
                {
                    ZoneCompiledCase compiled;
                    compiled._name = caseDef._name;
                    bOk            = ConditionExpr::compile( caseDef._condition, sourceName, compiled._condition, kBareCondition ) && bOk;
                    bOk            = compileActions( caseDef._do, sourceName, pCatalog, compiled._listCall ) && bOk;
                    outModule._listCase.push_back( std::move( compiled ) );
                }
                return bOk && outModule._pProcessor->checkModule( outModule, sourceName );
            }
        };
    } // namespace

    SW_CONDITION( zoneDepth01, VocabularyNoParams, &ZoneModuleInternal::readDepth01 );
    SW_CONDITION( distanceToCenter, VocabularyNoParams, &ZoneModuleInternal::readDistanceToCenter );
    SW_CONDITION( immersion, VocabularyNoParams, &ZoneModuleInternal::readImmersion );
    SW_CONDITION( timeInside, VocabularyNoParams, &ZoneModuleInternal::readTimeInside );
    SW_CONDITION( impactSpeed, VocabularyNoParams, &ZoneModuleInternal::readImpactSpeed );
    SW_CONDITION( contactNormal, VocabularyNoParams, &ZoneModuleInternal::readContactNormal );
    SW_CONDITION( zoneValue, ConditionNameArg, &ZoneModuleInternal::readZoneValue );
} // namespace sw

namespace sw
{
    void ZoneModuleRegistry::registerProcessor( const ZoneModuleProcessor* pProcessor )
    {
        RegistrationList<const ZoneModuleProcessor>& registrations = ZoneModuleInternal::getRegistrations();
        ScopedLock<mutex>                            lock{ registrations.getOwnerMutex() };
        (void)registrations.addLogged( pProcessor, "ZoneModule", pProcessor->getKindName() ); // 거절은 공용 오류 로그가 알린다
    }

    void ZoneModuleRegistry::unregisterProcessor( const ZoneModuleProcessor* pProcessor )
    {
        RegistrationList<const ZoneModuleProcessor>& registrations = ZoneModuleInternal::getRegistrations();
        ScopedLock<mutex>                            lock{ registrations.getOwnerMutex() };
        (void)registrations.remove( pProcessor ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    const ZoneModuleProcessor* ZoneModuleRegistry::find( string_view kindName )
    {
        RegistrationList<const ZoneModuleProcessor>& registrations = ZoneModuleInternal::getRegistrations();
        ScopedLock<mutex>                            lock{ registrations.getOwnerMutex() };
        return registrations.findByName( kindName );
    }

    string ZoneModuleRegistry::joinNames()
    {
        RegistrationList<const ZoneModuleProcessor>& registrations = ZoneModuleInternal::getRegistrations();
        ScopedLock<mutex>                            lock{ registrations.getOwnerMutex() };
        string                                       joined;
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            if ( index > 0 )
                joined += ", ";
            joined += registrations.getNameAt( index );
        }
        return joined;
    }

    ZoneModuleRegistrar::ZoneModuleRegistrar( const ZoneModuleProcessor* pProcessor )
        : _pProcessor{ pProcessor }
    {
        ZoneModuleRegistry::registerProcessor( pProcessor );
    }

    ZoneModuleRegistrar::~ZoneModuleRegistrar()
    {
        ZoneModuleRegistry::unregisterProcessor( _pProcessor );
    }
} // namespace sw

namespace sw
{
    const ZoneCompiledParam* ZoneCompiledModule::findParam( const hashed_string& name ) const
    {
        for ( const ZoneCompiledParam& param : _listParam )
        {
            if ( param._name == name )
                return &param;
        }
        return nullptr;
    }

    float32 ZoneCompiledModule::evaluateNumber( const hashed_string& name, const GameplayContext& context ) const
    {
        const ZoneCompiledParam* pParam = findParam( name );
        if ( pParam == nullptr || pParam->_kind != ZoneParamKind::Number )
            return 0.0f;
        const float64 value = pParam->_expr.evaluateNumber( context );
        return std::isnan( value ) ? 0.0f : static_cast<float32>( value ); // 값이 없음(대상에 없는 속성)은 영향 없음이다 — 처리기가 0 을 "하지 않음" 으로 읽게 기본값을 둔다
    }

    const string& ZoneCompiledModule::getText( const hashed_string& name ) const
    {
        static const string      s_empty;
        const ZoneCompiledParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_text : s_empty;
    }

    float3 ZoneCompiledModule::getVector( const hashed_string& name ) const
    {
        const ZoneCompiledParam* pParam = findParam( name );
        return pParam != nullptr ? pParam->_vector : float3{};
    }

    const vector<GameActionCall>& ZoneCompiledModule::getActions( const hashed_string& name ) const
    {
        static const vector<GameActionCall> s_empty;
        const ZoneCompiledParam*            pParam = findParam( name );
        return pParam != nullptr ? pParam->_listCall : s_empty;
    }

    void ZoneCompiledModule::evaluateActionParams( const GameplayContext& context, ActionValues& outValues ) const
    {
        for ( const ZoneCompiledParam& param : _listParam )
        {
            if ( param._bExtraValue == SW_FALSE )
                continue;
            const float64 value = param._expr.evaluateNumber( context );
            if ( outValues.set( param._name, std::isnan( value ) ? 0.0f : static_cast<float32>( value ) ) == false )
                SW_LOG_ERROR_ONCE_ID( "Zone.TooManyValues", "Zone module '%#' has more than %# values for its actions", _name.c_str(), ActionValues::kCapacity );
        }
    }

    bool ZoneCompiledProfile::compile( const ZoneProfile& profile, string_view sourceName, const AbilityCatalog* pCatalog )
    {
        _listModule.clear();
        _sourceName = string( sourceName );
        bool bOk    = true;
        for ( const ZoneModuleDef& def : profile._listModule )
        {
            ZoneCompiledModule module;
            bOk = ZoneModuleInternal::compileModule( def, _sourceName, pCatalog, module ) && bOk;
            _listModule.push_back( std::move( module ) );
        }
        if ( bOk == false )
            _listModule.clear(); // 반쯤 맞은 프로필로 돌지 않는다
        return bOk;
    }

    void ZoneEventContext::invoke( const vector<GameActionCall>& listCall ) const
    {
        for ( const GameActionCall& call : listCall )
        {
            ActionContext actionContext;
            actionContext._world  = _world;
            actionContext._pOwner = _pTarget;
            (void)call.invoke( actionContext ); // 실패(대상에 컴포넌트가 없다 · 무적)는 추적(ActionTrace)이 남긴다 — 다음 할 일은 그대로 한다
        }
    }

    void ZoneEventContext::runFirstMatchingCase( const vector<GameActionCall>& listDefault ) const
    {
        if ( _pModule == nullptr || _pModule->_listCase.empty() )
        {
            invoke( listDefault );
            return;
        }
        for ( const ZoneCompiledCase& caseDef : _pModule->_listCase )
        {
            if ( caseDef._condition.evaluate( _world ) )
            {
                invoke( caseDef._listCall );
                return;
            }
        }
    }
} // namespace sw
