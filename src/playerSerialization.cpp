#include <QFile>
#include <QDir>
#include "player.h"
#include "log.h"
#include "serialize.h"
#include "quest.h"
#include "items.h"

#define DEBUG_LOG false

bool Player::savePlayers(QList<Player*>& playersData)
{
    QFile playersFile("data/players/players.dat");
    if (!playersFile.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        logStatusError(tr("Error saving players database"));
        return false;
    }

    for (int i=0;i<playersData.size();i++)
    {
        playersFile.write(playersData[i]->name.toLatin1());
        playersFile.write("\31");
        playersFile.write(playersData[i]->passhash.toLatin1());
        if (i+1!=playersData.size())
            playersFile.write("\n");
    }
    return true;
}

QList<Player*> Player::loadPlayers()
{
    QList<Player*> players;
    QFile playersFile("data/players/players.dat");
    if (!playersFile.exists())
    {
        logMessage(tr("Players database not found, creating it"));
        playersFile.open(QIODevice::WriteOnly);
        playersFile.close();
    }

    if (!playersFile.open(QIODevice::ReadOnly))
    {
        logStatusError(tr("Error reading players database"));
        return players;
    }
    QList<QByteArray> data = playersFile.readAll().split('\n');
    if (data.size()==1 && data[0].isEmpty())
    {
        logMessage(tr("Player database is empty. Continuing happily"));
        return players;
    }
    for (int i=0;i<data.size();i++)
    {
        QList<QByteArray> line = data[i].split('\31');
        if (line.size()!=2)
        {
            logStatusError(tr("Error reading players database"));
            return players;
        }
        Player* newPlayer = new Player;
        newPlayer->name = line[0];
        newPlayer->passhash = line[1];
        players << newPlayer;
    }
    logMessage(tr("Got %1 players in database").arg(players.size()));
    return players;
}

void Player::savePonies(Player *player, QList<Pony> ponies)
{
    logMessage(tr("UDP: Saving ponies for %1 (%2)").arg(player->pony.netviewId).arg(player->name));

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(player->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+player->name.toLatin1()+"/ponies.dat");
    file.open(QIODevice::ReadWrite | QIODevice::Truncate);
    for (int i=0; i<ponies.size(); i++)
    {
        file.write(ponies[i].ponyData);
        file.write(vectorToData(ponies[i].pos));
        file.write(stringToData(ponies[i].sceneName.toLower()));
    }
}

QList<Pony> Player::loadPonies(Player* player)
{
    QList<Pony> ponies;
    QFile file(QDir::currentPath()+"/data/players/"+player->name.toLatin1()+"/ponies.dat");
    if (!file.open(QIODevice::ReadOnly))
        return ponies;

    QByteArray data = file.readAll();

    int i=0;
    while (i<data.size())
    {
        Pony pony{player};
        // Read the ponyData
        unsigned strlen;
        unsigned lensize=0;
        {
            unsigned char num3; int num=0, num2=0;
            do {
                num3 = data[i+lensize]; lensize++;
                num |= (num3 & 0x7f) << num2;
                num2 += 7;
            } while ((num3 & 0x80) != 0);
            strlen = (uint) num;
        }
        int ponyDataSize = strlen+lensize+PONYDATA_SIZE;
        pony.ponyData = data.mid(i,ponyDataSize);
        pony.name = dataToString(pony.ponyData); // The name is the first elem
        //app.logMessage("Found pony : "+pony.name);
        i+=ponyDataSize;

        // Read pos
        UVector pos = dataToVector(data.mid(i,12));
        pony.pos = pos;
        i+=12;

        // Read sceneName
        unsigned strlen2;
        unsigned lensize2=0;
        {
            unsigned char num3; int num=0, num2=0;
            do {
                num3 = data[i+lensize2]; lensize2++;
                num |= (num3 & 0x7f) << num2;
                num2 += 7;
            } while ((num3 & 0x80) != 0);
            strlen2 = (uint) num;
        }
        pony.sceneName = data.mid(i+lensize2, strlen2).toLower();
        i+=strlen2+lensize2;

        // Create quests
        for (int i=0; i<Quest::quests.size(); i++)
        {
            Quest quest = Quest::quests[i];
            quest.setOwner(player);
            pony.quests << quest;
        }

        ponies << pony;
    }

    return ponies;
}

