#pragma once
/**
 * @file ReflectionTypes.h
 * @brief Property / enum / function / type metadata for reflection
 */

#include "Core/CoreMinimal.h"
#include "Core/Reflection/ReflectionMacros.h"
#include "Core/Reflection/ReflectionContainers.h"
#include "Core/Utility/String/string_splitter.h"
#include "Core/Utility/Task/TaskTypes.h"

namespace sw
{

	struct PropertyMetadata
	{
		std::string _category  = "General";
		std::string _tooltip   = "";
		float32		_minRange  = 0.0f;
		float32		_maxRange  = 1.0f;
		bool		_bHasRange = false;
	};

	struct PropertyInfo
	{

		std::shared_ptr<IContainerWrapper> _containerWrapper;

		size_t _offset = 0;

		hashed_string	 _name;
		hashed_string	 _typeName;
		hashed_string	 _elementTypeName;
		hashed_string	 _keyTypeName;
		hashed_string	 _alias;
		PropertyMetadata _metadata;

		mutable uint32 _cachedNameHash	= 0;
		mutable uint32 _cachedAliasHash = 0;

		ContainerKind _containerKind = ContainerKind::None;
		uint8		  _bIsContainer : 1;

		PropertyInfo()
			: _bIsContainer( 0 )
		{
		}

		PropertyInfo( hashed_string name, hashed_string typeName, size_t offset,
					  bool bIsContainer = false, ContainerKind containerKind = ContainerKind::None,
					  hashed_string elementTypeName = {}, hashed_string keyTypeName = {},
					  std::shared_ptr<IContainerWrapper> containerWrapper = nullptr,
					  hashed_string						 alias			  = {} )
			: _containerWrapper( std::move( containerWrapper ) )
			, _offset( offset )
			, _name( name )
			, _typeName( typeName )
			, _elementTypeName( elementTypeName )
			, _keyTypeName( keyTypeName )
			, _alias( alias )
			, _containerKind( containerKind )
			, _bIsContainer( bIsContainer ? 1 : 0 )
		{
		}

		uint32 getNameHash() const noexcept
		{
			if ( _cachedNameHash == 0 && _name.empty() == false )
				_cachedNameHash = _name.getHash();
			return _cachedNameHash;
		}

		uint32 getAliasHash() const noexcept
		{
			if ( _cachedAliasHash == 0 && _alias.empty() == false )
				_cachedAliasHash = _alias.getHash();
			return _cachedAliasHash;
		}

		bool matchesName( const hashed_string& nameOrAlias ) const
		{
			return _name == nameOrAlias || ( _alias.empty() == false && _alias == nameOrAlias );
		}

		using PropertyBindingDelegate = Delegate<void( const PropertyInfo& prop, const void* instance )>;
		mutable PropertyBindingDelegate _onPropertyBoundChanged;

		void bindOnChanged( PropertyBindingDelegate delegate ) const
		{
			_onPropertyBoundChanged = std::move( delegate );
		}

		template <typename T, typename ObjectType>
		void setValue( ObjectType* instance, const T& newValue ) const
		{
			T* ptr = reinterpret_cast<T*>( reinterpret_cast<utf8*>( instance ) + _offset );
			if ( *ptr == newValue )
				return;

			*ptr = newValue;
			if constexpr ( std::is_base_of_v<sw::IPropertyObserver, ObjectType> )
			{
				sw::IPropertyObserver* observer = static_cast<sw::IPropertyObserver*>( instance );
				observer->onPropertyChanged( _name );
			}

			if ( _onPropertyBoundChanged.isBound() )
			{
				_onPropertyBoundChanged( *this, instance );
			}
		}

		template <typename T>
		T* getValuePtr( void* instance ) const
		{
			return reinterpret_cast<T*>( reinterpret_cast<utf8*>( instance ) + _offset );
		}

		template <typename T>
		const T* getValuePtr( const void* instance ) const
		{
			return reinterpret_cast<const T*>( reinterpret_cast<const utf8*>( instance ) + _offset );
		}
	};

	struct EnumInfo
	{
		std::unordered_map<hashed_string, int64> _mapNameToValue;
		std::unordered_map<int64, hashed_string> _mapValueToName;
		hashed_string							 _name;
		hashed_string							 _fullyQualifiedName;
		hashed_string							 _moduleName;
		uint8									 _bIsBitFlag : 1;

		EnumInfo()
			: _bIsBitFlag( 0 )
		{
		}

