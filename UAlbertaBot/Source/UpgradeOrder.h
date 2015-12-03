#pragma once

#include "Common.h"
#include "MetaType.h"

namespace UAlbertaBot
{

class UpgradeOrder
{
    BWAPI::Race               _race;
	// MetaType and Level
    std::vector<std::pair<MetaType,int>>     _upgradeOrder;
	mutable size_t			  _currentUpgrade = 0;

public:

    UpgradeOrder();
	UpgradeOrder(const BWAPI::Race & race);
	UpgradeOrder(const BWAPI::Race & race, const std::vector<std::pair<MetaType, int>> & metaVector);
	
	// Peek
	std::pair<MetaType, int> getNextUpgrade();

	// add to queue
    void add(const MetaType & t);
	//remove from queue
	void UpgradeOrder::removefirst();

	// Check if empty
	bool empty() const;

    const size_t size() const;
    const BWAPI::Race & getRace() const;

	const std::pair<MetaType, int> & operator [] (const size_t & index) const;
	std::pair<MetaType, int> & operator [] (const size_t & index);

};

}
