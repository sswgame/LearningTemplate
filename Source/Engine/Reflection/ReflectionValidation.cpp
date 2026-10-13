#include "pch.h"

#include "Engine/Reflection/ReflectionValidation.h"

#include "Core/Memory/LifetimeScope.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct ReflectionValidationInternal
        {
            /** @brief 결과 표 본체입니다. 처음 쓸 때 만들어지고 엔진 수명 범위가 닫히면 소멸합니다. */
            static inline ScopeOwned<ValidationIssueLog> s_validationIssueLog;

            /** @brief 값으로 든 반사 구조체의 타입입니다(내장 타입 · 열거형은 아니다). 없으면 nullptr 입니다. */
            static const TypeInfo* findNestedType( const hashed_string& typeName )
            {
                if ( typeName.empty() )
                    return nullptr;
                const TypeInfo* pType = engine::getTypeRegistry().findType( typeName );
                if ( pType == nullptr || pType->isPrimitive() )
                    return nullptr;
                return pType;
            }

            /** @brief 시퀀스 원소가 반사 구조체면 그 타입입니다(맵 · 중첩 컨테이너는 내려가지 않는다). */
            static const TypeInfo* findSequenceElementType( const PropertyInfo& prop )
            {
                const bool bNestedContainer = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                if ( prop._bIsContainer == SW_FALSE || prop._containerWrapper == nullptr || prop._containerWrapper->asSequence() == nullptr || bNestedContainer )
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
                    const TypeInfo* pNested = prop._bIsContainer == SW_TRUE ? findSequenceElementType( prop ) : findNestedType( prop._typeName );
                    if ( pNested != nullptr && pNested != &type && computeHasValidator( *pNested, depth + 1 ) )
                        return true;
                }
                return false;
            }

            static uint32 validate( const TypeInfo& type, const void* pInstance, ValidationContext& context, const uint32 depth )
            {
                if ( pInstance == nullptr || depth > constant::reflection::kMaxParentChainDepth || ReflectionValidation::hasValidator( type ) == false )
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
                    const void* pValue = prop.getRawPtr( pInstance );
                    if ( prop._bIsContainer == SW_FALSE )
                    {
                        const TypeInfo* pNested = findNestedType( prop._typeName );
                        if ( pNested != nullptr )
                            validate( *pNested, pValue, context, depth + 1 );
                        continue;
                    }
                    const TypeInfo* pElement = findSequenceElementType( prop );
                    if ( pElement == nullptr )
                        continue;
                    ISequenceContainerWrapper* pSequence    = prop._containerWrapper->asSequence();
                    const size_t               elementCount = pSequence->getSize( pValue );
                    for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                    {
                        validate( *pElement, pSequence->getElementConst( pValue, elementIndex ), context, depth + 1 );
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
        add( ValidationSeverity::Error, message );
    }

    void ValidationContext::addWarning( const string_view message )
    {
        add( ValidationSeverity::Warning, message );
    }

    bool ValidationContext::hasError() const noexcept
    {
        for ( const ValidationIssue& issue : _listIssue )
        {
            if ( issue._severity == ValidationSeverity::Error )
                return true;
        }
        return false;
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

    void ValidationContext::add( const ValidationSeverity severity, const string_view message )
    {
        ValidationIssue issue;
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
        return ReflectionValidationInternal::validate( type, pInstance, context, 0 );
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

    ValidationIssueLog& ValidationIssueLog::get()
    {
        ValidationIssueLog* const pLog = ReflectionValidationInternal::s_validationIssueLog.getOrCreate();
        if ( pLog != nullptr )
            return *pLog;
        // 엔진 종료 뒤의 접근이다(단언은 `getOrCreate` 가 했다). 결과를 받지 않은 빈 표로 답한다.
        static ValidationIssueLog s_closedFallback;
        return s_closedFallback;
    }

    ValidationIssueLog* ValidationIssueLog::find()
    {
        return ReflectionValidationInternal::s_validationIssueLog.find();
    }

    void ValidationIssueLog::replaceIssues( const uint64 sourceID, const vector<ValidationIssue>& listIssue )
    {
        std::lock_guard<mutex> lock( _mutex );
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
        std::lock_guard<mutex> lock( _mutex );
        outListIssue = _listIssue;
    }

    uint32 ValidationIssueLog::getIssueCount() const
    {
        std::lock_guard<mutex> lock( _mutex );
        return static_cast<uint32>( _listIssue.size() );
    }

    void ValidationIssueLog::clear()
    {
        std::lock_guard<mutex> lock( _mutex );
        if ( _listIssue.empty() == false )
            _revision.fetch_add( 1, std::memory_order_acq_rel );
        _listIssue.clear();
    }
} // namespace sw
