/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiInstrument.h

  (AU4 DAW fork) VST instrument binding of a MIDI-flagged wave track.

**********************************************************************/
#ifndef __AUDACITY_MIDI_INSTRUMENT__
#define __AUDACITY_MIDI_INSTRUMENT__

#include <string>

#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

class WaveTrack;

//! Attachment storing which instrument renders the notes of a MIDI-flagged
//! wave track. Holds the AU4 effect id string; empty means "not assigned".
//! Serialized as a <midiinstrument> child of <wavetrack>.
class WAVE_TRACK_API MidiInstrument final : public ClientData::Cloneable<>, public XMLTagHandler
{
public:
    MidiInstrument() = default;
    MidiInstrument(const MidiInstrument&) = default;
    MidiInstrument& operator=(const MidiInstrument&) = delete;
    ~MidiInstrument() override = default;
    std::unique_ptr<ClientData::Cloneable<> > Clone() const override;

    static MidiInstrument& Get(WaveTrack& track);
    static const MidiInstrument& Get(const WaveTrack& track);

    const std::string& EffectId() const { return mEffectId; }
    void SetEffectId(std::string effectId);

    static const std::string& XMLTag();
    bool HandleXMLTag(
        const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view& tag) override;
    void WriteXML(XMLWriter& xmlFile) const;

private:
    std::string mEffectId;
};

#endif // __AUDACITY_MIDI_INSTRUMENT__