void Pony::saveQuests()
{
    logMessage(QObject::tr("UDP: Saving quests for %1 (%2)").arg(netviewId).arg(owner->name));

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/quests.dat");
    if (!file.open(QIODevice::ReadWrite))
    {
        logError(QObject::tr("Error saving quests for %1 (%2)").arg(netviewId).arg(owner->name));
        return;
    }
    QByteArray questData = file.readAll();

    // Try to find an existing entry for this pony, if found delete it. Then go at the end.
    for (int i=0; i<questData.size();)
    {
        // Read the name
        QString entryName = dataToString(questData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(questData.mid(i));
        //app.logMessage("saveQuests : Reading entry "+entryName);

        quint16 entryDataSize = 4 * dataToUint16(questData.mid(i+nameSize));
        if (entryName == this->name) // Delete the entry, we'll rewrite it at the end
        {
            questData.remove(i,nameSize+entryDataSize+2);
            break;
        }
        else
            i += nameSize+entryDataSize+2;
    }

    // Now add our data at the end of the file
    QByteArray newEntry = stringToData(this->name);
    newEntry += (quint8)(quests.size() & 0xFF);
    newEntry += (quint8)((quests.size() >> 8) & 0xFF);
    for (const Quest& quest : quests)
    {
        newEntry += (quint8)(quest.id & 0xFF);
        newEntry += (quint8)((quest.id >> 8) & 0xFF);
        newEntry += (quint8)(quest.state & 0xFF);
        newEntry += (quint8)((quest.state >> 8) & 0xFF);
    }
    questData += newEntry;
    file.resize(0);
    file.write(questData);
    file.close();
}

void Pony::loadQuests()
{
    logMessage(QObject::tr("UDP: Loading quests for %1 (%2)").arg(netviewId).arg(owner->name));

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/quests.dat");
    if (!file.open(QIODevice::ReadOnly))
    {
        logError(QObject::tr("Error loading quests for %1 (%2)").arg(netviewId).arg(owner->name));
        return;
    }
    QByteArray questData = file.readAll();
    file.close();

    // Try to find an existing entry for this pony and load it.
    for (int i=0; i<questData.size();)
    {
        // Read the name
        QString entryName = dataToString(questData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(questData.mid(i));
        i+=nameSize;
        //app.logMessage("loadQuests : Reading entry "+entryName);

        quint16 nquests = dataToUint16(questData.mid(i));
        i+=2;
        if (entryName == this->name) // Read the entry
        {
            for (int j=0; j<nquests; j++)
            {
                quint16 questId = dataToUint16(questData.mid(i));
                quint16 questState = dataToUint16(questData.mid(i+2));
                i+=4;
                for (Quest& quest : quests)
                {
                    if (quest.id == questId)
                    {
                        quest.state = questState;
                        break;
                    }
                }
            }
            return;
        }
        else
            i += nquests * 4;
    }
}

void Pony::saveInventory()
{
    logMessage(QObject::tr("UDP: Saving inventory for %1 (%2)").arg(netviewId).arg(owner->name));

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/inventory.dat");
    if (!file.open(QIODevice::ReadWrite))
    {
        logError(QObject::tr("Error saving inventory for %1 (%2)").arg(netviewId).arg(owner->name));
        return;
    }
    QByteArray invData = file.readAll();

    // Try to find an existing entry for this pony, if found delete it. Then go at the end.
    for (int i=0; i<invData.size();)
    {
        // Read the name
        QString entryName = dataToString(invData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(invData.mid(i));
        //app.logMessage("saveInventory : Reading entry "+entryName);

        quint8 invSize = invData[i+nameSize+4];
        quint8 wornSize = invData[i+nameSize+4+1+invSize*9]; // Serialized sizeof InventoryItem is 9
        if (entryName == this->name) // Delete the entry, we'll rewrite it at the end
        {
            invData.remove(i,nameSize+4+1+invSize*9+1+wornSize*5);
            break;
        }
        else // Skip this entry
            i += nameSize+4+1+invSize*9+1+wornSize*5;
    }

    // Now add our data at the end of the file
    QByteArray newEntry = stringToData(this->name);
    newEntry += uint32ToData(nBits);
    newEntry += uint8ToData(inv.size());
    for (const InventoryItem& item : inv)
    {
        newEntry += uint8ToData(item.index);
        newEntry += uint32ToData(item.id);
        newEntry += uint32ToData(item.amount);
    }
    newEntry += uint8ToData(worn.size());
    for (const WearableItem& item : worn)
    {
        newEntry += uint8ToData(item.index);
        newEntry += uint32ToData(item.id);
    }
    invData += newEntry;
    file.resize(0);
    file.write(invData);
    file.close();
}

bool Pony::loadInventory()
{
    logMessage(QObject::tr("UDP: Loading inventory for %1 (%2)").arg(netviewId).arg(owner->name));

    QDir playerPath(QDir::currentPath());
    playerPath.cd("data");
    playerPath.cd("players");
    playerPath.mkdir(owner->name.toLatin1());

    QFile file(QDir::currentPath()+"/data/players/"+owner->name.toLatin1()+"/inventory.dat");
    if (!file.open(QIODevice::ReadOnly))
    {
        logError(QObject::tr("Error loading inventory for %1 (%2)").arg(netviewId).arg(owner->name));
        return false;
    }
    QByteArray invData = file.readAll();
    file.close();

    // Try to find an existing entry for this pony, if found load it.
    for (int i=0; i<invData.size();)
    {
        // Read the name
        QString entryName = dataToString(invData.mid(i));
        int nameSize = entryName.size()+getVUint32Size(invData.mid(i));
        //app.logMessage("loadInventory : Reading entry "+entryName);

        quint8 invSize = invData[i+nameSize+4];
        quint8 wornSize = invData[i+nameSize+4+1+invSize*9]; // Serialized sizeof InventoryItem is 9
        if (entryName == this->name)
        {
            i += nameSize;
            nBits = dataToUint32(invData.mid(i));
            i+=5; // Skip nBits and invSize
            inv.clear();
            for (int j=0; j<invSize; j++)
            {
                InventoryItem item;
                item.index = invData[i];
                i++;
                item.id = dataToUint32(invData.mid(i));
                i+=4;
                item.amount = dataToUint32(invData.mid(i));
                i+=4;
                inv.append(item);
            }
            i++; // Skip wornSize
            worn.clear();
            wornSlots = 0;
            for (int j=0; j<wornSize; j++)
            {
                WearableItem item;
                item.index = invData[i];
                i++;
                item.id = dataToUint32(invData.mid(i));
                i+=4;
                worn.append(item);
                wornSlots |= wearablePositionsMap[item.id];
            }
            return true;
        }
        else // Skip this entry
            i += nameSize+4+1+invSize*9+1+wornSize*5;
    }
    return false; // Entry not found
}
