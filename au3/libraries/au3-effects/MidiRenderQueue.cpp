/**********************************************************************

  Audacity: A Digital Audio Editor

  MidiRenderQueue.cpp

**********************************************************************/
#include "MidiRenderQueue.h"

namespace {
std::vector<MidiRenderQueue::Note> sPendingNotes;
}

void MidiRenderQueue::Set(std::vector<Note> notes)
{
    sPendingNotes = std::move(notes);
}

const std::vector<MidiRenderQueue::Note>& MidiRenderQueue::Get()
{
    return sPendingNotes;
}
