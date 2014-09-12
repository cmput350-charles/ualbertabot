#include "ProductionManager.h"

using namespace UAlbertaBot;

ProductionManager::ProductionManager() 
	: initialBuildSet(false)
	, reservedMinerals(0)
	, reservedGas(0)
	, assignedWorkerForThisBuilding(false)
	, haveLocationForThisBuilding(false)
	, enemyCloakedDetected(false)
	, rushDetected(false)
{
	populateTypeCharMap();

	//if (!Options::Modules::USING_BUILD_ORDER_DEMO)
	{
        //setBuildOrder(StrategyManager::Instance().getOpeningBookBuildOrder());
	}
}

void ProductionManager::setBuildOrder(const std::vector<MetaType> & buildOrder)
{
	// clear the current build order
	queue.clearAll();

	// for each item in the results build order, add it
	for (size_t i(0); i<buildOrder.size(); ++i)
	{
		// queue the item
		queue.queueAsLowestPriority(buildOrder[i], true);
	}
}

void ProductionManager::performBuildOrderSearch(const std::vector<MetaPair> & goal)
{	
    if (!Options::Modules::USING_MACRO_SEARCH || goal.empty())
    {
        return;
    }

    //std::vector<MetaType> buildOrder = BOSSManager::GetOptimizedNaiveBuildOrder(goal);
    //setBuildOrder(buildOrder);

	std::vector<MetaType> buildOrder = BOSSManager::Instance().getBuildOrder();

    if (!buildOrder.empty())
    {
        BWAPI::Broodwar->printf("PM: Build order found!");
	    setBuildOrder(buildOrder);
        BOSSManager::Instance().reset();
        searchGoal = std::vector<MetaPair>();
    }
    else
    {
        if (!BOSSManager::Instance().isSearchInProgress())
        {
            BWAPI::Broodwar->printf("Starting a new search!");
            BOSSManager::Instance().startNewSearch(goal);
        }
    }
}

void ProductionManager::setSearchGoal(MetaPairVector & goal)
{
	searchGoal = goal;
}

void ProductionManager::update() 
{
	// check the queue for stuff we can build
	manageBuildOrderQueue();

	if ((queue.size() == 0) && Options::Modules::USING_BUILD_ORDER_DEMO)
	{
		performBuildOrderSearch(searchGoal);
	}

	// if nothing is currently building, get a new goal from the strategy manager
	if ((queue.size() == 0) && (BWAPI::Broodwar->getFrameCount() > 10) && !Options::Modules::USING_BUILD_ORDER_DEMO)
	{
		BWAPI::Broodwar->drawTextScreen(150, 10, "Nothing left to build, new search!");
		std::vector<MetaPair> newGoal;// = StrategyManager::Instance().getBuildOrderGoal();
        newGoal.push_back(MetaPair(MetaType(BWAPI::UnitTypes::Protoss_Dark_Templar), 2));
		performBuildOrderSearch(newGoal);
	}

	//// detect if there's a build order deadlock once per second
	if ((BWAPI::Broodwar->getFrameCount() % 24 == 0) && detectBuildOrderDeadlock())
	{
		BWAPI::Broodwar->printf("Supply deadlock detected, building pylon!");
		queue.queueAsHighestPriority(MetaType(BWAPI::Broodwar->self()->getRace().getSupplyProvider()), true);
	}

	// if they have cloaked units get a new goal asap
	if (!enemyCloakedDetected && InformationManager::Instance().enemyHasCloakedUnits())
	{
		if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Photon_Cannon) < 2)
		{
			queue.queueAsHighestPriority(MetaType(BWAPI::UnitTypes::Protoss_Photon_Cannon), true);
			queue.queueAsHighestPriority(MetaType(BWAPI::UnitTypes::Protoss_Photon_Cannon), true);
		}

		if (BWAPI::Broodwar->self()->allUnitCount(BWAPI::UnitTypes::Protoss_Forge) == 0)
		{
			queue.queueAsHighestPriority(MetaType(BWAPI::UnitTypes::Protoss_Forge), true);
		}

		BWAPI::Broodwar->printf("Enemy Cloaked Unit Detected!");
		enemyCloakedDetected = true;
	}
    
    drawProductionInformation(10, 30);
