#include "pch.h"

#include "Engine/Object/Component/Component.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/Radians.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/Reflection.h"

namespace sw
{
    namespace
    {
        struct ComponentInternal
        {
            using FoldNumberFn = void ( * )( StateHashBuilder& inoutBuilder, const void* pValue );

            /** @brief num 값 하나를 접습니다. float32 는 `StateHashBuilder` 가 -0 · NaN 을 맞추고, float64 는 -0 을 +0 으로, 정수는 바이트 그대로다. */
            template <typename T>
            static void foldNumber( StateHashBuilder& inoutBuilder, const void* pValue )
            {
                const T value = *static_cast<const T*>( pValue );
                if constexpr ( std::is_same_v<T, float32> )
                {
                    inoutBuilder.addFloat( value );
                }
                else if constexpr ( std::is_floating_point_v<T> )
                {
                    const T normalized = value == T( 0 ) ? T( 0 ) : value; // -0 과 +0 은 같은 값이다(float32 와 같은 규칙)
                    inoutBuilder.addBytes( &normalized, sizeof( normalized ) );
                }
                else
                {
                    inoutBuilder.addBytes( &value, sizeof( value ) );
                }
            }

            /** @brief State 역할인 num 줄이면 그 타입의 접기, 아니면 nullptr 입니다. */
            template <typename T, bool bNumber>
            static constexpr FoldNumberFn pickFoldNumber()
            {
                if constexpr ( bNumber )
                    return &foldNumber<T>;
                else
                    return nullptr;
            }

            static constexpr bool isStateNumber( const ReflectBuiltinIndex builtinIndex )
            {
                const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                return traits.hasRole( ReflectBuiltinRole::State ) && traits._layout == ReflectBuiltinLayout::Number;
            }

            /** @brief State 역할인 줄이 Layout 또는 아래 손 처리기로 접히는지입니다. State 를 달고 접을 길이 없으면 컴파일이 멈춘다(`StateIO::reflected` 와 같은 목록). */
            static constexpr bool isFoldedBuiltin( const ReflectBuiltinIndex builtinIndex )
            {
                const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                if ( traits.hasRole( ReflectBuiltinRole::State ) == false || traits._layout != ReflectBuiltinLayout::None )
                    return true;
                return builtinIndex == ReflectBuiltinIndex::kbool || builtinIndex == ReflectBuiltinIndex::kstring || builtinIndex == ReflectBuiltinIndex::khashed_string ||
                       builtinIndex == ReflectBuiltinIndex::kGameObjectHandle;
            }

