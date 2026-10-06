# ==============================================================================
# @file Source/GameFramework/Kits/CMakeLists.txt
# @brief GameFramework 장르 키트 (장르별 — 목록은 README)
# ==============================================================================

# 키트는 서로 독립이고 모두 GameFramework 를 링크한다. 폴더는 `<장르 묶음>/<키트>`, 타겟은 `GF_<키트>`(묶음 이름은 타겟에 들지 않는다).

# ------------------------------------------------------------------------------
# 1) 액션 · 대전
# ------------------------------------------------------------------------------
add_subdirectory(Action/ActionAdventure)
add_subdirectory(Action/ActionCombat)
add_subdirectory(Action/ActionPlatformer)
add_subdirectory(Action/BattleRoyale)
add_subdirectory(Action/Fighting)
add_subdirectory(Action/MechArena)
add_subdirectory(Action/Metroidvania)

# ------------------------------------------------------------------------------
# 2) 공포 · 조사
# ------------------------------------------------------------------------------
add_subdirectory(Horror/AsymmetricHorror)
add_subdirectory(Horror/CoopScavenger)
add_subdirectory(Horror/GhostHunt)
add_subdirectory(Horror/SurvivalHorror)

# ------------------------------------------------------------------------------
# 3) RPG
# ------------------------------------------------------------------------------
add_subdirectory(Rpg/ClassicJrpg)
add_subdirectory(Rpg/MonsterCollector)
add_subdirectory(Rpg/OpenWorldWestern)
add_subdirectory(Rpg/Overworld)
add_subdirectory(Rpg/WitcherRpg)

# ------------------------------------------------------------------------------
# 4) 전략
# ------------------------------------------------------------------------------
add_subdirectory(Strategy/CityBuilder)
add_subdirectory(Strategy/RealTimeStrategy)
add_subdirectory(Strategy/SideScrollConquest)
add_subdirectory(Strategy/TacticsSrpg)

# ------------------------------------------------------------------------------
# 5) 시뮬레이션 · 생활
# ------------------------------------------------------------------------------
add_subdirectory(Simulation/CreatureLife)
add_subdirectory(Simulation/Farming)
add_subdirectory(Simulation/RestaurantSim)
add_subdirectory(Simulation/ThemePark)
add_subdirectory(Simulation/Voxel)

# ------------------------------------------------------------------------------
# 6) 캐주얼 · 파티
# ------------------------------------------------------------------------------
add_subdirectory(Casual/CardGame)
add_subdirectory(Casual/KartRacing)
add_subdirectory(Casual/PartyArena)
add_subdirectory(Casual/Rhythm)

# ------------------------------------------------------------------------------
# 7) 네트워크 방식 — 장르 키트는 이것들을 include 하지 않는다(게임이 둘을 잇는다)
# ------------------------------------------------------------------------------
add_subdirectory(Network/NetClientServer)
add_subdirectory(Network/NetDestruction)
add_subdirectory(Network/NetLockstep)
add_subdirectory(Network/NetMmo)
add_subdirectory(Network/NetSimulation)
add_subdirectory(Network/NetTurnRelay)

# ------------------------------------------------------------------------------
# 8) 온라인 서비스 — 기반 Online/ 계약 위의 서비스(계정 · 거래 …). 서버 전용은 `<묶음>/Server/<키트>`(GF_Server_<키트>)
# ------------------------------------------------------------------------------
add_subdirectory(Online/Account)
add_subdirectory(Online/Admin)
add_subdirectory(Online/Chat)
add_subdirectory(Online/Economy)
add_subdirectory(Online/Leaderboard)
add_subdirectory(Online/LiveOps)
add_subdirectory(Online/Mailbox)
add_subdirectory(Online/Matchmaking)
add_subdirectory(Online/ServerDirectory)
add_subdirectory(Online/Social)
add_subdirectory(Online/Trade)
add_subdirectory(Online/Server/Account)
add_subdirectory(Online/Server/Admin)
add_subdirectory(Online/Server/Chat)
add_subdirectory(Online/Server/Economy)
add_subdirectory(Online/Server/Leaderboard)
add_subdirectory(Online/Server/LiveOps)
add_subdirectory(Online/Server/Mailbox)
add_subdirectory(Online/Server/Matchmaking)
add_subdirectory(Online/Server/ServerDirectory)
add_subdirectory(Online/Server/Social)
add_subdirectory(Online/Server/Trade)

# ------------------------------------------------------------------------------
# 9) 저장 — 서비스 저장 계약(기반 Online/Store)의 드라이버. 서버 전용은 `<묶음>/Server/<키트>`(GF_Server_<키트>)
# ------------------------------------------------------------------------------
add_subdirectory(Storage/SqlStore)
add_subdirectory(Storage/Server/CacheStore)
add_subdirectory(Storage/Server/SqlStore)