//	if (Options::Debug::DRAW_UALBERTABOT_DEBUG) BWAPI::Broodwar->drawTextScreen(447, 17, "\x07 %d", BuildingManager::Instance().getReservedMinerals());
}

// on unit destroy
void ProductionManager::onUnitDestroy(BWAPI::Unit * unit)
{
	// we don't care if it's not our unit
	if (!unit || unit->getPlayer() != BWAPI::Broodwar->self())
	{
		return;
	}
		
	if (Options::Modules::USING_MACRO_SEARCH)
	{
		// if it's a worker or a building, we need to re-search for the current goal
		if ((unit->getType().isWorker() && !WorkerManager::Instance().isWorkerScout(unit)) || unit->getType().isBuilding())
		{
			BWAPI::Broodwar->printf("Critical unit died, re-searching build order");

			if (unit->getType() != BWAPI::UnitTypes::Zerg_Drone)
			{
				performBuildOrderSearch(StrategyManager::Instance().getBuildOrderGoal());
			}
		}
	}
}

void ProductionManager::manageBuildOrderQueue() 
{
	// if there is nothing in the queue, oh well
	if (queue.isEmpty()) 
	{
		return;
	}

	// the current item to be used
	BuildOrderItem & currentItem = queue.getHighestPriorityItem();

	// while there is still something left in the queue
	while (!queue.isEmpty()) 
	{
		// this is the unit which can produce the currentItem
        BWAPI::Unit * producer = getProducer(currentItem.metaType);

		// check to see if we can make it right now
		bool canMake = canMakeNow(producer, currentItem.metaType);

		// if we try to build too many refineries manually remove it
		if (currentItem.metaType.isRefinery() && (BWAPI::Broodwar->self()->allUnitCount(BWAPI::Broodwar->self()->getRace().getRefinery() >= 3)))
		{
			queue.removeCurrentHighestPriorityItem();
			break;
		}

		// if the next item in the list is a building and we can't yet make it
        if (currentItem.metaType.isBuilding() && !(producer && canMake) && !currentItem.metaType.unitType.isAddon())
		{
			// construct a temporary building object
			Building b(currentItem.metaType.unitType, BWAPI::Broodwar->self()->getStartLocation());

			// set the producer as the closest worker, but do not set its job yet
			producer = WorkerManager::Instance().getBuilder(b, false);

			// predict the worker movement to that building location
			predictWorkerMovement(b);
		}

		// if we can make the current item
		if (producer && canMake) 
		{
			// create it
			createMetaType(producer, currentItem.metaType);
			assignedWorkerForThisBuilding = false;
			haveLocationForThisBuilding = false;

			// and remove it from the queue
			queue.removeCurrentHighestPriorityItem();

			// don't actually loop around in here
			break;
		}
		// otherwise, if we can skip the current item
		else if (queue.canSkipItem())
		{
			// skip it
			queue.skipItem();

			// and get the next one
			currentItem = queue.getNextHighestPriorityItem();				
		}
		else 
		{
			// so break out
			break;
		}
	}
}

