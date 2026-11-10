#include "pch.h"

#include "Engine/Reflection/ReflectionValidation.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct ReflectionValidationInternal
        {
            /** @brief 값으로 든 반사 구조체의 타입입니다(내장 타입 · 열거형은 아니다). 없으면 nullptr 입니다. */
            static const TypeInfo* findNestedType( const hashed_string& typeName )
            {
                if ( typeName.empty() )
                    return nullptr;
                const TypeInfo* pType = engine::getSubsystem<TypeRegistry>().findType( typeName );
                if ( pType == nullptr || pType->isPrimitive() )
                    return nullptr;
                return pType;
            }

            /** @brief 시퀀스 원소가 반사 구조체면 그 타입입니다(맵 · 중첩 컨테이너는 내려가지 않는다). */
            static const TypeInfo* findSequenceElementType( const PropertyInfo& prop )
            {
                const bool bNestedContainer   = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                const bool bNoSequenceElement = prop._bIsContainer == SW_FALSE || prop._containerWrapper == nullptr || prop._containerWrapper->asSequence() == nullptr || bNestedContainer;
                if ( bNoSequenceElement )
                    return nullptr;
                return findNestedType( prop._elementTypeName );
            }

            /**
             * @brief 검증 함수가 있는지 — 자기 사슬의 타입 · 프로퍼티 검증, 값으로 든 구조체 · 시퀀스 원소 타입의 것까지.
             * @param depth 값 타입이 서로를 담는 사슬(드물다)에서 멈추는 깊이
             */
            static bool computeHasValidator( const TypeInfo& type, const uint32 depth )
            {
                if ( depth > constant::reflection::kMaxParentChainDepth )
                    return false;
                const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
                const uint32    chainLength = type.collectTypeChain( arrChain );
                for ( uint32 level = 0; level < chainLength; ++level )
                {
                    if ( arrChain[level]->_pValidate != nullptr )
                        return true;
                }
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop._pValidate != nullptr )
                        return true;
                    const TypeInfo* pNested          = prop._bIsContainer == SW_TRUE ? findSequenceElementType( prop ) : findNestedType( prop._typeName );
                    const bool      bNestedValidator = pNested != nullptr && pNested != &type && computeHasValidator( *pNested, depth + 1 );
                    if ( bNestedValidator )
                        return true;
                }
                return false;
            }

            /**
             * @brief 검증 한 번(`validateObject`) 동안 프로퍼티별 중첩 타입을 기억합니다. 시퀀스 원소마다 같은 프로퍼티의 타입을 레지스트리에서 다시 찾지 않게 한다.
             * @details 호출 하나 안에서만 산다 — 등록 · 해제 무효화 계약이 필요 없다(`TypeInfo` 주소는 레지스트리 수명 동안 고정). 칸이 차면 레지스트리에서 찾는다.
             *          측정: `ReflectionValidationTest.NestedElementCost`.
             */
            struct NestedTypeCache
            {
                static constexpr uint32 kCapacity = 32;

                const PropertyInfo* _arrProperty[kCapacity]{};
                const TypeInfo*     _arrType[kCapacity]{};
                uint32              _count{ 0 };

                const TypeInfo* find( const PropertyInfo& prop )
                {
                    for ( uint32 index = 0; index < _count; ++index )
                    {
                        if ( _arrProperty[index] == &prop )
                            return _arrType[index];
                    }
                    const TypeInfo* pType = prop._bIsContainer == SW_TRUE ? findSequenceElementType( prop ) : findNestedType( prop._typeName );
                    if ( _count < kCapacity )
                    {
                        _arrProperty[_count] = &prop;
                        _arrType[_count]     = pType;
                        ++_count;
                    }
                    return pType;
                }
            };

            static uint32 validate( const TypeInfo& type, const void* pInstance, ValidationContext& context, const uint32 depth, NestedTypeCache& cache )
            {
                const bool bNothingToValidate = pInstance == nullptr || depth > constant::reflection::kMaxParentChainDepth || ReflectionValidation::hasValidator( type ) == false;
                if ( bNothingToValidate )
                    return 0;
                const size_t issueCountBefore = context.getIssues().size();

                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( prop._pValidate != nullptr )
                    {
                        context.setScope( type._fullyQualifiedName, prop._name );
                        prop._pValidate( pInstance, context );
                    }
                    if ( prop._bIsBitField == SW_TRUE )
                        continue;
                    const void*     pValue  = prop.getRawPtr( pInstance );
                    const TypeInfo* pNested = cache.find( prop );
                    if ( pNested == nullptr )
                        continue;
                    if ( prop._bIsContainer == SW_FALSE )
                    {
                        validate( *pNested, pValue, context, depth + 1, cache );
                        continue;
                    }
                    const ISequenceContainerWrapper* pSequence    = prop._containerWrapper->asSequence();
                    const size_t                     elementCount = pSequence->getSize( pValue );
                    for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                    {
                        validate( *pNested, pSequence->getElementConst( pValue, elementIndex ), context, depth + 1, cache );
                    }
                }

                // 타입 검증은 기반부터 — 파생의 검증이 기반이 이미 본 것을 다시 적지 않게 순서를 정해 둔다.
                const TypeInfo* arrChain[constant::reflection::kMaxParentChainDepth];
                uint32          level = type.collectTypeChain( arrChain );
                while ( level > 0 )
                {
                    --level;
                    const TypeInfo& levelType = *arrChain[level];
                    if ( levelType._pValidate == nullptr )
                        continue;
                    context.setScope( levelType._fullyQualifiedName, hashed_string{} );
                    levelType._pValidate( pInstance, context );
                }
                context.setScope( hashed_string{}, hashed_string{} );
                return static_cast<uint32>( context.getIssues().size() - issueCountBefore );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ValidationContext::ValidationContext()
        : _listIssue{}
        , _sourceLabel{}
        , _typeName{}
        , _propertyName{}
        , _sourceID{ 0 }
    {
    }

    void ValidationContext::addError( const string_view message )
    {
        add( ValidationSeverity::Error, message, ValidationPropertyEdit{} );
    }

    void ValidationContext::addWarning( const string_view message )
    {
        add( ValidationSeverity::Warning, message, ValidationPropertyEdit{} );
    }

    void ValidationContext::addErrorWithFix( const string_view message, const ValidationPropertyEdit& fix )
    {
        add( ValidationSeverity::Error, message, fix );
    }

    void ValidationContext::addWarningWithFix( const string_view message, const ValidationPropertyEdit& fix )
    {
        add( ValidationSeverity::Warning, message, fix );
    }

    bool ValidationContext::hasError() const noexcept
    {
        return VectorUtil::containsBy<&ValidationIssue::_severity>( _listIssue, ValidationSeverity::Error );
    }

    void ValidationContext::setSource( const uint64 sourceID, const string_view sourceLabel )
    {
        _sourceID    = sourceID;
        _sourceLabel = string( sourceLabel );
    }

    void ValidationContext::setScope( const hashed_string& typeName, const hashed_string& propertyName )
    {
        _typeName     = typeName;
        _propertyName = propertyName;
    }

    void ValidationContext::add( const ValidationSeverity severity, const string_view message, const ValidationPropertyEdit& fix )
    {
        ValidationIssue issue;
        issue._fix = fix;
        if ( fix._valueText.empty() == false || fix._description.empty() == false )
        {
            // 고칠 프로퍼티를 적지 않았으면 지금 검증하는 프로퍼티다(프로퍼티 검증 함수의 흔한 경우)
            if ( issue._fix._propertyName.empty() )
                issue._fix._propertyName = _propertyName;
            SW_ASSERT( issue._fix._propertyName.empty() == false ); // 타입 검증 함수의 고침은 프로퍼티를 적어야 한다
        }
        issue._message      = string( message );
        issue._sourceLabel  = _sourceLabel;
        issue._typeName     = _typeName;
        issue._propertyName = _propertyName;
        issue._sourceID     = _sourceID;
        issue._severity     = severity;
        _listIssue.push_back( std::move( issue ) );
    }

    uint32 ReflectionValidation::validateObject( const TypeInfo& type, const void* pInstance, ValidationContext& context )
    {
        ReflectionValidationInternal::NestedTypeCache cache;
        return ReflectionValidationInternal::validate( type, pInstance, context, 0, cache );
    }

    bool ReflectionValidation::hasValidator( const TypeInfo& type )
    {
        if ( type._bValidatorCalculated == SW_TRUE )
            return type._bHasValidator == SW_TRUE;
        const bool bHasValidator   = ReflectionValidationInternal::computeHasValidator( type, 0 );
        type._bHasValidator        = bHasValidator ? SW_TRUE : SW_FALSE;
        type._bValidatorCalculated = SW_TRUE;
        return bHasValidator;
    }

    void ValidationIssueLog::replaceIssues( const uint64 sourceID, const vector<ValidationIssue>& listIssue )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listIssue.empty() && listIssue.empty() )
            return;
        // 제자리에서 지운다. 씬 언로드는 오브젝트마다 여기를 지나므로 새 목록을 만들지 않는다.
        const size_t sizeBefore = _listIssue.size();
        size_t       keptCount  = 0;
        for ( size_t issueIndex = 0; issueIndex < sizeBefore; ++issueIndex )
        {
            if ( _listIssue[issueIndex]._sourceID == sourceID )
                continue;
            if ( keptCount != issueIndex )
                _listIssue[keptCount] = std::move( _listIssue[issueIndex] );
            ++keptCount;
        }
        _listIssue.resize( keptCount );
        const bool bRemovedAny = keptCount != sizeBefore;
        for ( const ValidationIssue& issue : listIssue )
        {
            _listIssue.push_back( issue );
            _listIssue.back()._sourceID = sourceID;
        }
        if ( bRemovedAny || listIssue.empty() == false )
            _revision.fetch_add( 1, std::memory_order_acq_rel );
    }

    void ValidationIssueLog::removeSource( const uint64 sourceID )
    {
        replaceIssues( sourceID, {} );
    }

    void ValidationIssueLog::collectIssues( vector<ValidationIssue>& outListIssue ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outListIssue = _listIssue;
    }

    uint32 ValidationIssueLog::getIssueCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<uint32>( _listIssue.size() );
    }

    void ValidationIssueLog::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listIssue.empty() == false )
            _revision.fetch_add( 1, std::memory_order_acq_rel );
        _listIssue.clear();
    }
} // namespace sw
