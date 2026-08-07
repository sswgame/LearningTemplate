/**
 * @file GlobalVariableManager.cpp
 * @brief 전역 변수 매니저 구현
 */
#include "pch.h"
#include "GlobalVariableManager.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/StringUtil.h"
#include <charconv>
#include "Core/Utility/CommandLine/CommandLineManager.h"
namespace sw
{

	bool GlobalVariableInfo::getValueAsBool() const
	{
		if ( _pData != nullptr && _type == GlobalVariableType::Bool )
		{
			return *static_cast<bool*>( _pData );
		}
		return false;
	}

	int32 GlobalVariableInfo::getValueAsInt() const
	{
		if ( _pData != nullptr && ( _type == GlobalVariableType::Int32 || _type == GlobalVariableType::Enum ) )
		{
			return *static_cast<int32*>( _pData );
		}
		return 0;
	}

	float32 GlobalVariableInfo::getValueAsFloat() const
	{
		if ( _pData != nullptr && _type == GlobalVariableType::Float )
		{
			return *static_cast<float32*>( _pData );
		}
		return 0.0f;
	}

	std::string GlobalVariableInfo::getValueAsString() const
	{
		if ( _pData == nullptr )
		{
			return "";
		}

		switch ( _type )
		{
			case GlobalVariableType::Bool:
				return *static_cast<bool*>( _pData ) ? "true" : "false";
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
				return std::to_string( *static_cast<int32*>( _pData ) );
			case GlobalVariableType::Float:
				return std::to_string( *static_cast<float32*>( _pData ) );
			case GlobalVariableType::String:
				return *static_cast<std::string*>( _pData );
		}
		return "";
	}

	bool GlobalVariableInfo::setValueFromString( const std::string_view strValue )
	{
		if ( _pData == nullptr )
		{
			return false;
		}

		switch ( _type )
		{
			case GlobalVariableType::Bool:
			{
				const std::string lowerVal = sw::StringUtil::toLower( std::string( strValue ) );

				bool bVal					  = ( lowerVal == "true" || lowerVal == "1" || lowerVal == "yes" || lowerVal == "on" );
				*static_cast<bool*>( _pData ) = bVal;
				if ( _onValueChanged.isBound() )
					_onValueChanged( this );
				return true;
			}
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
			{
				int32 val	   = 0;
				auto [ptr, ec] = std::from_chars( strValue.data(), strValue.data() + strValue.size(), val );
				if ( ec == std::errc{} )
				{
					*static_cast<int32*>( _pData ) = val;
					if ( _onValueChanged.isBound() )
						_onValueChanged( this );
					return true;
				}
				return false;
			}
			case GlobalVariableType::Float:
			{
				float32 val	   = 0.0f;
				auto [ptr, ec] = std::from_chars( strValue.data(), strValue.data() + strValue.size(), val );
				if ( ec == std::errc{} )
				{
					*static_cast<float32*>( _pData ) = val;
					if ( _onValueChanged.isBound() )
						_onValueChanged( this );
					return true;
				}
				return false;
			}
			case GlobalVariableType::String:
			{
				*static_cast<std::string*>( _pData ) = std::string{ strValue };
				if ( _onValueChanged.isBound() )
					_onValueChanged( this );
				return true;
			}
		}
		return false;
	}

	void GlobalVariableInfo::resetToDefault()
	{
		if ( _pData == nullptr )
		{
			return;
		}

		switch ( _type )
		{
			case GlobalVariableType::Bool:
				if ( std::holds_alternative<bool>( _defaultValue ) )
				{
					*static_cast<bool*>( _pData ) = std::get<bool>( _defaultValue );
				}
				break;
			case GlobalVariableType::Int32:
			case GlobalVariableType::Enum:
				if ( std::holds_alternative<int32>( _defaultValue ) )
				{
					*static_cast<int32*>( _pData ) = std::get<int32>( _defaultValue );
				}
				break;
			case GlobalVariableType::Float:
				if ( std::holds_alternative<float32>( _defaultValue ) )
				{
					*static_cast<float32*>( _pData ) = std::get<float32>( _defaultValue );
				}
				break;
			case GlobalVariableType::String:
				if ( std::holds_alternative<std::string>( _defaultValue ) )
				{
					*static_cast<std::string*>( _pData ) = std::get<std::string>( _defaultValue );
				}
				break;
		}

		if ( _onValueChanged.isBound() )
		{
			_onValueChanged( this );
		}
	}

	GlobalVariableRegistrar*& GlobalVariableRegistrar::getHead()
	{
		static GlobalVariableRegistrar* s_head = nullptr;
		return s_head;
	}

	void GlobalVariableRegistrar::linkTo( GlobalVariableRegistrar*& moduleHead )
	{
		_next	   = moduleHead;
		moduleHead = this;
	}

	GlobalVariableRegistrar::GlobalVariableRegistrar( const utf8* name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, std::string>& defaultValue, const utf8* description, const utf8* enumType, const utf8* moduleName )
		: _name{ name }
		, _type{ type }
		, _pData{ pData }
		, _defaultValue{ defaultValue }
		, _description{ description }
		, _enumType{ enumType }
		, _moduleName{ moduleName }
		, _next{ nullptr }
	{
		linkTo( getHead() );
	}

	GlobalVariableRegistrar::GlobalVariableRegistrar( GlobalVariableRegistrar*& moduleHead, const utf8* name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, std::string>& defaultValue, const utf8* description, const utf8* enumType, const utf8* moduleName )
		: _name{ name }
		, _type{ type }
		, _pData{ pData }
		, _defaultValue{ defaultValue }
		, _description{ description }
		, _enumType{ enumType }
		, _moduleName{ moduleName }
		, _next{ nullptr }
	{
		linkTo( moduleHead );
	}