BWAPI::Unit * ProductionManager::getProducer(MetaType t, BWAPI::Position closestTo)
{
    // get the type of unit that builds this
    BWAPI::UnitType producerType = t.whatBuilds();

    // make a set of all candidate producers
    std::set<BWAPI::Unit *> candidateProducers;
    BOOST_FOREACH (BWAPI::Unit * unit, BWAPI::Broodwar->self()->getUnits())
    {
        UAB_ASSERT(unit != NULL, "Unit was null");

        // reasons a unit can not train the desired type
        if (unit->getType() != producerType)                    { continue; }
        if (!unit->isCompleted())                               { continue; }
        if (unit->isTraining())                                 { continue; }
        if (unit->isLifted())                                   { continue; }
        if (unit->isUnpowered())                                { continue; }

        // if the type is an addon, some special cases
        if (t.unitType.isAddon())
        {
            // if the unit already has an addon, it can't make one
            if (unit->getAddon() != NULL)
            {
                continue;
            }

            // if we just told this unit to build an addon, then it will not be building another one
            // this deals with the frame-delay of telling a unit to build an addon and it actually starting to build
            if (unit->getLastCommand().getType() == BWAPI::UnitCommandTypes::Build_Addon) 
            { 
                continue; 
            }

            bool isBlocked = false;

            // if the unit doesn't have space to build an addon, it can't make one
            BWAPI::TilePosition addonPosition(unit->getTilePosition().x() + unit->getType().tileWidth(), unit->getTilePosition().y() + unit->getType().tileHeight() - t.unitType.tileHeight());
            BWAPI::Broodwar->drawBoxMap(addonPosition.x()*32, addonPosition.y()*32, addonPosition.x()*32 + 64, addonPosition.y()*32 + 64, BWAPI::Colors::Red);
            
            for (int i=0; i<unit->getType().tileWidth() + t.unitType.tileWidth(); ++i)
            {
                for (int j=0; j<unit->getType().tileHeight(); ++j)
                {
                    BWAPI::TilePosition tilePos(unit->getTilePosition().x() + i, unit->getTilePosition().y() + j);

                    // if the map won't let you build here, we can't build it
                    if (!BWAPI::Broodwar->isBuildable(tilePos))
                    {
                        isBlocked = true;
                        BWAPI::Broodwar->drawBoxMap(tilePos.x()*32, tilePos.y()*32, tilePos.x()*32 + 32, tilePos.y()*32 + 32, BWAPI::Colors::Red);
                    }

                    // if there are any units on the addon tile, we can't build it
                    std::set<BWAPI::Unit *> uot = BWAPI::Broodwar->getUnitsOnTile(tilePos.x(), tilePos.y());
                    if (uot.size() > 0 && !(uot.size() == 1 && *(uot.begin()) == unit))
                    {
                        isBlocked = true;;
                        BWAPI::Broodwar->drawBoxMap(tilePos.x()*32, tilePos.y()*32, tilePos.x()*32 + 32, tilePos.y()*32 + 32, BWAPI::Colors::Red);
                    }
                }
            }

            if (isBlocked)
            {
                continue;
            }
        }
        
        // if the type requires an addon and the producer doesn't have one
        typedef std::pair<BWAPI::UnitType, int> ReqPair;
        BOOST_FOREACH (const ReqPair & pair , t.unitType.requiredUnits())
        {
            BWAPI::UnitType requiredType = pair.first;
            if (requiredType.isAddon())
            {
                if (!unit->getAddon() || (unit->getAddon()->getType() != requiredType))
                {
                    continue;
                }
            }
        }

        // if we haven't cut it, add it to the set of candidates
        candidateProducers.insert(unit);
    }

    return getClosestUnitToPosition(candidateProducers, closestTo);
}

BWAPI::Unit * ProductionManager::getClosestUnitToPosition(std::set<BWAPI::Unit *> & units, BWAPI::Position closestTo)
{
    if (units.size() == 0)
    {
        return NULL;
    }

    // if we don't care where the unit is return the first one we have
    if (closestTo == BWAPI::Positions::None)
    {
        return *(units.begin());
    }

    BWAPI::Unit * closestUnit = NULL;
    double minDist(1000000);

	BOOST_FOREACH (BWAPI::Unit * unit, units) 
    {
        UAB_ASSERT(unit != NULL, "Unit was null");

		double distance = unit->getDistance(closestTo);
		if (!closestUnit || distance < minDist) 
        {
			closestUnit = unit;
			minDist = distance;
		}
	}

    return closestUnit;
}

