#include "faction.h"

#include <cassert>
#include <stdexcept>

#include "factionmatrix.h"


Faction::Faction(const std::string& key, const std::string& printName):
    m_key(key),
    m_printName(printName),
    m_factionMatrix(nullptr)
{

}

const std::string& Faction::key() const {
    return m_key;
}

void Faction::setPrintName(const std::string& printName) {
    m_printName = printName;
}

const std::string& Faction::printName() const {
    return m_printName;
}

FactionRelation& Faction::relationTo(Faction& other) {
    if (m_factionMatrix == nullptr) {
        throw std::logic_error("Faction must belong to a faction matrix before querying relations.");
    }
    return m_factionMatrix->getRelation(*this, other);
}

void Faction::setFactionMatrix(FactionMatrix& factionMatrix) {
    if (m_factionMatrix != nullptr && m_factionMatrix != &factionMatrix) {
        throw std::logic_error("A faction cannot belong to multiple faction matrices.");
    }
    m_factionMatrix = &factionMatrix;
}
