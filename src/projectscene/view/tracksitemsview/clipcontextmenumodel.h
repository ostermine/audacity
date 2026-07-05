/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "context/iglobalcontext.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "effects/effects_base/ieffectsprovider.h"

#include "iprojectsceneconfiguration.h"
#include "types/projectscenetypes.h"

namespace au::projectscene {
class ClipContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    muse::GlobalInject<projectscene::IProjectSceneConfiguration> projectSceneConfiguration;
    muse::GlobalInject<effects::IEffectsProvider> effectsProvider;
    muse::ContextInject<context::IGlobalContext> globalContext{ this };

    Q_PROPERTY(ClipKey clipKey READ clipKey WRITE setClipKey NOTIFY clipKeyChanged FINAL)

public:
    ClipContextMenuModel() = default;

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void handleMenuItem(const QString& itemId) override;

    ClipKey clipKey() const;
    void setClipKey(const ClipKey& newClipKey);

signals:
    void clipKeyChanged();
    void clipTitleEditRequested();

private:
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;
    void updateStretchEnabledState(muse::uicomponents::MenuItem& item);
    void updatePitchSpeedModifiedEnabledState(muse::uicomponents::MenuItem& item);

    muse::uicomponents::MenuItemList makeClipColourItems();
    void updateColorCheckedState();
    void updateColorMenu();

    muse::uicomponents::MenuItem* makeMidiInstrumentMenu();
    bool isMidiLiveEnabled() const;

    ClipKey m_clipKey;
    muse::actions::ActionCodeList m_colorChangeActionCodeList;
};
}
