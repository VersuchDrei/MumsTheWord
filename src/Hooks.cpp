#include "Hooks.h"

#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/API.h"
#include "Settings.h"
#include "RE/E/ExtraDataList.h"

namespace {
    class PlayerCharacterEx : public RE::PlayerCharacter {
    public:
        void TryToSteal(RE::TESObjectREFR* a_fromRefr, RE::TESForm* a_item, RE::ExtraDataList* a_extraList) {
            auto currentProcess = GetActorRuntimeData().currentProcess;
            if (!a_fromRefr || !currentProcess || !currentProcess->high) {
                return;
            }

            // Pick-up passes the ref as its own source; ground items have nobody to steal from.
            if (a_fromRefr->GetBaseObject() == a_item) {
                return;
            }

            if (*Settings::useThreshold) {
                auto value = a_item->GetGoldValue();
                if (value > *Settings::costThreshold) {
                    return;
                }
            }

            bool stolen = false;
            auto owner = a_fromRefr->GetOwner();
            if (!owner && a_fromRefr->Is(RE::FormType::ActorCharacter) && !a_fromRefr->IsDead(true)) {
                stolen = a_fromRefr != this;
            } else if (owner && owner != this) {
                stolen = true;
                if (owner->Is(RE::FormType::Faction)) {
                    auto faction = static_cast<RE::TESFaction*>(owner);
                    if (IsInFaction(faction)) {
                        stolen = false;
                    }
                }
            }

            if (stolen) {
                auto high = currentProcess->high;
                std::vector<RE::NiPointer<RE::Actor>> actors;
                {
                    RE::BSReadLockGuard locker(high->knowledgeLock);
                    for (auto& knowledgeData : high->knowledgeArray) {
                        auto& knowledge = knowledgeData.second;
                        if (knowledge) {
                            auto akRef = knowledge->target.get();
                            if (akRef) {
                                actors.emplace_back(std::move(akRef));
                            }
                        }
                    }
                }

                bool detected = false;
                for (auto& actor : actors) {
                    if (!actor->IsPlayerTeammate() && !actor->IsDead(true) && actor->RequestDetectionLevel(this) > 0) {
                        detected = true;
                        break;
                    }
                }

                if (!detected && a_extraList) {
                    auto xOwnership = a_extraList->GetByType<RE::ExtraOwnership>();
                    if (xOwnership) {
                        xOwnership->owner = this;
                    } else {
                        xOwnership = new RE::ExtraOwnership(this);
                        a_extraList->Add(xOwnership);
                    }
                }
            }
        }
    };

    template <class F, class T>
    static void write_vfunc() {
        REL::Relocation<std::uintptr_t> vtbl{F::VTABLE[0]};
        T::func = vtbl.write_vfunc(T::idx, T::thunk);
    }
    struct AddObjectToContainerHook {
        static void thunk(PlayerCharacterEx* player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, int32_t a_count,
                          RE::TESObjectREFR* a_fromRefr) {
            if (!a_extraList) {
                //a_extraList = new RE::ExtraDataList();
            }

            player->TryToSteal(a_fromRefr, a_object, a_extraList);
            func(player, a_object, a_extraList, a_count, a_fromRefr);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static inline int idx = 0x5A;

        // Install our hook at the specified address
        static inline void Install() {
            write_vfunc<RE::PlayerCharacter, AddObjectToContainerHook>();
        }
    };

    struct PickUpObjectHook {
        static void thunk(PlayerCharacterEx* player, RE::TESObjectREFR* a_item, uint32_t a_count, bool a_arg3, bool a_playSound) {
            player->TryToSteal(a_item, a_item->GetBaseObject(), &a_item->extraList);
            func(player, a_item, a_count, a_arg3, a_playSound);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static inline int idx = REL::Module::GetRuntime() != REL::Module::Runtime::VR ? 0xCC : 0xCE;

        // Install our hook at the specified address
        static inline void Install() {
            write_vfunc<RE::PlayerCharacter, PickUpObjectHook>();
        }
    };
    
}  // namespace

void InstallHooks() {
    AddObjectToContainerHook::Install();
    PickUpObjectHook::Install();
}