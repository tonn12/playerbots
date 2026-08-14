
#include "playerbot/playerbot.h"
#include "FishAction.h"
#include "playerbot/TravelMgr.h"
#include "TellLosAction.h"
#include "EquipAction.h"

using namespace ai;

namespace
{
    WorldPosition GetFishSpotNearMaster(Player* master)
    {
        WorldPosition result;

        if (!master)
            return result;

        float maxDistance = sPlayerbotAIConfig.fishingMaxDistanceFromMaster;

        // Несколько попыток найти подходящую точку рядом с хозяином.
        for (uint8 attempt = 0; attempt < 20; ++attempt)
        {
            WorldPosition* candidate = sTravelMgr.GetFishSpot(WorldPosition(master), true);

            if (!candidate || !*candidate)
                continue;

            if (maxDistance <= 0.0f || candidate->distance(master) <= maxDistance)
                return *candidate;
        }

        return result;
    }
}

bool MoveToFishAction::isUseful()
{
    if (qualifier == "travel")
    {
        if (!AI_VALUE(bool, "travel target working"))
            return false;

        TravelTarget* target = AI_VALUE(TravelTarget*, "leader travel target");

        if (target->GetDestination()->GetPurpose() != TravelDestinationPurpose::GatherFishing)
            return false;
    }

    return true;
}

bool MoveToFishAction::Execute(Event& event)
{
    WorldPosition fishSpot =
        AI_VALUE2(WorldPosition, "custom position", "fish spot");

    WorldPosition fishAnchor =
        AI_VALUE2(WorldPosition, "custom position", "fish anchor");

    Player* master = GetMaster();

    bool fishNearMaster =
        qualifier != "travel" &&
        master &&
        fishAnchor;

    /*
     * Если включён режим fish here, а старая точка стала
     * слишком далеко от хозяина — забываем её.
     */
    if (fishNearMaster && fishSpot)
    {
        float maxDistance =
            sPlayerbotAIConfig.fishingMaxDistanceFromMaster;

        if (fishSpot.getMapId() != master->GetMapId() ||
            (maxDistance > 0.0f &&
             fishSpot.distance(master) > maxDistance))
        {
            RESET_AI_VALUE2(
                WorldPosition,
                "custom position",
                "fish spot");

            fishSpot = WorldPosition();
        }
    }

    /*
     * Оригинальная travel-рыбалка.
     */
    if (!fishSpot && qualifier == "travel")
    {
        TravelTarget* target =
            AI_VALUE(TravelTarget*, "leader travel target");

        fishSpot = *target->GetPosition();

        if (AI_VALUE(TravelTarget*, "travel target") != target)
            fishSpot = *sTravelMgr.GetFishSpot(bot, true);
    }

    /*
     * Выбираем новую fishing point.
     */
    if (!fishSpot)
    {
        if (fishNearMaster)
        {
            fishSpot = GetFishSpotNearMaster(master);
        }
        else
        {
            WorldPosition* candidate =
                sTravelMgr.GetFishSpot(bot);

            if (candidate)
                fishSpot = *candidate;
        }

        if (!fishSpot)
            return false;

        TravelPath movePath =
            sTravelNodeMap.getFullPath(bot, fishSpot, bot);

        if (movePath.empty())
        {
            RESET_AI_VALUE2(
                WorldPosition,
                "custom position",
                "fish spot");

            return false;
        }

        AI_VALUE(LastMovement&, "last movement").setPath(movePath);
    }

    SET_AI_VALUE2(
        WorldPosition,
        "custom position",
        "fish spot",
        fishSpot);

    if (fishSpot.distance(bot) < 1.0f)
        return false;

    return MoveTo(fishSpot);
}

bool FishAction::isUseful()
{
    if (qualifier == "travel")
    {
        if (!AI_VALUE(bool, "travel target working"))
            return false;

        TravelTarget* target = AI_VALUE(TravelTarget*, "leader travel target");

        if (target->GetDestination()->GetPurpose() != TravelDestinationPurpose::GatherFishing)
            return false;

        if (!bot->GetGroup() || ai->IsGroupLeader() || target->GetTimeLeft() < 0)
            target->CheckStatus();
    }

    WorldPosition fishSpot = AI_VALUE2(WorldPosition, "custom position", "fish spot");

    if (!fishSpot)
        return false;

    if (!AI_VALUE(bool, "can fish"))
        return false;

    if (fishSpot.distance(bot) > 1.0f)
        return false;

    return true;
}