            static FoldNumberFn findFoldNumber( const ReflectBuiltinIndex builtinIndex )
            {
#define SW_REFLECT_BUILTIN_TYPE( Canon, ... ) \
    static_assert( isFoldedBuiltin( ReflectBuiltinIndex::k##Canon ), "ReflectBuiltins.xxx: State on a none layout needs a hand handler in Component::foldStateHash (" #Canon ")" );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                static constexpr FoldNumberFn kArrFoldNumber[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... ) pickFoldNumber<ReflectBuiltinCppTypeT<CppType>, isStateNumber( ReflectBuiltinIndex::k##Canon )>(),
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                };
                return builtinIndex < ReflectBuiltinIndex::kCount ? kArrFoldNumber[static_cast<size_t>( builtinIndex )] : nullptr;
            }

            /** @brief f32 · i32 성분을 접습니다. float32 는 -0 · NaN 을 맞추고, int32 는 바이트 그대로다. */
            static void foldComponents( StateHashBuilder& inoutBuilder, const ReflectBuiltinLayout layout, const uint8 count, const void* pValue )
            {
                for ( uint8 index = 0; index < count; ++index )
                {
                    if ( layout == ReflectBuiltinLayout::Float32 )
                        inoutBuilder.addFloat( static_cast<const float32*>( pValue )[index] );
                    else
                        inoutBuilder.addBytes( static_cast<const int32*>( pValue ) + index, sizeof( int32 ) );
                }
            }

            /** @brief 길이를 앞에 둔 글자입니다 — 이웃 칸과 경계가 섞이지 않는다. */
            static void foldText( StateHashBuilder& inoutBuilder, const string_view text )
            {
                inoutBuilder.addUint32( static_cast<uint32>( text.size() ) );
                inoutBuilder.addString( text );
            }

            /**
             * @brief 잎 값 하나를 접습니다. 형식이 State 역할 밖이면 false 입니다(부르는 쪽이 멈춘다).
             * @details 내장 타입은 `ReflectBuiltins.xxx` 의 State 역할 · Layout 이 정한다(`StateIO::reflected` 와 같은 칸). Layout 이 none 인 State 타입만 손으로 접는다.
             */
            static bool foldLeaf( StateHashBuilder& inoutBuilder, const TypeRegistry& registry, const hashed_string& typeName, const void* pValue )
            {
                const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( typeName );
                if ( builtinIndex != ReflectBuiltinIndex::kNone )
                {
                    const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                    if ( traits.hasRole( ReflectBuiltinRole::State ) == false )
                        return false;
                    switch ( traits._layout )
                    {
                        case ReflectBuiltinLayout::Number:
                            findFoldNumber( builtinIndex )( inoutBuilder, pValue );
                            return true;
                        case ReflectBuiltinLayout::Float32:
                        case ReflectBuiltinLayout::Int32:
                            foldComponents( inoutBuilder, traits._layout, ReflectedValueUtil::getBuiltinComponentCount( builtinIndex ), pValue );
                            return true;
                        case ReflectBuiltinLayout::Angle:
                            inoutBuilder.addFloat( static_cast<const Radians*>( pValue )->radians() );
                            return true;
                        case ReflectBuiltinLayout::None:
                            break;
                    }
                    switch ( builtinIndex ) // 손 처리기 — 목록은 `isFoldedBuiltin` 과 같다
                    {
                        case ReflectBuiltinIndex::kbool:
                            inoutBuilder.addUint8( *static_cast<const bool*>( pValue ) ? 1 : 0 );
                            return true;
                        case ReflectBuiltinIndex::kstring:
                            foldText( inoutBuilder, *static_cast<const string*>( pValue ) );
                            return true;
                        case ReflectBuiltinIndex::khashed_string:
                            foldText( inoutBuilder, static_cast<const hashed_string*>( pValue )->c_str() ); // 글자로 접는다 — 이름 해시 값은 실행마다 같다는 약속이 없다
                            return true;
                        case ReflectBuiltinIndex::kGameObjectHandle:
                        {
                            // objectID 는 매니저가 만든 순서로 매긴다 — 같은 입력 · 같은 순서의 실행에서 같은 값이라 결정적이다
                            const uint64 objectID = static_cast<const GameObjectHandle*>( pValue )->objectID();
                            inoutBuilder.addBytes( &objectID, sizeof( objectID ) );
                            return true;
                        }
                        default:
                            return false;
                    }
                }
                const EnumInfo* pEnum = registry.findEnum( typeName );
                if ( pEnum != nullptr )
                {
                    const int64 value = pEnum->readValueFromMemory( pValue );
                    inoutBuilder.addBytes( &value, sizeof( value ) );
                    return true;
                }
                return false;
            }

            [[noreturn]] static void stopForUnsupportedField( const TypeInfo& owner, const PropertyInfo& prop )
            {
                SW_FATAL( "state field '%#::%#' (%#) cannot be folded into the state hash - use a builtin with the State role (ReflectBuiltins.xxx), an enum, a reflected struct or a sequence of those, "
                          "or keep the value out of RuntimeState and fold it in a foldStateHash override",
                          owner._fullyQualifiedName.c_str(), prop._name.c_str(), prop._typeName.c_str() );
            }

            /** @brief 상태 칸 하나를 접습니다. 구조체 값 · 구조체 원소는 걷기가 안쪽 칸으로 내려가 접으므로 여기서는 원소 수만 접는다. */
            static ReflectVisitFlow foldField( StateHashBuilder& inoutBuilder, const ReflectPropertyVisitor& propertyVisitor, const ReflectVisitConstField& field )
            {
                const PropertyInfo& prop = field._property;
                if ( prop._bIsBitField == SW_TRUE )
                {
                    inoutBuilder.addUint8( prop.getValue<bool>( field._pObject ) ? 1 : 0 );
                    return ReflectVisitFlow::Continue;
                }
                if ( prop._bIsContainer == SW_TRUE )
                {
                    const bool                       bNested   = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                    const ISequenceContainerWrapper* pSequence = prop._containerWrapper != nullptr && bNested == false ? prop._containerWrapper->asSequence() : nullptr;
                    if ( pSequence == nullptr )
                        stopForUnsupportedField( field._owner, prop );
                    const size_t elementCount = pSequence->getSize( field._pValue );
                    inoutBuilder.addUint32( static_cast<uint32>( elementCount ) );
                    if ( propertyVisitor.findStructType( prop._elementTypeName ) != nullptr )
                        return ReflectVisitFlow::Continue;
                    for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                    {
                        if ( foldLeaf( inoutBuilder, propertyVisitor.getRegistry(), prop._elementTypeName, pSequence->getElementConst( field._pValue, elementIndex ) ) == false )
                            stopForUnsupportedField( field._owner, prop );
                    }
                    return ReflectVisitFlow::Continue;
                }
                if ( propertyVisitor.findStructType( prop._typeName ) != nullptr )
                    return ReflectVisitFlow::Continue;
                if ( foldLeaf( inoutBuilder, propertyVisitor.getRegistry(), prop._typeName, field._pValue ) == false )
                    stopForUnsupportedField( field._owner, prop );
                return ReflectVisitFlow::Continue;
            }
        };
    } // namespace