// this function will check to see if all preconditions are met and then create a unit
void ProductionManager::createMetaType(BWAPI::Unit * producer, MetaType t) 
{
    if (!producer)
    {
        return;
    }

    // if we're dealing with a building
    if (t.isUnit() && t.unitType.isBuilding() 
        && t.unitType != BWAPI::UnitTypes::Zerg_Lair 
        && t.unitType != BWAPI::UnitTypes::Zerg_Hive
        && t.unitType != BWAPI::UnitTypes::Zerg_Greater_Spire
        && !t.unitType.isAddon())
    {
        // send the building task to the building manager
        BuildingManager::Instance().addBuildingTask(t.unitType, BWAPI::Broodwar->self()->getStartLocation());
    }
    else if (t.unitType.isAddon())
    {
        BWAPI::Broodwar->printf("Building Addon");
        //BWAPI::TilePosition addonPosition(producer->getTilePosition().x() + producer->getType().tileWidth(), producer->getTilePosition().y() + producer->getType().tileHeight() - t.unitType.tileHeight());
        producer->buildAddon(t.unitType);
    }
    // if we're dealing with a non-building unit
    else if (t.isUnit()) 
    {
        // if the race is zerg, morph the unit
        if (t.unitType.getRace() == BWAPI::Races::Zerg) 
        {
            producer->morph(t.unitType);
        // if not, train the unit
        } 
        else 
        {
            producer->train(t.unitType);
        }
    }
    // if we're dealing with a tech research
    else if (t.isTech())
    {
        producer->research(t.techType);
    }
    else if (t.isUpgrade())
    {
        //Logger::Instance().log("Produce Upgrade: " + t.getName() + "\n");
        producer->upgrade(t.upgradeType);
    }
    else
    {	
		
    }
}

bool ProductionManager::canMakeNow(BWAPI::Unit * producer, MetaType t)
{
    //UAB_ASSERT(producer != NULL, "Producer was null");

	bool canMake = meetsReservedResources(t);
	if (canMake)
	{
		if (t.isUnit())
		{
			canMake = BWAPI::Broodwar->canMake(producer, t.unitType);
		}
		else if (t.isTech())
		{
			canMake = BWAPI::Broodwar->canResearch(producer, t.techType);
		}
		else if (t.isUpgrade())
		{
			canMake = BWAPI::Broodwar->canUpgrade(producer, t.upgradeType);
		}
		else
		{	
			UAB_ASSERT(false, "Unknown type");
		}
	}

	return canMake;
}

bool ProductionManager::detectBuildOrderDeadlock()
{
	// if the queue is empty there is no deadlock
	if (queue.size() == 0 || BWAPI::Broodwar->self()->supplyTotal() >= 390)
	{
		return false;
	}

	// are any supply providers being built currently
	bool supplyInProgress =		BuildingManager::Instance().isBeingBuilt(BWAPI::Broodwar->self()->getRace().getCenter()) || 
								BuildingManager::Instance().isBeingBuilt(BWAPI::Broodwar->self()->getRace().getSupplyProvider());

	// does the current item being built require more supply
	int supplyCost			= queue.getHighestPriorityItem().metaType.supplyRequired();
	int supplyAvailable		= std::max(0, BWAPI::Broodwar->self()->supplyTotal() - BWAPI::Broodwar->self()->supplyUsed());

	// if we don't have enough supply and none is being built, there's a deadlock
	if ((supplyAvailable < supplyCost) && !supplyInProgress)
	{
		return true;
	}

	return false;
}

