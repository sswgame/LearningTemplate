	// ComponentFactory — $FQN (REFLECT_BODY)
	static void ${Id}_RegisterComponentFactory( ::sw::GameObjectManager& manager )
	{
		manager.registerComponentType<$FQN>( ::sw::hashed_string( "$Name" ), ::sw::hashed_string( "$ModuleName" ) );
	}
	static ::sw::ComponentFactoryRegistrar s_${Id}_componentFactory{
		&${Id}_RegisterComponentFactory, SW_COMPONENT_FACTORY_MODULE_HEAD() };