    void Component::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        const TypeInfo* pType = findCachedTypeInfo();
        if ( pType == nullptr )
            return;
        ReflectVisitOptions options;
        options._roleMask    = PropertyRoleMask::makeRuntimeState();
        options._bIntoValues = SW_TRUE; // 구조체 칸 · 구조체 원소는 안쪽 칸으로 접는다
        const ReflectPropertyVisitor propertyVisitor{ engine::getSubsystem<TypeRegistry>(), options };
        (void)propertyVisitor.visitProperties( *pType, static_cast<const void*>( this ), [&inoutBuilder, &propertyVisitor]( const ReflectVisitConstField& field )
        { return ComponentInternal::foldField( inoutBuilder, propertyVisitor, field ); } );
    }

    Component::Component()
        : _pOwner{ nullptr }
        , _componentID{ 0 } // 붙을 때 매니저의 번호 공간이 준다(`GameObject::attachCreatedComponent`)
        , _componentName{}
        , _localID{ 0 } // 0 은 "아직 정하지 않음" — 키는 순번에서 정한 값을 쓰고, 구조를 바꾸기 전에 굳힌다(`ComponentStableKey`)
        , _pTypeInfo{ nullptr }
        , _pPoolEntry{ nullptr }
        , _subTickActiveMask{ 0 }
        , _bActive{ SW_TRUE }
        , _bIsPendingDestroy{ false }
        , _tickGroup{ TickGroup::DuringPhysics }
        , _bCanEverTick{ SW_TRUE }
        , _bIsSceneComponent{ SW_FALSE }
        , _bHasBegunPlay{ SW_FALSE }
        , _bRegistered{ SW_FALSE }
        , _bPostLoadPending{ SW_FALSE }
        , _bStaged{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _poolEntrySlot{ kNotInPoolEntry }
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
        // 루트 → 파생 순서로 적용하는 일은 `ComponentDefaults::apply` 가 맡는다.
    }

    void Component::onBeginPlay()
    {
    }

    void Component::onEndPlay()
    {
    }

    void Component::dispatchRegister( GameObjectManager& manager )
    {
        if ( _bRegistered == SW_TRUE )
            return;
        _bRegistered = SW_TRUE;
        onRegister( manager );
    }

    void Component::dispatchPostLoad()
    {
        _bPostLoadPending = SW_FALSE;
        onPostLoad();
    }

