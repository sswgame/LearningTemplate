#include "pch.h"

#include "Engine/Object/Component/Component.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    Component::Component()
        : _pOwner{ nullptr }
        , _componentId{ _s_nextComponentId.fetch_add( 1, std::memory_order_relaxed ) }
        , _componentName{}
        , _pTypeInfo{ nullptr }
        , _pPool{ nullptr }
        , _subTickActiveMask{ 0 }
        , _bActive{ true }
        , _bIsPendingDestroy{ false }
        , _tickGroup{ TickGroup::DuringPhysics }
        , _bCanEverTick{ SW_TRUE }
        , _bIsSceneComponent{ SW_FALSE }
        , _bHasBegunPlay{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _listSubTick{}
    {
        // **여기서 기본값을 적용하지 않는다.** 예전에는 `initialize()` 를 불렀고 그 안에서 가상
        // `getTypeInfo()` 를 썼는데, 생성 중에는 객체가 아직 Component 라 **파생 타입이 아니라
        // 기반 타입의 TypeInfo** 가 나온다. 즉 MeshComponent 를 만들어도 "Component" 이름으로
        // 기본값을 찾았다.
        //
        // 실제 생성 경로는 타입을 아는 쪽이 이미 올바르게 넘겨 준다.
        // `GameObject::addComponent<T>` 와 `GameObjectManager` 의 이름 기반 생성이 둘 다
        // `applyTypeDefaults( 파생 TypeInfo )` 를 부른다. 생성자 호출은 중복이면서 틀린 조회였다.
        //
        // 기반 타입 노드(`<SceneComponent>` 같은)도 기본값을 가질 수 있으므로, 상속 체인을
        // 뿌리 → 파생 순서로 적용하는 일은 `ComponentDefaults::apply` 가 맡는다.
    }

    void Component::setDefaultGamedataPath( string_view path )
    {
        ComponentDefaults::setDefaultsPath( path );
    }

    string Component::getDefaultGamedataPath()
    {
        return ComponentDefaults::getDefaultsPath();
    }

    void Component::onBeginPlay()
    {
    }

    void Component::onEndPlay()
    {
    }

    void Component::dispatchBeginPlay()
    {
        if ( _bHasBegunPlay == SW_TRUE || isPendingDestroy() )
            return;
        _bHasBegunPlay = SW_TRUE;
        onBeginPlay();
    }

    void Component::dispatchEndPlay()
    {
        if ( _bHasBegunPlay == SW_FALSE )
            return;
        _bHasBegunPlay = SW_FALSE;
        onEndPlay();
    }

    void Component::onTick( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void Component::onSubTick( uint32 subTickId, float32 deltaTime )
    {
        (void)subTickId;
        (void)deltaTime;
    }

    SubTickHandle Component::registerSubTick( TickGroup group, uint32 subTickId, TickPhase phase, uint8 priority )
    {
        if ( subTickId == 0 )
            return {};

        if ( subTickId < 64 )
            _subTickActiveMask.fetch_or( 1ULL << subTickId, std::memory_order_relaxed );

        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
            {
                info._group    = group;
                info._phase    = phase;
                info._priority = priority;
                info._bActive  = SW_TRUE;
                if ( _pOwner != nullptr )
                    _pOwner->markTickOrderDirty();
                return SubTickHandle{ _componentId, subTickId };
            }
        }

        SubTickInfo newInfo{};
        newInfo._subTickId = subTickId;
        newInfo._group     = group;
        newInfo._phase     = phase;
        newInfo._priority  = priority;
        newInfo._bActive   = SW_TRUE;
        _listSubTick.push_back( std::move( newInfo ) );

        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();

        return SubTickHandle{ _componentId, subTickId };
    }

    bool Component::unregisterSubTick( uint32 subTickId )
    {
        if ( subTickId < 64 )
            _subTickActiveMask.fetch_and( ~( 1ULL << subTickId ), std::memory_order_relaxed );

        for ( size_t index = 0; index < _listSubTick.size(); ++index )
        {
            if ( _listSubTick[index]._subTickId == subTickId )
            {
                _listSubTick.erase( _listSubTick.begin() + index );
                if ( _pOwner != nullptr )
                    _pOwner->markTickOrderDirty();
                return true;
            }
        }
        return false;
    }

    bool Component::addSubTickPrerequisite( uint32 subTickId, const SubTickHandle& prerequisiteHandle )
    {
        if ( subTickId == 0 || prerequisiteHandle.isValid() == false )
            return false;

        // 자기 자신을 선행 조건으로 추가하지 못하게 한다
        if ( prerequisiteHandle._componentId == _componentId && prerequisiteHandle._subTickId == subTickId )
            return false;

        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
            {
                for ( const SubTickHandle& existing : info._listPrerequisite )
                {
                    if ( existing == prerequisiteHandle )
                        return true;
                }
                info._listPrerequisite.push_back( prerequisiteHandle );
                if ( _pOwner != nullptr )
                    _pOwner->markTickOrderDirty();
                return true;
            }
        }
        return false;
    }

    void Component::setSubTickActive( uint32 subTickId, bool bActive )
    {
        if ( subTickId == 0 )
            return;

        if ( subTickId < 64 )
        {
            if ( bActive )
                _subTickActiveMask.fetch_or( 1ULL << subTickId, std::memory_order_release );
            else
                _subTickActiveMask.fetch_and( ~( 1ULL << subTickId ), std::memory_order_release );
        }

        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
            {
                info._bActive = bActive ? SW_TRUE : SW_FALSE;
                if ( _pOwner != nullptr )
                    _pOwner->markTickOrderDirty();
                break;
            }
        }
    }

    bool Component::isSubTickActiveSlow( uint32 subTickId ) const
    {
        for ( const SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
                return info._bActive == SW_TRUE;
        }
        return false;
    }

    void Component::onDestroy()
    {
    }

    void Component::onPropertyChanged( hashed_string propertyName )
    {
        (void)propertyName;
    }

    void Component::applyTypeDefaults( const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo != nullptr )
            ComponentDefaults::applyDefaults( this, *pTypeInfo );
    }

    void Component::setActive( bool bActive )
    {
        // 같은 값이면 아무것도 하지 않는다 — 메시는 알림마다 렌더 더티를 찍는다(예전 인스펙터가 프레임마다 불렀다).
        if ( _bActive.load( std::memory_order_relaxed ) == bActive )
            return;
        static const hashed_string s_activeName( "_bActive" );
        _bActive.store( bActive, std::memory_order_relaxed );
        onPropertyChanged( s_activeName );
    }

    void Component::setTickGroup( TickGroup group )
    {
        // 같은 그룹이면 틱 항목을 다시 짓게 하지 않는다(기본 그룹을 onBeginPlay 에서 다시 세팅하는 컴포넌트가 여럿이다).
        if ( _tickGroup == group )
            return;
        _tickGroup = group;
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
    }

    void Component::setCanEverTick( bool bCanEverTick )
    {
        const uint8 newValue = bCanEverTick ? SW_TRUE : SW_FALSE;
        if ( _bCanEverTick == newValue )
            return;
        _bCanEverTick = newValue;
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
    }

    sw::ComponentHandle Component::getHandle() const
    {
        if ( _pOwner == nullptr )
            return {};
        return sw::ComponentHandle::makeOwned( _pOwner->getObjectId(), _componentId );
    }

    const TypeInfo* Component::getTypeInfo() const
    {
        // 만들 때 받은 타입이다. 모듈이 내려가 타입이 묘비가 됐으면 없는 것으로 답한다. 예전의 폴백(`findType<Component>()`)은
        // Component 가 등록 타입이 아니라 늘 nullptr 이었고, 그 답을 얻으려고 캐스트가 빗나갈 때마다 레지스트리를 잠갔다.
        return ( _pTypeInfo != nullptr && _pTypeInfo->isAlive() ) ? _pTypeInfo : nullptr;
    }

    hashed_string Component::getTypeName() const
    {
        const TypeInfo* pTypeInfo = findCachedTypeInfo();
        return ( pTypeInfo != nullptr ) ? pTypeInfo->_name : _componentName;
    }

    bool Component::isActive() const
    {
        if ( _bActive.load( std::memory_order_relaxed ) == false )
            return false;
        return _pOwner == nullptr || _pOwner->isActiveInHierarchy();
    }

    atomic<uint64> Component::_s_nextComponentId = 1;

} // namespace sw
