#include "UpgradeOrder.h"

using namespace UAlbertaBot;

UpgradeOrder::UpgradeOrder()
	: _race(BWAPI::Races::None)
{

}

UpgradeOrder::UpgradeOrder(const BWAPI::Race & race)
	: _race(race)
{

}

UpgradeOrder::UpgradeOrder(const BWAPI::Race & race, const std::vector<std::pair<MetaType, int>> & metaVector)
	: _race(race)
	, _upgradeOrder(metaVector)
{

}

void UpgradeOrder::add(const MetaType & t)
{
	UAB_ASSERT(t.getRace() == getRace(), "Trying to add difference Race metatype to upgrade order");
	UAB_ASSERT(t.isUpgrade(), "Trying to add something that is not an upgrade");

	int count = std::count_if(_upgradeOrder.begin(), _upgradeOrder.end(), [t](std::pair<MetaType, int> item) {return item.first.getUpgradeType() == t.getUpgradeType(); });

	// Place at front
	// Only upto max of the upgrade type
	if (count < BWAPI::Broodwar->self()->getMaxUpgradeLevel(t.getUpgradeType())) {
		_upgradeOrder.push_back(std::pair<MetaType, int>(t, count));
	}
}

const BWAPI::Race & UpgradeOrder::getRace() const
{
	return _race;
}

const size_t UpgradeOrder::size() const
{
	return _upgradeOrder.size();
}

bool UpgradeOrder::empty() const
{
	return _upgradeOrder.empty();
}

std::pair<MetaType, int> UpgradeOrder::getNextUpgrade() {
	return _upgradeOrder.front();
}


const std::pair<MetaType, int> & UpgradeOrder::operator [] (const size_t & index) const
{
	return _upgradeOrder[index];
}

std::pair<MetaType, int> & UpgradeOrder::operator [] (const size_t & index)
{
	return _upgradeOrder[index];
}
