			registry.registerClass( ::std::move( info ) );
$AliasRegs
		}
		Registrar()
		{
			static ::sw::TypeRegistrar reg{ &RegisterType };
		}
	};
	static Registrar<$Key> s_${ID}_registrar{};

