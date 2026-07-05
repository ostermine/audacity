/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiInstrument.cpp

  (AU4 DAW fork) VST instrument binding of a MIDI-flagged wave track.

**********************************************************************/
#include "MidiInstrument.h"

#include "WaveTrack.h"

static const ChannelGroup::Attachments::RegisteredFactory midiInstrumentFactory {
    [](auto&) { return std::make_unique<MidiInstrument>(); }
};

std::unique_ptr<ClientData::Cloneable<> > MidiInstrument::Clone() const
{
    return std::make_unique<MidiInstrument>(*this);
}

MidiInstrument& MidiInstrument::Get(WaveTrack& track)
{
    return track.Attachments::Get<MidiInstrument>(midiInstrumentFactory);
}

const MidiInstrument& MidiInstrument::Get(const WaveTrack& track)
{
    return Get(const_cast<WaveTrack&>(track));
}

void MidiInstrument::SetEffectId(std::string effectId)
{
    mEffectId = std::move(effectId);
}

static constexpr auto EffectId_attr = "effectid";

const std::string& MidiInstrument::XMLTag()
{
    static const std::string result{ "midiinstrument" };
    return result;
}

bool MidiInstrument::HandleXMLTag(
    const std::string_view& tag, const AttributesList& attrs)
{
    if (tag != XMLTag()) {
        return false;
    }

    for (const auto& pair : attrs) {
        if (pair.first == EffectId_attr) {
            mEffectId = pair.second.ToWString().ToStdString();
        }
    }
    return true;
}

XMLTagHandler* MidiInstrument::HandleXMLChild(const std::string_view&)
{
    return nullptr;
}

void MidiInstrument::WriteXML(XMLWriter& xmlFile) const
{
    if (mEffectId.empty()) {
        return;
    }

    xmlFile.StartTag(XMLTag());
    xmlFile.WriteAttr(EffectId_attr, wxString(mEffectId));
    xmlFile.EndTag(XMLTag());
}

// Hook the attachment into WaveTrack's XML serialization
static WaveTrackIORegistry::ObjectReaderEntry midiInstrumentReader {
    MidiInstrument::XMLTag(),
    [](WaveTrack& track) { return &MidiInstrument::Get(track); }
};

static WaveTrackIORegistry::ObjectWriterEntry midiInstrumentWriter {
    [](const WaveTrack& track, auto& xmlFile) {
        MidiInstrument::Get(track).WriteXML(xmlFile);
    }
};