// When the next item in the queue is a building, this checks to see if we should move to it
// This function is here as it needs to access prodction manager's reserved resources info
void ProductionManager::predictWorkerMovement(const Building & b)
{
	// get a possible building location for the building
	if (!haveLocationForThisBuilding)
	{
		predictedTilePosition = BuildingManager::Instance().getBuildingLocation(b);
	}

	if (predictedTilePosition != BWAPI::TilePositions::None)
	{
		haveLocationForThisBuilding		= true;
	}
	else
	{
		return;
	}
	
	// draw a box where the building will be placed
	int x1 = predictedTilePosition.x() * 32;
	int x2 = x1 + (b.type.tileWidth()) * 32;
	int y1 = predictedTilePosition.y() * 32;
	int y2 = y1 + (b.type.tileHeight()) * 32;
	if (Options::Debug::DRAW_UALBERTABOT_DEBUG) 
    {
        BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Blue, false);
    }

	// where we want the worker to walk to
	BWAPI::Position walkToPosition		= BWAPI::Position(x1 + (b.type.tileWidth()/2)*32, y1 + (b.type.tileHeight()/2)*32);

	// compute how many resources we need to construct this building
	int mineralsRequired				= std::max(0, b.type.mineralPrice() - getFreeMinerals());
	int gasRequired						= std::max(0, b.type.gasPrice() - getFreeGas());

	// get a candidate worker to move to this location
	BWAPI::Unit * moveWorker			= WorkerManager::Instance().getMoveWorker(walkToPosition);

	// Conditions under which to move the worker: 
	//		- there's a valid worker to move
	//		- we haven't yet assigned a worker to move to this location
	//		- the build position is valid
	//		- we will have the required resources by the time the worker gets there
	if (moveWorker && haveLocationForThisBuilding && !assignedWorkerForThisBuilding && (predictedTilePosition != BWAPI::TilePositions::None) &&
		WorkerManager::Instance().willHaveResources(mineralsRequired, gasRequired, moveWorker->getDistance(walkToPosition)) )
	{
		// we have assigned a worker
		assignedWorkerForThisBuilding = true;

		// tell the worker manager to move this worker
		WorkerManager::Instance().setMoveWorker(mineralsRequired, gasRequired, walkToPosition);
	}
}

void ProductionManager::performCommand(BWAPI::UnitCommandType t) 
{
	// if it is a cancel construction, it is probably the extractor trick
	if (t == BWAPI::UnitCommandTypes::Cancel_Construction)
	{
		BWAPI::Unit * extractor = NULL;
		BOOST_FOREACH(BWAPI::Unit * unit, BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType() == BWAPI::UnitTypes::Zerg_Extractor)
			{
				extractor = unit;
			}
		}

		if (extractor)
		{
			extractor->cancelConstruction();
		}
	}
}

int ProductionManager::getFreeMinerals()
{
	return BWAPI::Broodwar->self()->minerals() - BuildingManager::Instance().getReservedMinerals();
}

int ProductionManager::getFreeGas()
{
	return BWAPI::Broodwar->self()->gas() - BuildingManager::Instance().getReservedGas();
}

// return whether or not we meet resources, including building reserves
bool ProductionManager::meetsReservedResources(MetaType type) 
{
	// return whether or not we meet the resources
	return (type.mineralPrice() <= getFreeMinerals()) && (type.gasPrice() <= getFreeGas());
}


// selects a unit of a given type
BWAPI::Unit * ProductionManager::selectUnitOfType(BWAPI::UnitType type, BWAPI::Position closestTo) 
{
	// if we have none of the unit type, return NULL right away
	if (BWAPI::Broodwar->self()->completedUnitCount(type) == 0) 
	{
		return NULL;
	}

	BWAPI::Unit * unit = NULL;

	// if we are concerned about the position of the unit, that takes priority
    if (closestTo != BWAPI::Positions::None) 
    {
		double minDist(1000000);

		BOOST_FOREACH (BWAPI::Unit * u, BWAPI::Broodwar->self()->getUnits()) 
        {
			if (u->getType() == type) 
            {
				double distance = u->getDistance(closestTo);
				if (!unit || distance < minDist) {
					unit = u;
					minDist = distance;
				}
			}
		}

	// if it is a building and we are worried about selecting the unit with the least
	// amount of training time remaining
	} 
    else if (type.isBuilding()) 
    {
		BOOST_FOREACH (BWAPI::Unit * u, BWAPI::Broodwar->self()->getUnits()) 
        {
            UAB_ASSERT(u != NULL, "Unit was null");

			if (u->getType() == type && u->isCompleted() && !u->isTraining() && !u->isLifted() &&!u->isUnpowered()) {

				return u;
			}
		}
		// otherwise just return the first unit we come across
	} 
    else 
    {
		BOOST_FOREACH(BWAPI::Unit * u, BWAPI::Broodwar->self()->getUnits()) 
		{
            UAB_ASSERT(u != NULL, "Unit was null");

			if (u->getType() == type && u->isCompleted() && u->getHitPoints() > 0 && !u->isLifted() &&!u->isUnpowered()) 
			{
				return u;
			}
		}
	}

	// return what we've found so far
	return NULL;
}

