#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/AppController.h"

#include <QUuid>

using namespace drift::appcontroller_detail;

bool AppController::ignoreGrouping() const
{
    return m_ignoreGrouping;
}

void AppController::setIgnoreGrouping(bool ignore)
{
    if (m_ignoreGrouping == ignore)
        return;
    m_ignoreGrouping = ignore;
    emit ignoreGroupingChanged();
}

void AppController::toggleIgnoreGrouping()
{
    setIgnoreGrouping(!m_ignoreGrouping);
}

bool AppController::canGroupSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    int valid = 0;
    for (const QPair<int, int> &pair : pairs) {
        if (isValidClipIndex(pair.first, pair.second) && !m_project.tracks().at(pair.first).locked)
            ++valid;
    }
    return valid >= 2;
}

void AppController::groupSelectedClips()
{
    if (!canGroupSelection())
        return;

    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    const drift::Project before = m_project.detachedCopy();
    const QString newGroupId = newClipId();

    QSet<QString> oldGroupIds;
    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second) || m_project.tracks().at(pair.first).locked)
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        if (!clip.linkId.isEmpty() && clip.linkId != newGroupId)
            oldGroupIds.insert(clip.linkId);
        clip.linkId = newGroupId;
        clip.linkLoose = true;
    }

    // Clean up any former groups that now have < 2 members remaining in the project
    for (const QString &oldId : std::as_const(oldGroupIds)) {
        int count = 0;
        QPair<int, int> remaining{-1, -1};
        for (int t = 0; t < m_project.tracks().size(); ++t) {
            for (int c = 0; c < m_project.tracks()[t].clips.size(); ++c) {
                if (m_project.tracks()[t].clips[c].linkId == oldId) {
                    ++count;
                    remaining = {t, c};
                }
            }
        }
        if (count < 2 && remaining.first >= 0) {
            m_project.tracks()[remaining.first].clips[remaining.second].linkId.clear();
            m_project.tracks()[remaining.first].clips[remaining.second].linkLoose = false;
        }
    }

    const QString label = tr("Group Events");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

bool AppController::canUngroupSelection() const
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        if (!m_project.tracks().at(pair.first).clips.at(pair.second).linkId.isEmpty())
            return true;
    }
    return false;
}

void AppController::ungroupSelectedClips()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    const drift::Project before = m_project.detachedCopy();
    bool changed = false;
    QSet<QString> affectedGroupIds;

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second) || m_project.tracks().at(pair.first).locked)
            continue;
        drift::Clip &clip = m_project.tracks()[pair.first].clips[pair.second];
        if (clip.linkId.isEmpty())
            continue;
        affectedGroupIds.insert(clip.linkId);
        clip.linkId.clear();
        clip.linkLoose = false;
        changed = true;
    }

    if (!changed)
        return;

    // Check if any affected group is now reduced to 1 clip
    for (const QString &groupId : std::as_const(affectedGroupIds)) {
        int count = 0;
        QPair<int, int> loneClip{-1, -1};
        for (int t = 0; t < m_project.tracks().size(); ++t) {
            for (int c = 0; c < m_project.tracks()[t].clips.size(); ++c) {
                if (m_project.tracks()[t].clips[c].linkId == groupId) {
                    ++count;
                    loneClip = {t, c};
                }
            }
        }
        if (count < 2 && loneClip.first >= 0) {
            m_project.tracks()[loneClip.first].clips[loneClip.second].linkId.clear();
            m_project.tracks()[loneClip.first].clips[loneClip.second].linkLoose = false;
        }
    }

    const QString label = tr("Ungroup Events");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

void AppController::clearGroupSelectedClips()
{
    QList<QPair<int, int>> pairs = m_selection;
    if (pairs.isEmpty() && m_selectedTrack >= 0 && m_selectedClip >= 0)
        pairs.append(qMakePair(m_selectedTrack, m_selectedClip));
    if (pairs.isEmpty())
        return;

    const drift::Project before = m_project.detachedCopy();
    bool changed = false;
    QSet<QString> clearedGroupIds;

    for (const QPair<int, int> &pair : pairs) {
        if (!isValidClipIndex(pair.first, pair.second))
            continue;
        const QString linkId = m_project.tracks().at(pair.first).clips.at(pair.second).linkId;
        if (linkId.isEmpty() || clearedGroupIds.contains(linkId))
            continue;
        clearedGroupIds.insert(linkId);
        for (drift::Track &track : m_project.tracks()) {
            for (drift::Clip &candidate : track.clips) {
                if (candidate.linkId == linkId) {
                    candidate.linkId.clear();
                    candidate.linkLoose = false;
                    changed = true;
                }
            }
        }
    }

    if (!changed)
        return;

    const QString label = tr("Clear Group");
    pushProjectEdit(before, label);
    syncClipModels();
    finishEdit(label);
}

void AppController::selectAllInGroup(int trackIndex, int clipIndex)
{
    if (!isValidClipIndex(trackIndex, clipIndex))
        return;
    const drift::Clip &clip = m_project.tracks().at(trackIndex).clips.at(clipIndex);
    if (clip.linkId.isEmpty()) {
        selectClip(trackIndex, clipIndex);
        return;
    }
    QList<QPair<int, int>> groupPairs;
    for (int t = 0; t < m_project.tracks().size(); ++t) {
        for (int c = 0; c < m_project.tracks()[t].clips.size(); ++c) {
            if (m_project.tracks()[t].clips[c].linkId == clip.linkId)
                groupPairs.append(qMakePair(t, c));
        }
    }
    m_selection = groupPairs;
    m_selectedTrack = trackIndex;
    m_selectedClip = clipIndex;
    notifySelectionChanged();
    emit selectedTransitionDataChanged();
}

bool AppController::canLinkSelection() const
{
    return canGroupSelection();
}

void AppController::linkSelectedClips()
{
    groupSelectedClips();
}

bool AppController::canUnlinkSelection() const
{
    return canUngroupSelection();
}

void AppController::unlinkSelectedClips()
{
    ungroupSelectedClips();
}