	void registerCoreGlobalVariables()
	{
		getGlobalVariableManager().registerPendingVariables( "Core", GlobalVariableRegistrar::getHead() );
	}

	bool GlobalVariableManager::initialize()
	{
		return true;
	}

	void GlobalVariableManager::registerPendingVariables( const std::string_view moduleName, GlobalVariableRegistrar* pHead )
	{
		// pHead must be one module's list — never the mixed cross-DLL chain.
		GlobalVariableRegistrar* current = pHead;
		while ( current != nullptr )
		{
			if ( findVariable( current->_name ) == nullptr )
			{
				registerVariable( current->_name, current->_type, current->_pData, current->_defaultValue, current->_description, current->_enumType, moduleName );
			}
			current = current->_next;
		}
	}

	void GlobalVariableManager::unregisterVariablesByModule( const std::string_view moduleName )
	{
		std::string strModule{ moduleName };
		for ( auto it = _variables.begin(); it != _variables.end(); )
		{
			if ( it->second._moduleName == strModule )
			{
				it = _variables.erase( it );
			}
			else
			{
				++it;
			}
		}
	}

	void GlobalVariableManager::registerToCommandLine( CommandLineManager* pCmdLineManager )
	{
		if ( pCmdLineManager == nullptr )
			return;

		for ( const auto& pair : _variables )
		{
			const GlobalVariableInfo& info = pair.second;
			if ( std::holds_alternative<int32>( info._defaultValue ) )
				pCmdLineManager->addArgument<int32>( { info._name }, false, static_cast<int32>( std::get<int32>( info._defaultValue ) ), true );
			else if ( std::holds_alternative<float32>( info._defaultValue ) )
				pCmdLineManager->addArgument<float32>( { info._name }, false, static_cast<float32>( std::get<float32>( info._defaultValue ) ), true );
			else if ( std::holds_alternative<bool>( info._defaultValue ) )
				pCmdLineManager->addArgument<bool>( { info._name }, false, static_cast<bool>( std::get<bool>( info._defaultValue ) ), true );
			else if ( std::holds_alternative<std::string>( info._defaultValue ) )
				pCmdLineManager->addArgument<std::string>( { info._name }, false, std::string( std::get<std::string>( info._defaultValue ) ), true );
		}
	}

	void GlobalVariableManager::updateFromCommandLine( CommandLineManager* pCmdLineManager )
	{
		if ( pCmdLineManager == nullptr )
			return;

		for ( const auto& pair : _variables )
		{
			const std::string&		  name = pair.first;
			const GlobalVariableInfo& info = pair.second;

			if ( info._type == GlobalVariableType::Int32 )
			{
				int32 val = 0;
				if ( pCmdLineManager->getArgument( name, val ) )
					setValueFromString( name, std::to_string( val ) );
			}
			else if ( info._type == GlobalVariableType::Float )
			{
				float32 val = 0.0f;
				if ( pCmdLineManager->getArgument( name, val ) )
					setValueFromString( name, std::to_string( val ) );
			}
			else if ( info._type == GlobalVariableType::Bool )
			{
				bool val = false;
				if ( pCmdLineManager->getArgument( name, val ) )
					setValueFromString( name, val ? "true" : "false" );
			}
			else if ( info._type == GlobalVariableType::String )
			{
				std::string val;
				if ( pCmdLineManager->getArgument( name, val ) )
					setValueFromString( name, val );
			}
		}
	}

	bool GlobalVariableManager::registerVariable( const std::string_view name, GlobalVariableType type, void* pData, const std::variant<bool, int32, float32, std::string>& defaultValue, const std::string_view description, const std::string_view enumType, const std::string_view moduleName )
	{
		if ( name.empty() == true || pData == nullptr )
		{
			return false;
		}

		std::string strName{ name };
		if ( _variables.find( strName ) != _variables.end() )
		{

			SW_LOG_WARNING( "[GlobalVariableManager] Variable %# is already registered.", strName.c_str() );
			return false;
		}

		GlobalVariableInfo info;
		info._name		   = strName;
		info._type		   = type;
		info._pData		   = pData;
		info._defaultValue = defaultValue;
		info._description  = std::string{ description };
		info._enumType	   = std::string{ enumType };
		info._moduleName   = std::string{ moduleName };

		_variables.emplace( strName, std::move( info ) );
		return true;
	}

	GlobalVariableInfo* GlobalVariableManager::findVariable( const std::string& name )
	{
		auto iter = _variables.find( name );
		if ( iter != _variables.end() )
		{
			return &iter->second;
		}
		return nullptr;
	}

	const std::unordered_map<std::string, GlobalVariableInfo>& GlobalVariableManager::getAllVariables() const
	{
		return _variables;
	}

	bool GlobalVariableManager::setValueFromString( const std::string& name, const std::string& strValue )
	{
		GlobalVariableInfo* pInfo = findVariable( name );
		if ( pInfo != nullptr )
		{
			return pInfo->setValueFromString( strValue );
		}
		return false;
	}

	bool GlobalVariableManager::resetToDefault( const std::string& name )
	{
		GlobalVariableInfo* pInfo = findVariable( name );
		if ( pInfo != nullptr )
		{
			pInfo->resetToDefault();
			return true;
		}
		return false;
	}

	void GlobalVariableManager::resetAllToDefault()
	{
		for ( auto& [name, info] : _variables )
		{
			info.resetToDefault();
		}
	}
} // namespace sw
