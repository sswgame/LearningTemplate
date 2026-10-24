#include "pch.h"

#include "Engine/Probe/AutomationProbe.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationProbe" );

    namespace
    {
        struct AutomationProbeInternal
        {
            /** @brief 이름의 밑입니다. 끝이 `_` + 0 으로 시작하지 않는 숫자면 그것을 뗀다(`Tree_3` → `Tree`, `Tree_03` · `Tree_` 는 그대로). */
            static string_view computeBaseName( string_view name )
            {
                const size_t underscore = name.rfind( '_' );
                const bool   bHasNumber = underscore != string_view::npos && underscore + 1 < name.size() && name[underscore + 1] != '0';
                if ( bHasNumber == false )
                    return name;
                for ( size_t index = underscore + 1; index < name.size(); ++index )
                {
                    if ( name[index] < '0' || name[index] > '9' )
                        return name;
                }
                return name.substr( 0, underscore );
            }

            /** @brief 밑이 @p baseName 인 살아 있는 오브젝트를 만든 순서(id 오름차순)로 모읍니다. 이름은 오브젝트 저장소처럼 대소문자를 가리지 않는다. */
            static void collectByBaseName( const GameObjectManager& manager, string_view baseName, vector<GameObject*>& outListObject )
            {
                outListObject.clear();
                manager.forEachGameObject( [baseName, &outListObject]( GameObject* pObject )
                {
                    const bool bSameBase = pObject->isPendingDestroy() == false && StringUtil::equals( computeBaseName( pObject->getName().c_str() ), baseName, true );
                    if ( bSameBase )
                        outListObject.push_back( pObject );
                } );
                std::sort( outListObject.begin(), outListObject.end(), []( const GameObject* pLhs, const GameObject* pRhs )
                { return pLhs->getObjectID() < pRhs->getObjectID(); } );
            }

            /** @brief `Name[N]` 이면 밑과 번호를 나눕니다. 대괄호가 없으면 false 이고, 대괄호가 있는데 번호가 틀리면 @p outProblem 을 적고 false 입니다. */
            static bool splitIndex( string_view target, string_view& outBaseName, int32& outIndex, string& outProblem )
            {
                const size_t open = target.find( '[' );
                if ( open == string_view::npos )
                    return false;
                const bool bWellFormed = open > 0 && target.back() == ']' && open + 2 < target.size() &&
                                         StringUtil::parseInt( target.substr( open + 1, target.size() - open - 2 ), outIndex ) && outIndex >= 0;
                if ( bWellFormed == false )
                {
                    utf8 arrProblem[kProblemCapacity]{};
                    formatstring( arrProblem, kProblemCapacity, "target '%#' must be Name[N] with N a non-negative integer", target );
                    outProblem = arrProblem;
                    return false;
                }
                outBaseName = target.substr( 0, open );
                return true;
            }

            /** @brief 고를 수 없다는 실패 줄에 이름을 몇 개까지 적을지입니다. */
            static constexpr size_t kSiblingNameLimit = 4;

            /** @brief 실패 까닭 한 줄의 버퍼 크기입니다. */
            static constexpr uint32 kProblemCapacity = constant::kMaxBuffer512;

            static string describeSiblings( const vector<GameObject*>& listObject )
            {
                string text;
                for ( size_t index = 0; index < listObject.size() && index < kSiblingNameLimit; ++index )
                {
                    text += ( index == 0 ? "" : ", " );
                    text += listObject[index]->getName().c_str();
                }
                if ( listObject.size() > kSiblingNameLimit )
                    text += ", ...";
                return text;
            }

            /** @brief 등록된 탐침(등록 순서 · 이름은 대소문자를 가린다). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. 핫 리로드가 올리는 교체 이미지의 같은 이름은 대체한다(`RegistrationReloadScope`). */
            static RegistrationList<const AutomationProbeRegistration>& getRegistrations()
            {
                static RegistrationList<const AutomationProbeRegistration> s_registration{ "AutomationProbeRegistry", RegistrationOrder::Insertion, true };
                return s_registration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationProbes::registerProbe( const AutomationProbeRegistration* pRegistration )
    {
        const bool bInvalidProbeRegistration =
            pRegistration == nullptr || pRegistration->_pName == nullptr || ( pRegistration->_pFunction == nullptr ) == ( pRegistration->_pQueryFunction == nullptr );
        if ( bInvalidProbeRegistration )
            return false;
        return AutomationProbeInternal::getRegistrations().addLogged( pRegistration, "Automation probe", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void AutomationProbes::unregisterProbe( const AutomationProbeRegistration* pRegistration )
    {
        (void)AutomationProbeInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 탐침을 빼는 것은 할 일이 없다
    }

    const AutomationProbeRegistration* AutomationProbes::find( string_view name )
    {
        return AutomationProbeInternal::getRegistrations().findByName( name );
    }

    bool AutomationProbes::read( const AutomationProbeRegistration& registration, const AutomationProbeQuery& query, AutomationProbeValue& outValue )
    {
        if ( registration._pQueryFunction != nullptr )
            return registration._pQueryFunction( query, outValue );
        float64 number = 0.0;
        if ( registration._pFunction( query._pManager, number ) == false )
            return false;
        outValue.setNumber( number );
        return true;
    }

    GameObject* AutomationTarget::find( const GameObjectManager* pManager, string_view target, AutomationTargetLookup& outLookup, string& outProblem )
    {
        using Internal = AutomationProbeInternal;
        outProblem.clear();
        if ( target.empty() )
        {
            outLookup = AutomationTargetLookup::NotGiven;
            return nullptr;
        }
        if ( pManager == nullptr )
        {
            outLookup  = AutomationTargetLookup::Missing;
            outProblem = "there is no active scene";
            return nullptr;
        }
        vector<GameObject*> listObject;
        string_view         baseName;
        int32               index = 0;
        if ( Internal::splitIndex( target, baseName, index, outProblem ) )
        {
            Internal::collectByBaseName( *pManager, baseName, listObject );
            if ( static_cast<size_t>( index ) < listObject.size() )
            {
                outLookup = AutomationTargetLookup::Found;
                return listObject[static_cast<size_t>( index )];
            }
            outLookup = AutomationTargetLookup::Missing;
            utf8 arrProblem[Internal::kProblemCapacity]{};
            formatstring( arrProblem, Internal::kProblemCapacity, "no object %# (%# live object(s) named %#)", target, listObject.size(), baseName );
            outProblem = arrProblem;
            return nullptr;
        }
        if ( outProblem.empty() == false )
        {
            outLookup = AutomationTargetLookup::Rejected;
            return nullptr;
        }
        GameObject* pObject = pManager->findGameObjectByName( hashed_string( target ) );
        if ( pObject == nullptr )
        {
            outLookup = AutomationTargetLookup::Missing;
            utf8 arrProblem[Internal::kProblemCapacity]{};
            formatstring( arrProblem, Internal::kProblemCapacity, "no object named '%#'", target );
            outProblem = arrProblem;
            return nullptr;
        }
        // 번호를 적지 않은 이름인데 같은 밑의 오브젝트가 여럿이면 어느 것을 뜻했는지 모른다.
        if ( Internal::computeBaseName( target ).size() == target.size() )
        {
            Internal::collectByBaseName( *pManager, target, listObject );
            if ( listObject.size() > 1 )
            {
                outLookup = AutomationTargetLookup::Rejected;
                utf8 arrProblem[Internal::kProblemCapacity]{};
                formatstring( arrProblem, Internal::kProblemCapacity, "%# objects are named %# (%#) - pick one with %#[0..%#]", listObject.size(), target,
                              Internal::describeSiblings( listObject ), target, listObject.size() - 1 );
                outProblem = arrProblem;
                return nullptr;
            }
        }
        outLookup = AutomationTargetLookup::Found;
        return pObject;
    }

    string AutomationProbeQuery::describeMissing( const utf8* pWhat ) const
    {
        utf8 arrProblem[AutomationProbeInternal::kProblemCapacity]{};
        formatstring( arrProblem, AutomationProbeInternal::kProblemCapacity, "'%#' has no %#", _target, pWhat );
        return string( arrProblem );
    }

    AutomationProbeQuery AutomationProbeQuery::make( const GameObjectManager* pManager, string_view target, string_view key )
    {
        AutomationProbeQuery query;
        query._pManager      = pManager;
        query._target        = target;
        query._key           = key;
        query._pTargetObject = AutomationTarget::find( pManager, target, query._targetLookup, query._targetProblem );
        return query;
    }

    AutomationProbeRegistrar::AutomationProbeRegistrar( const AutomationProbeRegistration* pRegistration )
        : _pRegistration{ AutomationProbes::registerProbe( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationProbeRegistrar::~AutomationProbeRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationProbes::unregisterProbe( _pRegistration );
    }
} // namespace sw
