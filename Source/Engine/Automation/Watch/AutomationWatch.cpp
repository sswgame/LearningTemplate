#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct AutomationWatchInternal
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
                VectorUtil::sort( outListObject, []( const GameObject* pLhs, const GameObject* pRhs )
                { return pLhs->getObjectID() < pRhs->getObjectID(); } );
            }

            /** @brief `Name[N]` 이면 밑과 번호를 나누고 true 입니다. 대괄호가 없으면 false 이고, 대괄호가 있는데 번호가 틀리면 그 까닭(대상은 글)입니다. */
            [[nodiscard]] static expected<bool> splitIndex( string_view target, string_view& outBaseName, int32& outIndex )
            {
                const size_t open = target.find( '[' );
                if ( open == string_view::npos )
                    return false;
                const bool bWellFormed = open > 0 && target.back() == ']' && open + 2 < target.size() &&
                                         StringUtil::parseInt( target.substr( open + 1, target.size() - open - 2 ), outIndex ) && outIndex >= 0;
                if ( bWellFormed == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a target must be Name[N] with N a non-negative integer", target } );
                outBaseName = target.substr( 0, open );
                return true;
            }

            /** @brief 고르지 못한 까닭입니다 — 대상(@p pFormat 은 `formatstring` 글꼴)에 무엇을 찾았는지 적는다. */
            template <typename... Args>
            [[nodiscard]] static unexpected<Error> fail( ErrorKind kind, const utf8* pMessage, const utf8* pFormat, Args&&... args )
            {
                utf8 arrDetail[kProblemCapacity]{};
                formatstring( arrDetail, kProblemCapacity, pFormat, std::forward<Args>( args )... );
                return unexpected( Error{ kind, pMessage, string_view( arrDetail ) } );
            }

            /** @brief 고를 수 없다는 실패 줄에 이름을 몇 개까지 적을지입니다. */
            static constexpr size_t kSiblingNameLimit = 4;

            /** @brief 실패 까닭 한 줄의 버퍼 크기입니다. */
            static constexpr uint32 kProblemCapacity = constant::buffer::kMax512;

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

            /** @brief 등록된 감시 값(등록 순서 · 이름은 대소문자를 가린다). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. 핫 리로드가 올리는 교체 이미지의 같은 이름은 대체한다(`RegistrationReloadScope`). */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const AutomationWatchRegistration, "AutomationWatchRegistry", RegistrationOrder::Insertion, true )
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationWatchRegistry::registerWatch( const AutomationWatchRegistration* pRegistration )
    {
        const bool bInvalidWatchRegistration =
            pRegistration == nullptr || pRegistration->_pName == nullptr || ( pRegistration->_pFunction == nullptr ) == ( pRegistration->_pQueryFunction == nullptr );
        if ( bInvalidWatchRegistration )
            return false;
        return AutomationWatchInternal::getRegistrations().addLogged( pRegistration, "Automation watch", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void AutomationWatchRegistry::unregisterWatch( const AutomationWatchRegistration* pRegistration )
    {
        (void)AutomationWatchInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 감시 값을 빼는 것은 할 일이 없다
    }

    const AutomationWatchRegistration* AutomationWatchRegistry::find( string_view name )
    {
        return AutomationWatchInternal::getRegistrations().findByName( name );
    }

    bool AutomationWatchRegistry::read( const AutomationWatchRegistration& registration, const AutomationWatchQuery& query, AutomationWatchValue& outValue )
    {
        if ( registration._pQueryFunction != nullptr )
            return registration._pQueryFunction( query, outValue );
        float64 number = 0.0;
        if ( registration._pFunction( query._pManager, number ) == false )
            return false;
        outValue.setNumber( number );
        return true;
    }

    expected<GameObject*> AutomationTarget::find( const GameObjectManager* pManager, string_view target )
    {
        using Internal = AutomationWatchInternal;
        if ( target.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no target is given" } );
        if ( pManager == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "there is no active scene" } );
        vector<GameObject*>  listObject;
        string_view          baseName;
        int32                index = 0;
        const expected<bool> split = Internal::splitIndex( target, baseName, index );
        if ( split.has_value() == false )
            return unexpected( split.error() );
        if ( *split )
        {
            Internal::collectByBaseName( *pManager, baseName, listObject );
            if ( static_cast<size_t>( index ) < listObject.size() )
                return listObject[static_cast<size_t>( index )];
            return Internal::fail( ErrorKind::NotFound, "no object at that index", "%# (%# live object(s) named %#)", target, listObject.size(), baseName );
        }
        GameObject* pObject = pManager->findGameObjectByName( hashed_string( target ) );
        if ( pObject == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "no object named", target } );
        // 번호를 적지 않은 이름인데 같은 밑의 오브젝트가 여럿이면 어느 것을 뜻했는지 모른다.
        if ( Internal::computeBaseName( target ).size() == target.size() )
        {
            Internal::collectByBaseName( *pManager, target, listObject );
            if ( listObject.size() > 1 )
                return Internal::fail( ErrorKind::InvalidArgument, "several objects share the name - pick one with Name[N]", "%# objects are named %# (%#) - %#[0..%#]",
                                       listObject.size(), target, Internal::describeSiblings( listObject ), target, listObject.size() - 1 );
        }
        return pObject;
    }

    AutomationTargetLookup AutomationTarget::classifyLookupError( const Error& error )
    {
        return error.kind() == ErrorKind::NotFound ? AutomationTargetLookup::Missing : AutomationTargetLookup::Rejected;
    }

    string AutomationTarget::describe( const Error& error )
    {
        return error.detail().empty() ? string( error.message() ) : string( error.message() ) + ": " + string( error.detail() );
    }

    string AutomationWatchQuery::describeMissing( const utf8* pWhat ) const
    {
        utf8 arrProblem[AutomationWatchInternal::kProblemCapacity]{};
        formatstring( arrProblem, AutomationWatchInternal::kProblemCapacity, "'%#' has no %#", _target, pWhat );
        return string( arrProblem );
    }

    AutomationWatchQuery AutomationWatchQuery::make( const GameObjectManager* pManager, string_view target, string_view key )
    {
        AutomationWatchQuery query;
        query._pManager = pManager;
        query._target   = target;
        query._key      = key;
        if ( target.empty() )
            return query; // 대상이 없는 질의다(`NotGiven`)
        const expected<GameObject*> found = AutomationTarget::find( pManager, target );
        if ( found.has_value() )
        {
            query._pTargetObject = *found;
            query._targetLookup  = AutomationTargetLookup::Found;
        }
        else
        {
            query._targetLookup  = AutomationTarget::classifyLookupError( found.error() );
            query._targetProblem = AutomationTarget::describe( found.error() );
        }
        return query;
    }
} // namespace sw