    void Component::dispatchUnregister( GameObjectManager& manager )
    {
        if ( _bRegistered == SW_FALSE )
            return;
        _bRegistered = SW_FALSE;
        onUnregister( manager );
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

    void Component::onTick( TickContext& /*context*/ )
    {
    }

    void Component::onSubTick( uint32 subTickID, TickContext& context )
    {
        (void)subTickID;
        (void)context;
    }

    SubTickHandle Component::registerSubTick( TickGroup group, uint32 subTickID, TickPhase phase, uint8 priority )
    {
        if ( subTickID == 0 )
            return {};
        if ( isValidTickGroup( group ) == false )
        {
            SW_LOG_WARNING_ID( "Component.SubTickAsksTick", "Sub-tick %# asks for tick group %#, which does not exist - not registered", subTickID, static_cast<uint32>( group ) );
            return {};
        }
        if ( priority > kMaxTickPriority )
        {
            SW_LOG_WARNING_ID( "Component.SubTickPriorityAbove", "Sub-tick %# priority %# is above %# - clamped (a priority must not cross into the next phase)", subTickID,
                               static_cast<uint32>( priority ), static_cast<uint32>( kMaxTickPriority ) );
            priority = kMaxTickPriority;
        }
        // 틱 중이면 목록 · 마스크 모두 틱 뒤로 — 핸들은 목록과 상관없이 정해지므로 바로 돌려준다(헤더 머리말).
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [group, subTickID, phase, priority]( Component& self )
        { self.registerSubTick( group, subTickID, phase, priority ); } ) ) )
            return makeTickHandle( subTickID );

        if ( SubTickInfo* pInfo = VectorUtil::findBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID ) )
        {
            pInfo->_group    = group;
            pInfo->_phase    = phase;
            pInfo->_priority = priority;
            pInfo->_bActive  = SW_TRUE;
            setSubTickRunnable( subTickID, true );
            if ( _pOwner != nullptr )
                _pOwner->markTickOrderDirty();
            return makeTickHandle( subTickID );
        }

        SubTickInfo newInfo{};
        newInfo._subTickID = subTickID;
        newInfo._group     = group;
        newInfo._phase     = phase;
        newInfo._priority  = priority;
        newInfo._bActive   = SW_TRUE;
        _listSubTick.push_back( std::move( newInfo ) );
        setSubTickRunnable( subTickID, true );

        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();

        return makeTickHandle( subTickID );
    }

    SubTickHandle Component::makeTickHandle( uint32 subTickID ) const
    {
        return SubTickHandle{ _componentID, subTickID, ( _pOwner != nullptr ) ? _pOwner->getObjectID() : 0 };
    }

    bool Component::unregisterSubTick( uint32 subTickID )
    {
        // 실행 여부는 바로 내린다 — 이번 틱의 남은 항목이 곧바로 건너뛴다(원자라 틱 중에 바꿔도 된다). 목록에서 빼는 것은 틱 뒤로.
        setSubTickRunnable( subTickID, false );
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickID]( Component& self )
        { (void)self.unregisterSubTick( subTickID ); } ) ) )
            return true;

        const optional<size_t> index = VectorUtil::findIndexBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID );
        if ( index.has_value() == false )
            return false;
        _listSubTick.erase( _listSubTick.begin() + static_cast<ptrdiff_t>( *index ) );
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
        return true;
    }

    bool Component::addSubTickPrerequisite( uint32 subTickID, const SubTickHandle& prerequisiteHandle, TickPrerequisiteKind kind )
    {
        if ( subTickID == 0 || prerequisiteHandle.isValid() == false )
            return false;
        if ( prerequisiteHandle._objectID == 0 )
        {
            SW_LOG_WARNING_ID( "Component.SubTickPrerequisiteNames", "Sub-tick %# prerequisite names component %# without its object - take the handle after the component is added to an object",
                               subTickID, prerequisiteHandle._componentID );
            return false;
        }

        // 자기 자신을 선행 조건으로 추가하지 못하게 한다
        if ( prerequisiteHandle._componentID == _componentID && prerequisiteHandle._subTickID == subTickID )
            return false;
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickID, prerequisiteHandle, kind]( Component& self )
        { (void)self.addSubTickPrerequisite( subTickID, prerequisiteHandle, kind ); } ) ) )
            return true;

        SubTickInfo* pInfo = VectorUtil::findBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID );
        if ( pInfo == nullptr )
            return false;
        for ( SubTickPrerequisite& prerequisite : pInfo->_listPrerequisite )
        {
            if ( prerequisite._handle != prerequisiteHandle )
                continue;
            if ( prerequisite._kind == kind )
                return true;
            prerequisite._kind = kind; // 같은 간선의 종류만 바뀌었다 — 스테이지가 레벨 · 태스크 간선을 다시 가른다
            if ( _pOwner != nullptr )
                _pOwner->markTickOrderDirty();
            return true;
        }
        pInfo->_listPrerequisite.push_back( SubTickPrerequisite{ prerequisiteHandle, kind } );
        if ( _pOwner != nullptr )
            _pOwner->markTickOrderDirty();
        return true;
    }

    bool Component::removeSubTickPrerequisite( uint32 subTickID, const SubTickHandle& prerequisiteHandle )
    {
        if ( subTickID == 0 || prerequisiteHandle.isValid() == false )
            return false;
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickID, prerequisiteHandle]( Component& self )
        { (void)self.removeSubTickPrerequisite( subTickID, prerequisiteHandle ); } ) ) ) // 미룬 제거 — 그때 이미 빠졌으면 할 일이 없다
            return true;

        for ( SubTickInfo& info : _listSubTick )
        {
            if ( info._subTickID != subTickID )
                continue;
            for ( size_t index = 0; index < info._listPrerequisite.size(); ++index )
            {
                if ( info._listPrerequisite[index]._handle != prerequisiteHandle )
                    continue;
                info._listPrerequisite.erase( info._listPrerequisite.begin() + static_cast<ptrdiff_t>( index ) );
                if ( _pOwner != nullptr )
                    _pOwner->markTickOrderDirty();
                return true;
            }
            return false;
        }
        return false;
    }

    void Component::setSubTickActive( uint32 subTickID, bool bActive )
    {
        if ( subTickID == 0 )
            return;

        // 실행 여부는 바로 바꾼다 — 틱 중에 끄면 이번 틱의 남은 항목이 곧바로 건너뛴다(원자라 틱 중에 바꿔도 된다). 목록의 값은 틱 뒤로 미루고,
        // 미룬 호출이 실행 여부도 다시 적어 틱 안에서 여러 번 바꾼 순서가 그대로 남는다.
        setSubTickRunnable( subTickID, bActive );
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [subTickID, bActive]( Component& self )
        { self.setSubTickActive( subTickID, bActive ); } ) ) )
            return;

        if ( SubTickInfo* pInfo = VectorUtil::findBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID ) )
        {
            pInfo->_bActive = bActive ? SW_TRUE : SW_FALSE;
            if ( _pOwner != nullptr )
                _pOwner->markTickOrderDirty();
        }
    }

    bool Component::isSubTickActiveSlow( uint32 subTickID ) const
    {
        const SubTickInfo* pInfo = VectorUtil::findBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID );
        return pInfo != nullptr && pInfo->isRunnable();
    }

    void Component::setSubTickRunnable( uint32 subTickID, bool bRunnable )
    {
        if ( subTickID < 64 )
        {
            if ( bRunnable )
                _subTickActiveMask.fetch_or( 1ULL << subTickID, std::memory_order_release );
            else
                _subTickActiveMask.fetch_and( ~( 1ULL << subTickID ), std::memory_order_release );
            return;
        }
        // 틱 중에는 목록의 모양이 얼어 있다(구조 변경은 미룬다) — 다른 워커가 원소의 원자 칸만 쓴다.
        if ( SubTickInfo* pInfo = VectorUtil::findBy<&SubTickInfo::_subTickID>( _listSubTick, subTickID ) )
            pInfo->setRunnable( bRunnable );
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
        // 기본값은 엔진 범위의 `ComponentDefaults` 가 든다. 서비스가 묶이지 않은 곳(서비스 없는 도구 · 테스트)은 기본값 없이 만든다.
        if ( pTypeInfo != nullptr && engine::areEngineServicesBound() )
            engine::getSubsystem<ComponentDefaults>().apply( this, *pTypeInfo );
    }

    void Component::setActive( bool bActive )
    {
        // 같은 값이면 아무것도 하지 않는다 — 메시는 알림마다 렌더 더티를 찍는다(인스펙터는 프레임마다 부를 수 있다).
        if ( _bActive.load( std::memory_order_relaxed ) == bActive )
            return;
        _bActive.store( bActive, std::memory_order_relaxed );
        onPropertyChanged( getActivePropertyName() );
    }

    const hashed_string& Component::getActivePropertyName()
    {
        static const hashed_string s_activeName( "_bActive" );
        return s_activeName;
    }

    void Component::setTickGroup( TickGroup group )
    {
        // 같은 그룹이면 틱 항목을 다시 짓게 하지 않는다(기본 그룹을 onBeginPlay 에서 다시 세팅하는 컴포넌트가 여럿이다).
        if ( _tickGroup == group )
            return;
        if ( deferIfStructureFrozen( Delegate<void( Component& )>( [group]( Component& self )
        { self.setTickGroup( group ); } ) ) )
            return;
        // 없는 그룹을 받아 두면 레지스트리가 조용히 버려 이 컴포넌트가 한 번도 돌지 않는다. 지금 그룹을 지킨다.
        if ( isValidTickGroup( group ) == false )
        {
            SW_LOG_WARNING_ID( "Component.TickGroupDoesNot", "Tick group %# does not exist - the component keeps its current group", static_cast<uint32>( group ) );
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
        GameObjectManager* pManager = ( _pOwner != nullptr ) ? _pOwner->_pOwnerManager : nullptr; // 틱 중에 부른다(구조 변경 큐) — getMutableManager 의 틱 단언을 지나지 않는다
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
        return sw::ComponentHandle::makeOwned( _pOwner->getObjectID(), _componentID );
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

} // namespace sw