void ProductionManager::populateTypeCharMap()
{
	typeCharMap['p'] = MetaType(BWAPI::UnitTypes::Protoss_Probe);
	typeCharMap['z'] = MetaType(BWAPI::UnitTypes::Protoss_Zealot);
	typeCharMap['d'] = MetaType(BWAPI::UnitTypes::Protoss_Dragoon);
	typeCharMap['t'] = MetaType(BWAPI::UnitTypes::Protoss_Dark_Templar);
	typeCharMap['c'] = MetaType(BWAPI::UnitTypes::Protoss_Corsair);
	typeCharMap['e'] = MetaType(BWAPI::UnitTypes::Protoss_Carrier);
	typeCharMap['h'] = MetaType(BWAPI::UnitTypes::Protoss_High_Templar);
	typeCharMap['n'] = MetaType(BWAPI::UnitTypes::Protoss_Photon_Cannon);
	typeCharMap['a'] = MetaType(BWAPI::UnitTypes::Protoss_Arbiter);
	typeCharMap['r'] = MetaType(BWAPI::UnitTypes::Protoss_Reaver);
	typeCharMap['o'] = MetaType(BWAPI::UnitTypes::Protoss_Observer);
	typeCharMap['s'] = MetaType(BWAPI::UnitTypes::Protoss_Scout);
	typeCharMap['l'] = MetaType(BWAPI::UpgradeTypes::Leg_Enhancements);
	typeCharMap['v'] = MetaType(BWAPI::UpgradeTypes::Singularity_Charge);
}

void ProductionManager::drawProductionInformation(int x, int y)
{
	// fill prod with each unit which is under construction
	std::vector<BWAPI::Unit *> prod;
	BOOST_FOREACH (BWAPI::Unit * unit, BWAPI::Broodwar->self()->getUnits())
	{
        UAB_ASSERT(unit != NULL, "Unit was null");

		if (unit->isBeingConstructed())
		{
			prod.push_back(unit);
		}
	}
	
	// sort it based on the time it was started
	std::sort(prod.begin(), prod.end(), CompareWhenStarted());

	if (Options::Debug::DRAW_UALBERTABOT_DEBUG) BWAPI::Broodwar->drawTextScreen(x, y, "\x04 Build Order Info:");
	if (Options::Debug::DRAW_UALBERTABOT_DEBUG) BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04UNIT NAME");

	size_t reps = prod.size() < 10 ? prod.size() : 10;

	y += 40;
	int yy = y;

	// for each unit in the queue
	for (size_t i(0); i<reps; i++) 
    {
		std::string prefix = "\x07";

		yy += 10;

		BWAPI::UnitType t = prod[i]->getType();

		if (Options::Debug::DRAW_UALBERTABOT_DEBUG) BWAPI::Broodwar->drawTextScreen(x, yy, "%s%s", prefix.c_str(), t.getName().c_str());
		if (Options::Debug::DRAW_UALBERTABOT_DEBUG) BWAPI::Broodwar->drawTextScreen(x+150, yy, "%s%6d", prefix.c_str(), prod[i]->getRemainingBuildTime());
	}

	queue.drawQueueInformation(x, yy+10);
}

ProductionManager & ProductionManager::Instance()
{
	static ProductionManager instance;
	return instance;
}

void ProductionManager::onGameEnd()
{

}