		hashed_string toString( int64 val ) const
		{
			auto iter = _mapValueToName.find( val );
			return iter != _mapValueToName.end() ? iter->second : hashed_string();
		}

		hashed_string toStringFlags( int64 val ) const
		{
			if ( _bIsBitFlag == false )
				return toString( val );

			if ( val == 0 )
			{
				auto iter = _mapValueToName.find( 0 );
				return iter != _mapValueToName.end() ? iter->second : hashed_string( "None" );
			}

			std::string result;
			for ( const auto& [name, bitVal] : _mapNameToValue )
			{
				if ( bitVal != 0 && ( val & bitVal ) == bitVal )
				{
					if ( result.empty() == false )
						result += " | ";
					result += name.c_str();
				}
			}

			return hashed_string( result.c_str() );
		}

		int64 stringFlagsToValue( const std::string_view& flagsStr ) const
		{
			if ( _bIsBitFlag == false )
			{
				hashed_string nameKey( flagsStr.data(), static_cast<uint32>( flagsStr.size() ) );
				auto		  iter = _mapNameToValue.find( nameKey );
				return iter != _mapNameToValue.end() ? iter->second : 0;
			}

			int64			intResult = 0;
			/**
			 * @brief 분할기를 반환합니다
			 */
			string_splitter splitter( flagsStr, { "|" } );
			for ( const auto& tokenView : splitter.getSplitList() )
			{
				std::string trimmedStr = StringUtil::trim( std::string( tokenView ) );
				if ( trimmedStr.empty() == false )
				{
					hashed_string tokenKey( trimmedStr.c_str(), static_cast<uint32>( trimmedStr.size() ) );
					auto		  iter = _mapNameToValue.find( tokenKey );
					if ( iter != _mapNameToValue.end() )
					{
						intResult |= iter->second;
					}
				}
			}

			return intResult;
		}
	};

	struct FunctionInfo
	{
		std::string									  _name;
		hashed_string								  _hashName;
		Delegate<TaskValue( void*, const TaskArgs& )> _invoker;
	};

	struct TypeInfo
	{
		hashed_string												   _name;
		hashed_string												   _fullyQualifiedName;
		hashed_string												   _parentFQN;
		hashed_string												   _moduleName;
		size_t														   _size;
		std::vector<PropertyInfo>									   _propertyList;
		std::vector<FunctionInfo>									   _methods;
		mutable std::unordered_map<hashed_string, const PropertyInfo*> _mapNameToProperty;
		mutable std::unordered_map<hashed_string, const FunctionInfo*> _mapNameToMethod;
		mutable bool												   _bIsCacheBuilt	 = false;
		mutable bool												   _bIsPODFastPath	 = false;
		mutable bool												   _bIsPODCalculated = false;

		/**
		 * @brief PODFastPath 여부를 반환합니다
		 */
		bool isPODFastPath() const;
		/**
		 * @brief A 여부를 반환합니다
		 */
		bool isA( const hashed_string& targetFqn ) const;

		void buildLookupCache() const
		{
			if ( _bIsCacheBuilt )
				return;

			for ( const sw::PropertyInfo& prop : _propertyList )
			{
				_mapNameToProperty[prop._name] = &prop;
				if ( prop._alias.empty() == false )
				{
					_mapNameToProperty[prop._alias] = &prop;
				}
			}

			for ( const sw::FunctionInfo& method : _methods )
			{
				_mapNameToMethod[method._hashName] = &method;
			}

			_bIsCacheBuilt = true;
		}

		const PropertyInfo* findProperty( const hashed_string& propNameOrAlias ) const
		{
			if ( _propertyList.size() <= 4 )
			{
				for ( const sw::PropertyInfo& prop : _propertyList )
				{
					if ( prop.matchesName( propNameOrAlias ) )
						return &prop;
				}
				return nullptr;
			}

			buildLookupCache();
			auto it = _mapNameToProperty.find( propNameOrAlias );
			return it != _mapNameToProperty.end() ? it->second : nullptr;
		}

		const FunctionInfo* findMethod( const hashed_string& methodName ) const
		{
			if ( _methods.size() <= 4 )
			{
				for ( const sw::FunctionInfo& method : _methods )
				{
					if ( method._hashName == methodName )
						return &method;
				}
				return nullptr;
			}

			buildLookupCache();
			auto it = _mapNameToMethod.find( methodName );
			return it != _mapNameToMethod.end() ? it->second : nullptr;
		}
	};

}
