/**
 * @file ReflectVisit.h
 * @brief 리플렉션 칸을 도는 한 곳(`ReflectPropertyVisitor`)입니다 — 방문 순서(기반부터) · 역할 거르기 · 구조체 값과 시퀀스 원소로 내려가기를 여기 두고, 소비자(상태 해시 · 검증 · 직렬화 …)는 방문자만 씁니다.
 * @details 언리얼 `TFieldIterator<FProperty>` · `FPropertyValueIterator`(구조체 · 컨테이너 재귀), 유니티 `SerializedProperty.Next( enterChildren )` ·
 *          Unity.Properties `PropertyVisitor` 자리다. 칸마다 가상 호출이 들지 않게 방문자는 템플릿 인자로 받아 인라인한다(`ReflectedValueVisitor` 와 같은 모양) —
 *          가상 `IPropertyVisitor` 는 기각했다(docs/DesignBacklog.md 리플렉션 남은 몫 W-R4).
 *          - 방문 순서는 `TypeInfo::getPropertiesWithBase` 그대로다(기반부터, 파생이 같은 이름을 제자리에서 덮는다).
 *          - 비트필드는 값 주소가 없다 — `_pValue` 가 nullptr 이고 방문자는 `_property.getValue<bool>( _pObject )` 로 읽는다(같은 바이트의 다른 비트를 덮지 않는다).
 *          - 내려가기(`_bIntoValues`)는 반사 구조체 값과, 원소가 반사 구조체인 시퀀스다. 맵 · 중첩 컨테이너 · 잎 원소는 칸으로만 알린다(그 안을 볼 소비자가 생기면 더한다).
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include <type_traits>

namespace sw
{
    /** @brief 방문자가 돌려주는 다음 걸음입니다. */
    enum class ReflectVisitFlow : uint8
    {
        Continue,     ///< 다음 칸(내려갈 수 있으면 그 안부터)
        SkipChildren, ///< 이 칸 안으로 내려가지 않는다
        Stop,         ///< 걷기를 멈춘다
    };

    /**
     * @brief 칸 역할(`PROPERTY( Replicated )` · `SaveGame` · `RuntimeState` · `Interp`)의 비트 집합입니다. 비면 모든 칸입니다.
     * @details 역할을 묻는 곳마다 메타데이터 플래그를 따로 읽지 않게 한 형식이다 — 걷는 쪽이 이 집합 하나로 거른다.
     */
    struct PropertyRoleMask
    {
        uint8 _bits{ 0 };

        static constexpr PropertyRoleMask makeAll() noexcept { return PropertyRoleMask{}; }
        static constexpr PropertyRoleMask makeReplicated() noexcept { return PropertyRoleMask{ 1u << 0 }; }
        static constexpr PropertyRoleMask makeSaveGame() noexcept { return PropertyRoleMask{ 1u << 1 }; }
        static constexpr PropertyRoleMask makeRuntimeState() noexcept { return PropertyRoleMask{ 1u << 2 }; }
        static constexpr PropertyRoleMask makeInterp() noexcept { return PropertyRoleMask{ 1u << 3 }; }

        constexpr PropertyRoleMask operator|( const PropertyRoleMask other ) const noexcept { return PropertyRoleMask{ static_cast<uint8>( _bits | other._bits ) }; }

        /** @brief 칸이 집합의 역할 하나라도 가지면(집합이 비면 늘) true 입니다. */
        bool matches( const PropertyMetadata& metadata ) const noexcept
        {
            if ( _bits == 0 )
                return true;
            const bool bReplicated   = ( _bits & makeReplicated()._bits ) != 0 && metadata._bReplicated == SW_TRUE;
            const bool bSaveGame     = ( _bits & makeSaveGame()._bits ) != 0 && metadata._bSaveGame == SW_TRUE;
            const bool bRuntimeState = ( _bits & makeRuntimeState()._bits ) != 0 && metadata._bRuntimeState == SW_TRUE;
            const bool bInterp       = ( _bits & makeInterp()._bits ) != 0 && metadata._bInterp == SW_TRUE;
            return bReplicated || bSaveGame || bRuntimeState || bInterp;
        }
    };

    /** @brief 방문한 칸 하나입니다. @p TObject 는 `void`(쓰기) 또는 `const void`(읽기)입니다. */
    template <typename TObject>
    struct ReflectVisitFieldOf
    {
        const TypeInfo&     _owner; ///< 걷는 타입(칸을 상속으로 받았어도 그 타입) — 내려간 구조체면 그 구조체 타입
        const PropertyInfo& _property;
        TObject*            _pObject; ///< 칸을 가진 객체의 주소
        TObject*            _pValue;  ///< 값 주소입니다. 비트필드는 nullptr — `_property.getValue<bool>( _pObject )` 로 읽는다
        uint32              _depth;   ///< 0 이 최상위입니다. 구조체 값 · 시퀀스 원소로 내려갈 때마다 하나 늘어난다
    };
    using ReflectVisitField      = ReflectVisitFieldOf<void>;
    using ReflectVisitConstField = ReflectVisitFieldOf<const void>;

    /** @brief 걷기 설정입니다. */
    struct ReflectVisitOptions
    {
        PropertyRoleMask       _roleMask{};      ///< 이 역할의 칸만 알린다(최상위만 거른다 — 고른 칸 안의 구조체 칸은 모두 그 칸의 몫이다)
        uint8                  _bIntoValues : 1; ///< 반사 구조체 값 · 구조체 원소 시퀀스로 내려간다
        [[maybe_unused]] uint8 _reserved    : 7;

        ReflectVisitOptions() noexcept
            : _roleMask{}
            , _bIntoValues{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class ReflectPropertyVisitor
     * @brief 파일 머리말 참고. 레지스트리와 걷기 설정을 들고, 방문자를 받아 칸을 돕니다.
     */
    class ReflectPropertyVisitor
    {
    public:
        ReflectPropertyVisitor( const TypeRegistry& registry, const ReflectVisitOptions& options ) noexcept
            : _registry{ registry }
            , _options{ options }
        {
        }

        /**
         * @brief @p pInstance(타입 @p type 객체의 주소)의 칸을 돕니다. @p visitor 는 `ReflectVisitFlow( const ReflectVisitField& )` 입니다.
         * @return 방문자가 Stop 을 돌려줬으면 false 입니다.
         */
        template <typename TVisitor>
        bool visitProperties( const TypeInfo& type, void* pInstance, TVisitor&& visitor ) const
        {
            return visitObject<void>( type, pInstance, visitor, 0 ) != ReflectVisitFlow::Stop;
        }

        /** @brief 읽기만 하는 방문입니다. @p visitor 는 `ReflectVisitFlow( const ReflectVisitConstField& )` 입니다. */
        template <typename TVisitor>
        bool visitProperties( const TypeInfo& type, const void* pInstance, TVisitor&& visitor ) const
        {
            return visitObject<const void>( type, pInstance, visitor, 0 ) != ReflectVisitFlow::Stop;
        }

        const TypeRegistry& getRegistry() const noexcept { return _registry; }

        /** @brief 값으로 든 반사 구조체의 타입입니다(내장 타입 · 열거형 · 등록되지 않은 이름은 nullptr). */
        const TypeInfo* findStructType( const hashed_string& typeName ) const
        {
            if ( typeName.empty() )
                return nullptr;
            const TypeInfo* pType = _registry.findType( typeName );
            return pType != nullptr && pType->isPrimitive() == false ? pType : nullptr;
        }

    private:
        template <typename TObject, typename TVisitor>
        ReflectVisitFlow visitObject( const TypeInfo& type, TObject* pInstance, TVisitor& visitor, const uint32 depth ) const
        {
            if ( pInstance == nullptr || depth > constant::reflection::kMaxParentChainDepth )
                return ReflectVisitFlow::Continue;
            for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
            {
                if ( depth == 0 && _options._roleMask.matches( *prop._metadata ) == false )
                    continue;
                TObject* const                     pValue = prop.getRawPtr( pInstance );
                const ReflectVisitFieldOf<TObject> field{ type, prop, pInstance, pValue, depth };
                const ReflectVisitFlow             flow = visitor( field );
                if ( flow == ReflectVisitFlow::Stop )
                    return flow;
                if ( flow == ReflectVisitFlow::SkipChildren || _options._bIntoValues == SW_FALSE || pValue == nullptr )
                    continue;
                if ( visitValue( prop, pValue, visitor, depth ) == ReflectVisitFlow::Stop )
                    return ReflectVisitFlow::Stop;
            }
            return ReflectVisitFlow::Continue;
        }

        template <typename TObject, typename TVisitor>
        ReflectVisitFlow visitValue( const PropertyInfo& prop, TObject* pValue, TVisitor& visitor, const uint32 depth ) const
        {
            if ( prop._bIsContainer == SW_FALSE )
            {
                const TypeInfo* pStruct = findStructType( prop._typeName );
                return pStruct != nullptr ? visitObject<TObject>( *pStruct, pValue, visitor, depth + 1 ) : ReflectVisitFlow::Continue;
            }
            const bool                       bNested   = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
            const ISequenceContainerWrapper* pSequence = prop._containerWrapper != nullptr && bNested == false ? prop._containerWrapper->asSequence() : nullptr;
            const TypeInfo*                  pElement  = pSequence != nullptr ? findStructType( prop._elementTypeName ) : nullptr;
            if ( pElement == nullptr )
                return ReflectVisitFlow::Continue;
            const size_t elementCount = pSequence->getSize( pValue );
            for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
            {
                TObject* pElementValue = nullptr;
                if constexpr ( std::is_const_v<TObject> )
                    pElementValue = pSequence->getElementConst( pValue, elementIndex );
                else
                    pElementValue = pSequence->getElement( pValue, elementIndex );
                if ( visitObject<TObject>( *pElement, pElementValue, visitor, depth + 1 ) == ReflectVisitFlow::Stop )
                    return ReflectVisitFlow::Stop;
            }
            return ReflectVisitFlow::Continue;
        }

    private:
        const TypeRegistry& _registry;
        ReflectVisitOptions _options;
    };
} // namespace sw
