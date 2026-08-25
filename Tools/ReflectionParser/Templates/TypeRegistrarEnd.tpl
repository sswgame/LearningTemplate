			registry.registerClass( info );
$AliasRegs
		}

		${Id}_Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static ${Id}_Registrar s_${Id}_registrar{};

