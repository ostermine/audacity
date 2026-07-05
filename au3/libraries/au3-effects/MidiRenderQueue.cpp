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

std::vector<MidiRenderQueue::Note> MidiRenderQueue::Take()
{
    std::vector<Note> result = std::move(sPendingNotes);
    sPendingNotes.clear();
    return result;
}
