#include "pch.h"

#include "Engine/Object/Component/Component.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    SW_LOG_CALLER( "Component" );

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
        // **여기서 기본값을 적용하지 않는다.** 생성 중에는 객체가 아직 Component 라 가상
        // `getTypeInfo()` 가 **파생 타입이 아니라 기반 타입의 TypeInfo** 를 내놓는다 — MeshComponent 를
        // 만들어도 "Component" 이름으로 기본값을 찾게 된다.
        //
        // 실제 생성 경로는 타입을 아는 쪽이 이미 올바르게 넘겨 준다.
        // `GameObject::addComponent<T>` 와 `GameObjectManager` 의 이름 기반 생성이 둘 다
        // `applyTypeDefaults( 파생 TypeInfo )` 를 부른다.
        //
        // 기반 타입 노드(`<SceneComponent>` 같은)도 기본값을 가질 수 있으므로, 상속 체인을
        // 뿌리 → 파생 순서로 적용하는 일은 `ComponentDefaults::apply` 가 맡는다.
    }

    void Component::setDefaultGameSettingsPath( string_view path )
    {
        ComponentDefaults::setDefaultsPath( path );
    }

    string Component::getDefaultGameSettingsPath()
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
        if ( isValidTickGroup( group ) == false )
        {
            SW_LOG_WARNING( "Sub-tick %# asks for tick group %#, which does not exist - not registered", subTickId, static_cast<uint32>( group ) );
            return {};
        }
        if ( priority > kMaxTickPriority )
        {
            SW_LOG_WARNING( "Sub-tick %# priority %# is above %# - clamped (a priority must not cross into the next phase)", subTickId,
                            static_cast<uint32>( priority ), static_cast<uint32>( kMaxTickPriority ) );
            priority = kMaxTickPriority;
        }
        // 틱 중이면 목록 · 마스크 모두 틱 뒤로 — 핸들은 목록과 상관없이 정해지므로 바로 돌려준다(헤더 머리말).
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [group, subTickId, phase, priority]( Component& self )
        { self.registerSubTick( group, subTickId, phase, priority ); } ) ) )
            return SubTickHandle{ _componentId, subTickId };

        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
            {
                info._group    = group;
                info._phase    = phase;
                info._priority = priority;
                info._bActive  = SW_TRUE;
                setSubTickRunnable( subTickId, true );
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
        setSubTickRunnable( subTickId, true );

        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();

        return SubTickHandle{ _componentId, subTickId };
    }

    bool Component::unregisterSubTick( uint32 subTickId )
    {
        // 실행 여부는 바로 내린다 — 이번 틱의 남은 항목이 곧바로 건너뛴다(원자라 틱 중에 바꿔도 된다). 목록에서 빼는 것은 틱 뒤로.
        setSubTickRunnable( subTickId, false );
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickId]( Component& self )
        { (void)self.unregisterSubTick( subTickId ); } ) ) )
            return true;

        for ( size_t index = 0; index < _listSubTick.size(); ++index )
        {
            if ( _listSubTick[index]._subTickId == subTickId )
            {
                _listSubTick.erase( _listSubTick.begin() + static_cast<ptrdiff_t>( index ) );
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
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickId, prerequisiteHandle]( Component& self )
        { (void)self.addSubTickPrerequisite( subTickId, prerequisiteHandle ); } ) ) )
            return true;

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

        // 실행 여부는 바로 바꾼다 — 틱 중에 끄면 이번 틱의 남은 항목이 곧바로 건너뛴다(원자라 틱 중에 바꿔도 된다). 목록의 값은 틱 뒤로 미루고,
        // 미룬 호출이 실행 여부도 다시 적어 틱 안에서 여러 번 바꾼 순서가 그대로 남는다.
        setSubTickRunnable( subTickId, bActive );
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickId, bActive]( Component& self )
        { self.setSubTickActive( subTickId, bActive ); } ) ) )
            return;

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
                return info.isRunnable();
        }
        return false;
    }

    void Component::setSubTickRunnable( uint32 subTickId, bool bRunnable )
    {
        if ( subTickId < 64 )
        {
            if ( bRunnable )
                _subTickActiveMask.fetch_or( 1ULL << subTickId, std::memory_order_release );
            else
                _subTickActiveMask.fetch_and( ~( 1ULL << subTickId ), std::memory_order_release );
            return;
        }
        // 틱 중에는 목록의 모양이 얼어 있다(구조 변경은 미룬다) — 다른 워커가 원소의 원자 칸만 쓴다.
        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickId == subTickId )
            {
                info.setRunnable( bRunnable );
                return;
            }
        }
    }

    void Component::onDestroy()
    {
    }

    void Component::onPropertyChanged( hashed_string propertyName )
    {
        (void)propertyName;
    }

    void Component::notifyStateWritten()
    {
        const TypeInfo* pTypeInfo = getTypeInfo();
        if ( pTypeInfo != nullptr )
            pTypeInfo->forEachProperty( [this]( const PropertyInfo& prop )
            { onPropertyChanged( prop._name ); }, true );
        onPostLoad();
    }

    void Component::applyTypeDefaults( const TypeInfo* pTypeInfo )
    {
        if ( pTypeInfo != nullptr )
            ComponentDefaults::applyDefaults( this, *pTypeInfo );
    }

    void Component::setActive( bool bActive )
    {
        // 같은 값이면 아무것도 하지 않는다 — 메시는 알림마다 렌더 더티를 찍는다(인스펙터는 프레임마다 부를 수 있다).
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
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [group]( Component& self )
        { self.setTickGroup( group ); } ) ) )
            return;
        // 없는 그룹을 받아 두면 등록부가 조용히 버려 이 컴포넌트가 한 번도 돌지 않는다. 지금 그룹을 지킨다.
        if ( isValidTickGroup( group ) == false )
        {
            SW_LOG_WARNING( "Tick group %# does not exist - the component keeps its current group", static_cast<uint32>( group ) );
            return;
        }
        _tickGroup = group;
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
    }

    void Component::setCanEverTick( bool bCanEverTick )
    {
        const uint8 newValue = bCanEverTick ? SW_TRUE : SW_FALSE;
        if ( _bCanEverTick == newValue )
            return;
        // 틱 중이면 틱 뒤로 — 이 비트는 다른 워커가 읽는 비트(`_bIsSceneComponent` · `_bHasBegunPlay`)와 한 바이트다.
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [bCanEverTick]( Component& self )
        { self.setCanEverTick( bCanEverTick ); } ) ) )
            return;
        _bCanEverTick = newValue;
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
    }

    bool Component::deferIfStructureFrozen( Delegate<void( Component& )> func )
    {
        GameObjectManager* pManager = ( _pOwner != nullptr ) ? _pOwner->getManager() : nullptr;
        if ( pManager == nullptr || pManager->isStructuralMutationFrozen() == false )
            return false;
        const sw::ComponentHandle handle = getHandle();
        pManager->deferStructuralChange( [pManager, handle, deferred = std::move( func )]()
        {
            Component* pSelf = pManager->resolveComponent( handle );
            if ( pSelf != nullptr )
                deferred( *pSelf );
        } );
        return true;
    }

    sw::ComponentHandle Component::getHandle() const
    {
        if ( _pOwner == nullptr )
            return {};
        return sw::ComponentHandle::makeOwned( _pOwner->getObjectId(), _componentId );
    }

    const TypeInfo* Component::getTypeInfo() const
    {
        // 만들 때 받은 타입이다. 모듈이 내려가 타입이 묘비가 됐으면 없는 것으로 답한다. 기반의 타입을 파생의 답으로 내면 안 되므로
        // `findType<Component>()` 같은 폴백은 두지 않는다.
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
