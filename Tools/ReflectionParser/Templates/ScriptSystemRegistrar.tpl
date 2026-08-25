	// ScriptSystem — $FQN
	static void ${Id}_RegisterScriptSystem( sw::GameObjectManager* manager )
	{
		manager->registerScriptSystemTick( sw::Delegate<void(sw::GameObjectManager*, sw::Registry&, float32, bool)>(
			[]([[maybe_unused]] sw::GameObjectManager* mgr, sw::Registry& reg, float32 dt, bool bParallel)
			{
				sw::sparse_set<$FQN>* view = reg.getSparseSet<$FQN>();
				if (view == nullptr) return;

				const uint32 count = static_cast<uint32>(view->size());
				if (count == 0) return;

				auto doTick = []( auto& comp, float32 deltaTime )
				{
					if constexpr (sw::has_isActive<std::decay_t<decltype(comp)>>::value) {
						if (!comp.isActive()) return;
					}
					if constexpr (sw::has_isPendingKill<std::decay_t<decltype(comp)>>::value) {
						if (comp.isPendingKill()) return;
					}
					if constexpr (sw::has_tick<std::decay_t<decltype(comp)>, void(float32)>::value) {
						comp.tick(deltaTime);
					}
					else if constexpr (sw::has_onTick<std::decay_t<decltype(comp)>, void(float32)>::value) {
						comp.onTick(deltaTime);
					}
				};

				if ( bParallel && count >= 64 )
				{
					sw::TaskManager& taskMgr = ::sw::getTaskManager();
					sw::TaskStageHandle stage = taskMgr.createAnonymousStage( "${Id}_Tick" );
					sw::TaskHandle handle = taskMgr.emplaceParallelBlock( 0, count, [&reg, view, dt, doTick]( uint32 start, uint32 end )
					{
						const auto& keys = view->getDenseKeys();
						for ( uint32 i = start; i < end; ++i )
						{
							auto compHandle = reg.handleFor<$FQN>( reg.handleFromIndex( keys[i] ) );
							if ( $FQN* comp = compHandle.get() )
								doTick( *comp, dt );
						}
					} );
					stage.addTask( handle );
					handle.submit();
					taskMgr.waitStage( stage );
				}
				else
				{
					const auto& keys = view->getDenseKeys();
					for (uint32 i = 0; i < count; ++i)
					{
						auto compHandle = reg.handleFor<$FQN>( reg.handleFromIndex( keys[i] ) );
						if ( $FQN* comp = compHandle.get() )
							doTick( *comp, dt );
					}
				}
			}
		));
        
        manager->registerScriptSystemBeginPlay( sw::Delegate<void(sw::GameObjectManager*, sw::Registry&)>(
            [](sw::GameObjectManager* mgr, sw::Registry& reg) {
				sw::sparse_set<$FQN>* view = reg.getSparseSet<$FQN>();
				if (view == nullptr) return;

				const uint32 count = static_cast<uint32>(view->size());
				if (count == 0) return;

				auto doBeginPlay = []( auto& comp, sw::GameObjectManager* pTargetMgr, sw::Entity e )
				{
					if constexpr (sw::has_owner<std::decay_t<decltype(comp)>>::value) {
						comp.owner = pTargetMgr->findGameObjectByEntity(e);
					}
					if constexpr (sw::has_setOwner<std::decay_t<decltype(comp)>>::value) {
						comp.setOwner(pTargetMgr->findGameObjectByEntity(e));
					}
					if constexpr (sw::has_beginPlay<std::decay_t<decltype(comp)>>::value) {
						comp.beginPlay();
					}
					else if constexpr (sw::has_onBeginPlay<std::decay_t<decltype(comp)>>::value) {
						comp.onBeginPlay();
					}
				};

				const auto& keys = view->getDenseKeys();
				for (uint32 i = 0; i < count; ++i)
				{
					const sw::Entity e = reg.handleFromIndex(keys[i]);
					auto compHandle = reg.handleFor<$FQN>( e );
					if ( $FQN* comp = compHandle.get() )
						doBeginPlay( *comp, mgr, e );
				}
            }
        ));
        
        manager->registerScriptSystemEndPlay( sw::Delegate<void(sw::GameObjectManager*, sw::Registry&)>(
            []([[maybe_unused]] sw::GameObjectManager* mgr, sw::Registry& reg) {
				sw::sparse_set<$FQN>* view = reg.getSparseSet<$FQN>();
				if (view == nullptr) return;

				const uint32 count = static_cast<uint32>(view->size());
				if (count == 0) return;

				auto doEndPlay = []( auto& comp )
				{
					if constexpr (sw::has_endPlay<std::decay_t<decltype(comp)>>::value) {
						comp.endPlay();
					}
					else if constexpr (sw::has_onEndPlay<std::decay_t<decltype(comp)>>::value) {
						comp.onEndPlay();
					}
				};

				const auto& keys = view->getDenseKeys();
				for (uint32 i = 0; i < count; ++i)
				{
					auto compHandle = reg.handleFor<$FQN>( reg.handleFromIndex( keys[i] ) );
					if ( $FQN* comp = compHandle.get() )
						doEndPlay( *comp );
				}
			}
        ));
	}
	static sw::ScriptSystemRegistrar s_${Id}_scriptSystem{ &${Id}_RegisterScriptSystem };
