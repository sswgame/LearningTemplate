			registry.registerEnum( info );
$AliasRegs
		}

		Registrar()
		{
			static ::sw::EnumRegistrar reg{ &RegisterEnum };
		}
	};
	static Registrar<$FQN> s_${ID}_registrar{};