bool FishAction::Execute(Event& event)
{
    if (qualifier == "travel")
    {
        if (!AI_VALUE(bool, "travel target working"))
            return false;
    }

    if (bot->IsMoving())
    {
        ai->StopMoving();
        SetDuration(100);
        return true;
    }

    WorldPosition fishSpot = AI_VALUE2(WorldPosition, "custom position", "fish spot");

    if (abs(fishSpot.getO() - bot->GetOrientation()) > 0.5)
    {
        bot->SetFacingTo(fishSpot.getO());
        SetDuration(100);
        return true;
    }

    ai->StopMoving();

    std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");

    if (poles.empty())
        return false;

    Item* pole = poles.front();
    uint8 bagIndex = pole->GetBagSlot();
    uint8 slot = pole->GetSlot();

    if (slot != EQUIPMENT_SLOT_MAINHAND)
        EquipAction::EquipItem(ai, GetMaster(), pole);

    Event fishCastEvent = Event("fish", "7731 " + chat->formatWorldobject(bot));
    bool didCast = CastCustomSpellAction::Execute(fishCastEvent);

    SetDuration(sPlayerbotAIConfig.globalCoolDown);

    return didCast;
}

bool UseFishingBobberAction::Execute(Event& event)
{
    std::list<GameObject*> objects = TellLosAction::GoGuidListToObjList(ai, AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los"));

    for (auto& obj : objects)
    {
        if (obj->GetEntry() != 35591)
            continue;

        if (obj->GetOwnerGuid() != bot->GetObjectGuid())
            continue;

        if (obj->GetLootState() != GO_READY)
        {
            time_t bobberActiveTime = obj->GetRespawnTime() - FISHING_BOBBER_READY_TIME;
            if (bobberActiveTime > time(0))
                SetDuration((bobberActiveTime - time(0)) * IN_MILLISECONDS + 500);
            else
                SetDuration(1000);
            return true;
        }

        std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
        *packet << obj->GetObjectGuid();
        bot->GetSession()->QueuePacket(std::move(packet));

        std::ostringstream out; out << "Opening " << chat->formatGameobject(obj);
        ai->TellPlayerNoFacing(ai->GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

        SetDuration(3000);

        if (!urand(0,10))
        {
            RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
        }

        return true;
    }

    return false;
}

bool FishCommandAction::Execute(Event& event)
{
    Player* requester =
        event.getOwner() ? event.getOwner() : GetMaster();

    std::string command = event.getParam();

    if (command == "stop" || command == "off")
    {
        ai->ChangeStrategy(
            "-fish",
            BotState::BOT_STATE_NON_COMBAT);

        RESET_AI_VALUE2(
            WorldPosition,
            "custom position",
            "fish spot");

        RESET_AI_VALUE2(
            WorldPosition,
            "custom position",
            "fish anchor");

        ai->TellPlayerNoFacing(
            requester,
            "Fishing stopped.");

        return true;
    }

    if (command.empty() ||
        command == "here" ||
        command == "on")
    {
        Player* master = GetMaster();

        if (!master)
        {
            ai->TellError(
                requester,
                "I have no master to fish near.");

            return false;
        }

        SET_AI_VALUE2(
            WorldPosition,
            "custom position",
            "fish anchor",
            WorldPosition(master));

        RESET_AI_VALUE2(
            WorldPosition,
            "custom position",
            "fish spot");

        ai->ChangeStrategy(
            "+fish",
            BotState::BOT_STATE_NON_COMBAT);

        ai->TellPlayerNoFacing(
            requester,
            "Fishing near master.");

        return true;
    }

    ai->TellPlayerNoFacing(
        requester,
        "Usage: fish here | fish stop");

    return false;
}