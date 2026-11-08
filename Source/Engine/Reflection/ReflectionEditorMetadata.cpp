#include "pch.h"

#include "Engine/Reflection/ReflectionEditorMetadata.h"

#include "Core/Registry/RegistrationList.h"

#include "Engine/Reflection/ReflectionConstants.h"

/**
 * @file ReflectionEditorMetadata.cpp
 * @brief 에디터 메타데이터의 기본값과 개발 전용 생성 단위가 올리는 줄의 정적 등록 목록입니다.
 */
namespace sw
{
    namespace
    {
        /** @brief FQN 을 이름으로 든 줄 목록입니다. 함수 정적이라 다른 TU · DLL 의 정적 등록자가 먼저 와도 비어 있는 채로 선다. */
        SW_DEFINE_REGISTRATION_LIST( getTypeEditorMetadataSources, const TypeEditorMetadataSource, "TypeEditorMetadataRegistry", RegistrationOrder::Insertion, true )
        SW_DEFINE_REGISTRATION_LIST( getEnumEditorMetadataSources, const EnumEditorMetadataSource, "EnumEditorMetadataRegistry", RegistrationOrder::Insertion, true )

        struct ReflectionEditorMetadataInternal
        {
            static const string* findInMap( const unordered_map<hashed_string, string>& mapCustomMeta, const hashed_string& key ) noexcept
            {
                const auto iter = mapCustomMeta.find( key );
                return iter != mapCustomMeta.end() ? &iter->second : nullptr;
            }
        };
    } // namespace

    TypeEditorMetadata::TypeEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _bHideInMenu{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    const string* TypeEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const TypeEditorMetadata& TypeEditorMetadata::getDefault() noexcept
    {
        static const TypeEditorMetadata s_default{};
        return s_default;
    }

    PropertyEditorMetadata::PropertyEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _editCondition{}
        , _fileFilter{}
        , _uiMinRange{ 0.0f }
        , _uiMaxRange{ 1.0f }
        , _bHasUIMinRange{ SW_FALSE }
        , _bHasUIMaxRange{ SW_FALSE }
        , _bEditConditionHides{ SW_FALSE }
        , _bColorHDR{ SW_FALSE }
        , _bMultiline{ SW_FALSE }
        , _bHideInInspector{ SW_FALSE }
        , _reservedDisplay{ 0 }
    {
    }

    const string* PropertyEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const PropertyEditorMetadata& PropertyEditorMetadata::getDefault() noexcept
    {
        static const PropertyEditorMetadata s_default{};
        return s_default;
    }

    FunctionEditorMetadata::FunctionEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _editorPreview{}
        , _mapCustomMeta{}
        , _bCallInEditor{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    const string* FunctionEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const FunctionEditorMetadata& FunctionEditorMetadata::getDefault() noexcept
    {
        static const FunctionEditorMetadata s_default{};
        return s_default;
    }

    const string* EnumEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    bool TypeEditorMetadataSource::add( const TypeEditorMetadataSource* pSource )
    {
        return getTypeEditorMetadataSources().addLogged( pSource, "Type editor metadata", pSource->_pFqn ) == RegistrationResult::Added;
    }

    void TypeEditorMetadataSource::remove( const TypeEditorMetadataSource* pSource )
    {
        (void)getTypeEditorMetadataSources().remove( pSource ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    bool EnumEditorMetadataSource::add( const EnumEditorMetadataSource* pSource )
    {
        return getEnumEditorMetadataSources().addLogged( pSource, "Enum editor metadata", pSource->_pFqn ) == RegistrationResult::Added;
    }

    void EnumEditorMetadataSource::remove( const EnumEditorMetadataSource* pSource )
    {
        (void)getEnumEditorMetadataSources().remove( pSource );
    }

    const TypeEditorMetadataSource* TypeEditorMetadataSource::find( const string_view fqn )
    {
        return getTypeEditorMetadataSources().findByName( fqn );
    }

    const EnumEditorMetadataSource* EnumEditorMetadataSource::find( const string_view fqn )
    {
        return getEnumEditorMetadataSources().findByName( fqn );
    }
} // namespace sw
