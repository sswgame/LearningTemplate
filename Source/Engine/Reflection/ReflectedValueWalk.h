/**
 * @file ReflectedValueWalk.h
 * @brief 리플렉션 값 하나를 구조체 필드와 컨테이너 단계까지 재귀로 내려가며, 지정한 타입의 잎을 제자리에서 바꾸는 방문입니다.
 * @details 쓰기 쪽(타입 처리기)은 값이 어느 깊이에 있든 잎마다 불리므로, 읽은 뒤 잎을 옮기는 쪽도 같은 깊이를 봐야 쓰기와 읽기가 대칭이다
 *          (예: `GameObjectHandle` 을 저장 id 로 적고, 읽은 뒤 이 실행의 오브젝트로 옮기기 — `ObjectStateBatch::resolveObjectReferences`).
 *          - 각 깊이에서 필드 거르기(@p TFieldFilter, 보통 `SerializerUtil::shouldSerializeProperty`)가 false 인 필드는 내려가지 않는다. 읽지 않은 필드는 지금 값이다.
 *          - 순서 컨테이너는 제자리에서 고치고, set 의 잎 원소와 map 의 잎 키는 빼고 다시 넣는다.
 *          - 제자리에서 고칠 수 없는 잎(구조체 · 컨테이너를 원소로 가진 set 안의 잎, 구조체 키 안의 잎)이 있으면 `walk*` 가 false 다.
 *            부르는 쪽이 로드 오류로 처리한다 — 그 잎을 그대로 두면 다른 실행의 id 가 남는다.
 *          - 잎을 품을 수 없는 타입은 타입 그래프로 한 번 판정해 기억하고(`_mapTypeHoldsLeaf`) 내려가지 않는다. 그래서 큰 씬 로드에서도 잎이 없는 컴포넌트는 비용이 없다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/TextAddress.h"

#include <type_traits>

namespace sw
{
    /**
     * @class ReflectedValueWalk
     * @brief 파일 머리말 참고. @p TLeaf 는 바이트로 복사할 수 있는 잎 타입이고, @p TFieldFilter 는 `bool( const TypeInfo& owner, const PropertyInfo& prop )`,
     *        @p TRemap 은 `TLeaf( const TLeaf&, const PropertyInfo& leafProperty )` 입니다. 뒤의 인자는 그 잎이 든 필드입니다(컨테이너 원소 · 키도 그 컨테이너 필드).
     */
    template <typename TLeaf, typename TFieldFilter, typename TRemap>
    class ReflectedValueWalk
    {
        static_assert( std::is_trivially_copyable_v<TLeaf>, "잎은 set 에서 빼고 다시 넣을 때 바이트로 복사한다" );

    public:
        ReflectedValueWalk( const TypeRegistry& registry, const hashed_string& leafTypeName, TFieldFilter filter, TRemap remap )
            : _registry{ registry }
            , _leafTypeName{ leafTypeName }
            , _filter{ std::move( filter ) }
            , _remap{ std::move( remap ) }
            , _mapTypeHoldsLeaf{}
        {
        }

        /** @brief @p instance (타입 @p type 객체의 주소)의 거른 필드를 모든 깊이까지 내려가 잎을 바꿉니다. 고칠 수 없는 잎이 있으면 false 입니다(나머지는 바꾼다). */
        [[nodiscard]] bool walkObject( const MutableObjectAddress instance, const TypeInfo& type )
        {
            if ( instance.get() == nullptr || typeHoldsLeaf( type ) == false )
                return true;
            bool bAllWalked = true;
            for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
            {
                bAllWalked = walkProperty( instance, type, prop ) && bAllWalked;
            }
            return bAllWalked;
        }

        /**
         * @brief @p instance (타입 @p owner 객체의 주소)의 필드 하나를 내려가 잎을 바꿉니다. 거른 필드 · 비트 필드는 건드리지 않고 true 입니다.
         * @details 부르는 쪽이 고칠 수 없는 필드의 이름을 알리려고 최상위 필드마다 부를 때 씁니다.
         */
        [[nodiscard]] bool walkProperty( const MutableObjectAddress instance, const TypeInfo& owner, const PropertyInfo& prop )
        {
            if ( prop._bIsBitField == SW_TRUE || _filter( owner, prop ) == false )
                return true;
            const MutableValueAddress value{ prop.getRawPtr( instance.get() ) };
            if ( value.get() == nullptr )
                return true;
            // 이 필드 아래의 잎은 모두 이 필드의 것이다 — 구조체 안으로 내려가면 안쪽 필드가 자기 것으로 바꿨다가 돌아올 때 되돌린다.
            const PropertyInfo* pOuterProperty = _pLeafProperty;
            _pLeafProperty                     = &prop;
            bool bWalked                       = true;
            if ( prop._bIsContainer == SW_TRUE )
            {
                if ( prop._containerWrapper != nullptr )
                {
                    const NestedContainerInfo shape = prop.getContainerShape();
                    bWalked                         = walkContainer( value, shape );
                }
            }
            else
            {
                bWalked = walkValue( value, prop._typeName );
            }
            _pLeafProperty = pOuterProperty;
            return bWalked;
        }

        /** @brief 타입 @p type 이 어느 깊이에든 잎을 품을 수 있으면 true 입니다(타입 그래프로 판정하고 기억한다). false 면 내려갈 필요가 없습니다. */
        bool holdsLeaf( const TypeInfo& type ) { return typeHoldsLeaf( type ); }

        /** @brief 컨테이너 @p container (컨테이너 값 자체의 주소, 모양 @p shape)의 원소 · 키 · 값을 내려가 잎을 바꿉니다. */
        [[nodiscard]] bool walkContainer( const MutableValueAddress container, const NestedContainerInfo& shape )
        {
            void* const pContainer     = container.get();
            const bool  bNothingToWalk = pContainer == nullptr || shape._wrapper == nullptr || shapeHoldsLeaf( shape ) == false;
            if ( bNothingToWalk )
                return true;
            ISequenceContainerWrapper* pSequence = shape._wrapper->asSequence();
            if ( pSequence != nullptr )
                return walkSequence( pContainer, *pSequence, shape );
            IMapContainerWrapper* pMap = shape._wrapper->asMap();
            if ( pMap != nullptr )
                return walkMap( pContainer, *pMap, shape );
            return true;
        }

    private:
        /** @brief 컨테이너가 아닌 값 하나(잎이거나 잎을 품은 구조체)를 바꿉니다. */
        bool walkValue( const MutableValueAddress value, const hashed_string& typeName )
        {
            if ( typeName == _leafTypeName )
            {
                TLeaf* pLeaf = static_cast<TLeaf*>( value.get() );
                *pLeaf       = _remap( *pLeaf, *_pLeafProperty );
                return true;
            }
            // 구조체 값의 주소는 그 구조체 타입의 객체 주소다.
            const TypeInfo* pStruct = findStruct( typeName );
            return pStruct == nullptr || walkObject( MutableObjectAddress{ value.get() }, *pStruct );
        }

        bool walkSequence( void* pContainer, ISequenceContainerWrapper& sequence, const NestedContainerInfo& shape )
        {
            const size_t elementCount = sequence.getSize( pContainer );
            const bool   bLeafElement = shape._elementNested == nullptr && shape._elementTypeName == _leafTypeName;
            if ( sequence.allowsInPlaceElementWrite() == false )
            {
                // 원소가 곧 정렬 · 해시 키인 set 이다. 잎 원소는 옮긴 값으로 빼고 다시 넣고, 잎을 품은 원소(구조체 · 컨테이너)는 고칠 수 없다.
                if ( bLeafElement == false )
                    return false;
                vector<TLeaf> listLeaf;
                listLeaf.reserve( elementCount );
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    listLeaf.push_back( _remap( *static_cast<const TLeaf*>( sequence.getElementConst( pContainer, elementIndex ) ), *_pLeafProperty ) );
                }
                sequence.clear( pContainer );
                bool bAllAppended = true;
                for ( size_t elementIndex = 0; elementIndex < listLeaf.size(); ++elementIndex )
                {
                    const TLeaf leaf = listLeaf[elementIndex];
                    bAllAppended     = sequence.appendElement( pContainer, elementIndex, ElementFillDelegate::create( [leaf]( void* pElement )
                    {
                        Memory::copy( pElement, &leaf, sizeof( TLeaf ) );
                        return true;
                    } ) ) && bAllAppended;
                }
                return bAllAppended;
            }
            bool bAllWalked = true;
            for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
            {
                const MutableValueAddress element{ sequence.getElement( pContainer, elementIndex ) };
                if ( shape._elementNested != nullptr )
                    bAllWalked = walkContainer( element, *shape._elementNested ) && bAllWalked;
                else
                    bAllWalked = walkValue( element, shape._elementTypeName ) && bAllWalked;
            }
            return bAllWalked;
        }

        bool walkMap( void* pContainer, IMapContainerWrapper& map, const NestedContainerInfo& shape )
        {
            // 값부터 제자리에서 바꾼다(잎, 잎을 품은 구조체, 중첩 컨테이너).
            bool bAllWalked = true;
            map.forEachMutable( pContainer, MapForEachMutableDelegate::create( [this, &shape, &bAllWalked]( const void*, void* pValue )
            {
                const MutableValueAddress value{ pValue };
                if ( shape._elementNested != nullptr )
                    bAllWalked = walkContainer( value, *shape._elementNested ) && bAllWalked;
                else
                    bAllWalked = walkValue( value, shape._elementTypeName ) && bAllWalked;
            } ) );
            if ( shape._keyTypeName == _leafTypeName )
                return reinsertLeafKeys( pContainer, map ) && bAllWalked;
            // 구조체 키 안의 잎은 정렬 · 해시의 기준이라 고칠 수 없다.
            const TypeInfo* pKeyStruct           = findStruct( shape._keyTypeName );
            const bool      bLeafInsideStructKey = pKeyStruct != nullptr && typeHoldsLeaf( *pKeyStruct ) && map.getSize( pContainer ) > 0;
            if ( bLeafInsideStructKey )
                return false;
            return bAllWalked;
        }

        /** @brief 잎 키는 제자리에서 고칠 수 없다 — 값을 밖에 복사해 두고, 비운 뒤 옮긴 키로 다시 넣습니다. */
        bool reinsertLeafKeys( void* pContainer, IMapContainerWrapper& map )
        {
            constexpr size_t kAlignment  = 64;
            const size_t     valueStride = ( map.getValueSize() + kAlignment - 1 ) / kAlignment * kAlignment;
            const size_t     entryCount  = map.getSize( pContainer );
            if ( entryCount == 0 )
                return true;
            const MemoryBlock valueBlock = MemoryBlock::allocateAligned( valueStride * entryCount, kAlignment );
            if ( valueBlock.isValid() == false )
                return false;
            uint8* const  pValueBlock = valueBlock.getAs<uint8>();
            vector<TLeaf> listKey;
            listKey.reserve( entryCount );
            bool bCopied = true;
            map.forEach( pContainer, MapForEachDelegate::create( [this, &map, pValueBlock, valueStride, &listKey, &bCopied]( const void* pKey, const void* pValue )
            {
                void* pSlot = pValueBlock + valueStride * listKey.size();
                map.defaultConstructValue( pSlot );
                bCopied = map.copyValue( pSlot, pValue ) && bCopied;
                listKey.push_back( _remap( *static_cast<const TLeaf*>( pKey ), *_pLeafProperty ) );
            } ) );
            if ( bCopied )
            {
                map.clear( pContainer );
                for ( size_t entryIndex = 0; entryIndex < listKey.size(); ++entryIndex )
                {
                    map.insertKeyValue( pContainer, &listKey[entryIndex], pValueBlock + valueStride * entryIndex );
                }
            }
            for ( size_t entryIndex = 0; entryIndex < listKey.size(); ++entryIndex )
            {
                map.destroyValue( pValueBlock + valueStride * entryIndex );
            }
            return bCopied;
        }

        /** @brief 이름이 리플렉션 구조체(내장 값 타입이 아님)이면 그 타입입니다. */
        const TypeInfo* findStruct( const hashed_string& typeName ) const
        {
            if ( typeName.empty() || typeName == _leafTypeName )
                return nullptr;
            const TypeInfo* pType = _registry.findType( typeName );
            return ( pType != nullptr && pType->isPrimitive() == false ) ? pType : nullptr;
        }

        bool shapeHoldsLeaf( const NestedContainerInfo& shape )
        {
            if ( shape._elementTypeName == _leafTypeName || shape._keyTypeName == _leafTypeName )
                return true;
            if ( shape._elementNested != nullptr && shapeHoldsLeaf( *shape._elementNested ) )
                return true;
            const TypeInfo* pElement = findStruct( shape._elementTypeName );
            const TypeInfo* pKey     = findStruct( shape._keyTypeName );
            return ( pElement != nullptr && typeHoldsLeaf( *pElement ) ) || ( pKey != nullptr && typeHoldsLeaf( *pKey ) );
        }

        /** @brief 타입이 어느 깊이에든 잎을 품을 수 있는지(필드 거르기와 관계없이 타입 그래프로) 판정합니다. 순환은 "아니요" 로 끊습니다. */
        bool typeHoldsLeaf( const TypeInfo& type )
        {
            const auto memoIt = _mapTypeHoldsLeaf.find( &type );
            if ( memoIt != _mapTypeHoldsLeaf.end() )
                return memoIt->second;
            _mapTypeHoldsLeaf[&type] = false; // 계산 중 — 자기 자신을 품는 순환이 여기서 끊긴다
            bool bHolds              = false;
            for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
            {
                if ( prop._bIsContainer == SW_TRUE )
                {
                    bHolds = prop._containerWrapper != nullptr && shapeHoldsLeaf( prop.getContainerShape() );
                }
                else
                {
                    const TypeInfo* pStruct = findStruct( prop._typeName );
                    bHolds                  = prop._typeName == _leafTypeName || ( pStruct != nullptr && typeHoldsLeaf( *pStruct ) );
                }
                if ( bHolds )
                    break;
            }
            _mapTypeHoldsLeaf[&type] = bHolds;
            return bHolds;
        }

        const TypeRegistry&                  _registry;
        hashed_string                        _leafTypeName;
        TFieldFilter                         _filter;
        TRemap                               _remap;
        const PropertyInfo*                  _pLeafProperty{ nullptr }; ///< 지금 내려가는 필드다. `_remap` 이 잎과 함께 받는다(타입 핸들의 대상 타입이 필드에 있다)
        unordered_map<const TypeInfo*, bool> _mapTypeHoldsLeaf;
    };
} // namespace sw
