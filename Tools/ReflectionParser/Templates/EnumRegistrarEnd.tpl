			registry.registerEnum( info );
$AliasRegs
		}

		${Id}_Registrar()
		{
			static ::sw::EnumRegistrar reg{ &RegisterEnum };
		}
	};
	static ${Id}_Registrar s_${Id}_registrar{};

